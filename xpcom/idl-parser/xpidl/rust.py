# rust.py - Generate rust bindings from IDL.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Print a runtime Rust bindings file for the IDL file specified"""

# --- Safety Hazards ---

# Generating rust bindings to C++ methods has some safety hazards which need to
# be kept an eye out for. These are a few of those hazards:

# notxpcom methods return their results directly by value. The x86 windows
# stdcall ABI returns aggregates by value differently for methods than
# functions, and rust only exposes the function ABI, so that's the one we're
# using. The correct ABI can be emulated for notxpcom methods returning
# aggregates by passing an &mut ReturnType parameter as the second parameter.
# This strategy is used by the winapi-rs crate.
# https://github.com/retep998/winapi-rs/blob/7338a5216a6a7abeefcc6bb1bc34381c81d3e247/src/macros.rs#L220-L231

# nostdcall methods on x86 windows will use the thiscall ABI, which is not
# stable in rust right now, so we cannot generate bindings to them.

# In general, passing C++ objects by value over the C ABI is not a good idea,
# and when possible we should avoid doing so.

import sys
import os.path
import re
import xpidl


class AutoIndent(object):
    """A small autoindenting wrapper around a fd.
    Used to make the code output more readable."""

    def __init__(self, fd):
        self.fd = fd
        self.indent = 0

    def write(self, string):
        """A smart write function which automatically adjusts the
        indentation of each line as it is written by counting braces"""
        for s in string.split('\n'):
            s = s.strip()
            indent = self.indent
            if len(s) == 0:
                indent = 0
            elif s[0] == '}':
                indent -= 1

            self.fd.write("    " * indent + s + "\n")
            for c in s:
                if c == '(' or c == '{' or c == '[':
                    self.indent += 1
                elif c == ')' or c == '}' or c == ']':
                    self.indent -= 1


def rustSanitize(s):
    keywords = [
        "abstract", "alignof", "as", "become", "box",
        "break", "const", "continue", "crate", "do",
        "else", "enum", "extern", "false", "final",
        "fn", "for", "if", "impl", "in",
        "let", "loop", "macro", "match", "mod",
        "move", "mut", "offsetof", "override", "priv",
        "proc", "pub", "pure", "ref", "return",
        "Self", "self", "sizeof", "static", "struct",
        "super", "trait", "true", "type", "typeof",
        "unsafe", "unsized", "use", "virtual", "where",
        "while", "yield"
    ]
    if s in keywords:
        return s + "_"
    return s


# printdoccomments = False
printdoccomments = True

if printdoccomments:
    def printComments(fd, clist, indent):
        fd.write("%s%s" % (indent, doccomments(clist)))

    def doccomments(clist):
        if len(clist) == 0:
            return ""
        s = "/// ```text"
        for c in clist:
            for cc in c.splitlines():
                s += "\n/// " + cc
        s += "\n/// ```\n///\n"
        return s

else:
    def printComments(fd, clist, indent):
        pass

    def doccomments(clist):
        return ""


def firstCap(str):
    return str[0].upper() + str[1:]


# Attribute VTable Methods
def attributeNativeName(a, getter):
    binaryname = rustSanitize(a.binaryname if a.binaryname else firstCap(a.name))
    return "%s%s" % ('Get' if getter else 'Set', binaryname)


def attributeParamName(a):
    return "a" + firstCap(a.name)


def attributeRawParamList(iface, a, getter):
    l = [(attributeParamName(a),
          a.realtype.rustType('out' if getter else 'in'))]
    if a.implicit_jscontext:
        raise xpidl.RustNoncompat("jscontext")
    if a.nostdcall:
        raise xpidl.RustNoncompat("nostdcall")
    return l


def attributeParamList(iface, a, getter):
    l = ["this: *const " + iface.name]
    l += ["%s: %s" % x for x in attributeRawParamList(iface, a, getter)]
    return ", ".join(l)


def attrAsVTableEntry(iface, m, getter):
    try:
        return "pub %s: unsafe extern \"system\" fn (%s) -> nsresult" % \
            (attributeNativeName(m, getter),
             attributeParamList(iface, m, getter))
    except xpidl.RustNoncompat as reason:
        return """\
/// Unable to generate binding, as `%s` is currently unsupported.
pub %s: *const ::libc::c_void""" % (reason, attributeNativeName(m, getter))


# Method VTable generation functions
def methodNativeName(m):
    binaryname = m.binaryname is not None and m.binaryname or firstCap(m.name)
    return rustSanitize(binaryname)


def methodReturnType(m):
    if m.notxpcom:
        return m.realtype.rustType('in').strip()
    return "nsresult"


def methodRawParamList(iface, m):
    l = [(rustSanitize(p.name), p.rustType()) for p in m.params]

    if m.implicit_jscontext:
        raise xpidl.RustNoncompat("jscontext")

    if m.optional_argc:
        raise xpidl.RustNoncompat("optional_argc")

    if m.nostdcall:
        raise xpidl.RustNoncompat("nostdcall")

    if not m.notxpcom and m.realtype.name != 'void':
        l.append(("_retval", m.realtype.rustType('out')))

    return l


def methodParamList(iface, m):
    l = ["this: *const %s" % iface.name]
    l += ["%s: %s" % x for x in methodRawParamList(iface, m)]
    return ", ".join(l)


def methodAsVTableEntry(iface, m):
    try:
        return "pub %s: unsafe extern \"system\" fn (%s) -> %s" % \
            (methodNativeName(m),
             methodParamList(iface, m),
             methodReturnType(m))
    except xpidl.RustNoncompat as reason:
        return """\
/// Unable to generate binding, as `%s` is currently unsupported.
pub %s: *const ::libc::c_void""" % (reason, methodNativeName(m))


method_impl_tmpl = """\
#[inline]
pub unsafe fn %(name)s(&self, %(params)s) -> %(ret_ty)s {
    ((*self.vtable).%(name)s)(self, %(args)s)
}
"""

def methodAsWrapper(iface, m):
    try:
        param_list = methodRawParamList(iface, m)
        params = ["%s: %s" % x for x in param_list]
        args = [x[0] for x in param_list]

        return method_impl_tmpl % {
            'name': methodNativeName(m),
            'params': ', '.join(params),
            'ret_ty': methodReturnType(m),
            'args': ', '.join(args),
        }
    except xpidl.RustNoncompat:
        # Dummy field for the doc comments to attach to.
        # Private so that it's not shown in rustdoc.
        return "const _%s: () = ();" % methodNativeName(m)


infallible_impl_tmpl = """\
#[inline]
pub unsafe fn %(name)s(&self) -> %(realtype)s {
    let mut result = <%(realtype)s as ::std::default::Default>::default();
    let _rv = ((*self.vtable).%(name)s)(self, &mut result);
    debug_assert!(::nserror::NsresultExt::succeeded(_rv));
    result
}
"""

def attrAsWrapper(iface, m, getter):
    try:
        if m.implicit_jscontext:
            raise xpidl.RustNoncompat("jscontext")

        if m.nostdcall:
            raise xpidl.RustNoncompat("nostdcall")

        name = attributeParamName(m)

        if getter and m.infallible:
            return infallible_impl_tmpl % {
                'name': attributeNativeName(m, getter),
                'realtype': m.realtype.rustType('in'),
            }

        rust_type = m.realtype.rustType('out' if getter else 'in')
        return method_impl_tmpl % {
            'name': attributeNativeName(m, getter),
            'params': name + ': ' + rust_type,
            'ret_ty': 'nsresult',
            'args': name,
        }

    except xpidl.RustNoncompat:
        # Dummy field for the doc comments to attach to.
        # Private so that it's not shown in rustdoc.
        return "const _%s: () = ();" % attributeNativeName(m, getter)


header = """\
//
// DO NOT EDIT.  THIS FILE IS GENERATED FROM %(filename)s
//

"""


def idl_basename(f):
    """returns the base name of a file with the last extension stripped"""
    return os.path.basename(f).rpartition('.')[0]


def print_rust_bindings(idl, fd, filename):
    fd = AutoIndent(fd)

    fd.write(header % {'filename': filename})

    # All of the idl files will be included into the same rust module, as we
    # can't do forward declarations. Because of this, we want to ignore all
    # import statements

    for p in idl.productions:
        if p.kind == 'include' or p.kind == 'cdata' or p.kind == 'forward':
            continue

        if p.kind == 'interface':
            write_interface(p, fd)
            continue

        if p.kind == 'typedef':
            try:
                # We have to skip the typedef of bool to bool (it doesn't make any sense anyways)
                if p.name == "bool":
                    continue

                if printdoccomments:
                    fd.write("/// `typedef %s %s;`\n///\n" %
                        (p.realtype.nativeType('in'), p.name))
                    fd.write(doccomments(p.doccomments))
                fd.write("pub type %s = %s;\n\n" % (p.name, p.realtype.rustType('in')))
            except xpidl.RustNoncompat as reason:
                fd.write("/* unable to generate %s typedef, as %s is not supported yet */\n\n" %
                         (p.name, reason))


uuid_decoder = re.compile(r"""(?P<m0>[a-f0-9]{8})-
                              (?P<m1>[a-f0-9]{4})-
                              (?P<m2>[a-f0-9]{4})-
                              (?P<m3>[a-f0-9]{4})-
                              (?P<m4>[a-f0-9]{12})$""", re.X)


base_vtable_tmpl = """
pub __base: %sVTable,

"""


vtable_tmpl = """\
#[doc(hidden)]
#[repr(C)]
pub struct %(name)sVTable {%(base)s%(entries)s}

"""


deref_tmpl = """\
impl ::std::ops::Deref for %(name)s {
    type Target = %(base)s;
    #[inline]
    fn deref(&self) -> &%(base)s {
        unsafe {
            ::std::mem::transmute(self)
        }
    }
}

// Enable coercing to base classes
impl<T: %(base)sCoerce> %(name)sCoerce for T {
    #[inline]
    fn coerce_from(v: &%(name)s) -> &Self {
        T::coerce_from(v)
    }
}
"""


struct_tmpl = """\
#[repr(C)]
pub struct %(name)s {
    vtable: *const %(name)sVTable,

    /// This field is a phantomdata to ensure that the VTable type and any
    /// struct containing it is not safe to send across threads, as XPCOM is
    /// generally not threadsafe.
    ///
    /// XPCOM interfaces in general are not safe to send across threads.
    __nosync: ::std::marker::PhantomData<::std::rc::Rc<u8>>,
}

unsafe impl XpCom for %(name)s {
    const IID: nsIID = nsID(0x%(m0)s, 0x%(m1)s, 0x%(m2)s,
                            [%(m3joined)s]);
}

unsafe impl RefCounted for %(name)s {
    #[inline]
    unsafe fn addref(&self) {
        self.AddRef();
    }
    #[inline]
    unsafe fn release(&self) {
        self.Release();
    }
}

// This trait just clutters up the documentation - hide it.
#[doc(hidden)]
pub trait %(name)sCoerce {
    /// Cheaply cast a value of this type from a `%(name)s`.
    fn coerce_from(v: &%(name)s) -> &Self;
}

impl %(name)sCoerce for %(name)s {
    #[inline]
    fn coerce_from(v: &%(name)s) -> &Self {
        v
    }
}

impl %(name)s {
    /// Cast this `%(name)s` to one of its base interfaces.
    #[inline]
    pub fn coerce<T: %(name)sCoerce>(&self) -> &T {
        T::coerce_from(self)
    }
}
"""


wrapper_tmpl = """\
impl %(name)s {
%(consts)s
%(methods)s
}

"""


def write_interface(iface, fd):
    if iface.namemap is None:
        raise Exception("Interface was not resolved.")

    # if we see a base class-less type other than nsISupports, we just need
    # to discard anything else about it other than its constants.
    if iface.base is None and iface.name != "nsISupports":
        assert len([m for m in iface.members
                    if type(m) == xpidl.Attribute or type(m) == xpidl.Method]) == 0
        return

    # Extract the UUID's information so that it can be written into the struct definition
    names = uuid_decoder.match(iface.attributes.uuid).groupdict()
    m3str = names['m3'] + names['m4']
    names['m3joined'] = ", ".join(["0x%s" % m3str[i:i+2] for i in xrange(0, 16, 2)])
    names['name'] = iface.name

    if printdoccomments:
        if iface.base is not None:
            fd.write("/// `interface %s : %s`\n///\n" %
                (iface.name, iface.base))
        else:
            fd.write("/// `interface %s`\n///\n" %
                iface.name)
    printComments(fd, iface.doccomments, '')
    fd.write(struct_tmpl % names)

    if iface.base is not None:
        fd.write(deref_tmpl % {
            'name': iface.name,
            'base': iface.base,
        })

    entries = ""
    for member in iface.members:
        if type(member) == xpidl.Attribute:
            entries += "/* %s */\n" % member.toIDL()
            entries += "%s,\n" % attrAsVTableEntry(iface, member, True)
            if not member.readonly:
                entries += "%s,\n" % attrAsVTableEntry(iface, member, False)
            entries += "\n"

        elif type(member) == xpidl.Method:
            entries += "/* %s */\n" % member.toIDL()
            entries += "%s,\n\n" % methodAsVTableEntry(iface, member)


    fd.write(vtable_tmpl % {
        'name': iface.name,
        'base': base_vtable_tmpl % iface.base if iface.base is not None else "",
        'entries': entries,
    })

    # Get all of the constants
    consts = ""
    for member in iface.members:
        if type(member) == xpidl.ConstMember:
            consts += doccomments(member.doccomments)
            consts += "pub const %s: i64 = %s;\n" % (member.name, member.getValue())

    methods = ""
    for member in iface.members:
        if type(member) == xpidl.Attribute:
            methods += doccomments(member.doccomments)
            methods += "/// `%s`\n" % member.toIDL()
            methods += "%s\n" % attrAsWrapper(iface, member, True)
            if not member.readonly:
                methods += doccomments(member.doccomments)
                methods += "/// `%s`\n" % member.toIDL()
                methods += "%s\n" % attrAsWrapper(iface, member, False)
            methods += "\n"

        elif type(member) == xpidl.Method:
            methods += doccomments(member.doccomments)
            methods += "/// `%s`\n" % member.toIDL()
            methods += "%s\n\n" % methodAsWrapper(iface, member)

    fd.write(wrapper_tmpl % {
        'name': iface.name,
        'consts': consts,
        'methods': methods
    })
