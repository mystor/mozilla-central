#!/usr/bin/env python
# rust.py - Generate rust bindings from IDL.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Print a Rust bindings file for the IDL files specified on the command line"""

import sys
import os.path
import re
import xpidl
import itertools
import glob

printdoccomments = False

if printdoccomments:
    def printComments(fd, clist, indent):
        for c in clist:
            fd.write("%s%s\n" % (indent, c))
else:
    def printComments(fd, clist, indent):
        pass


def firstCap(str):
    return str[0].upper() + str[1:]


# Attribute VTable Methods
def attributeNativeName(a, getter):
    binaryname = xpidl.rust_sanitize(a.binaryname if a.binaryname else a.name)
    return "%s%s" % ('get_' if getter else 'set_', binaryname)


def attributeVTableParamName(a):
    return "a" + firstCap(a.name)


def attributeVTableParamlist(iface, a, getter):
    l = ["this: *const " + iface.name]
    l += ["%s: %s" % (attributeVTableParamName(a),
                      a.realtype.rustTypeInfo('out' if getter else 'in')['vtable'])]
    if a.implicit_jscontext:
        raise xpidl.NonRustType()

    return ", ".join(l)


def attrAsVTableEntry(iface, m, getter):
    try:
        return "pub %s: unsafe extern \"%s\" fn (%s) -> nsresult" % (attributeNativeName(m, getter),
                                                                     getVTableABI(m),
                                                                     attributeVTableParamlist(iface, m, getter))
    except xpidl.NonRustType:
        return """\
/// Unable to call function as its signature contains a non-rust type
pub %s: *const ::libc::c_void""" % attributeNativeName(m, getter)


def getVTableABI(m):
    # XXX: This is really gross to choose the calling convention like this in
    # the code generator. It would be more ideal to emit code with #[cfg()]
    # directives or similar.
    if os.name == 'nt' and not m.nostdcall:
        return "stdcall"
    return "C"


# Method VTable generation functions
def methodNativeName(m):
    binaryname = m.binaryname is not None and m.binaryname or m.name
    return xpidl.rust_sanitize(binaryname)


def methodVTableReturnType(m):
    if m.notxpcom:
        return m.realtype.rustTypeInfo('in')['vtable'].strip()
    return "nsresult"


def methodVTableParamList(iface, m):
    l = ["this: *const %s" % iface.name]
    l += ["%s: %s" % (xpidl.rust_sanitize(p.name), p.rustTypeInfo()['vtable']) for p in m.params]

    if m.implicit_jscontext:
        # XXX: Not implemented yet
        raise xpidl.NonRustType()

    if m.optional_argc:
        # XXX: Not implemented yet
        raise xpidl.NonRustType()

    if not m.notxpcom and m.realtype.name != 'void':
        l.append("_retval: %s" % m.realtype.rustTypeInfo('out', '_retval')['vtable'])

    return ", ".join(l)


def methodAsVTableEntry(iface, m):
    try:
        return "pub %s: unsafe extern \"%s\" fn (%s) -> %s" % (methodNativeName(m),
                                                               getVTableABI(m),
                                                               methodVTableParamList(iface, m),
                                                               methodVTableReturnType(m))
    except xpidl.NonRustType:
        return """\
/// Unable to call function as its signature contains a non-rust type
pub %s: *const ::libc::c_void""" % methodNativeName(m)


nsresult_method_impl_tmpl = """\
#[inline]
pub unsafe fn %(name)s%(tmpl_args)s(&self, %(params)s) -> Result<%(ret_ty)s, nsresult> {
    %(setups)s
    match ((*self.vtable).%(name)s)(self as *const _, %(args)s) {
        NS_OK => {},
        e => return Err(e),
    }
    Ok(%(ret)s)
}"""

notxpcom_method_impl_tmpl = """\
#[inline]
pub unsafe fn %(name)s%(tmpl_args)s(&self, %(params)s) -> %(ret_ty)s {
    %(setups)s
    let _retval = ((*self.vtable).%(name)s)(self as *const _, %(args)s);
    %(ret)s
}"""


def methodAsWrapper(iface, m):
    try:
        if m.implicit_jscontext:
            # XXX: Not implemented yet
            raise xpidl.NonRustType()

        if m.optional_argc:
            # XXX: Not implemented yet
            raise xpidl.NonRustType()

        # Determine if we have a parameter with an iid_is parameter.
        # if we do, record which parameter it is, we will generate
        # a templated wrapper which will extract the IID from its
        # template parameter.
        iid_is = None
        for p in m.params:
            if p.iid_is and p.paramtype == 'out':
                iid_is = p.iid_is
                break
        # Confirm that our iid_is parameter is actually a 'in' param
        if iid_is:
            for p in m.params:
                if p.name == iid_is and p.paramtype != 'in':
                    iid_is = None
                    break

        # Collect Value parameters
        params = []
        setups = []
        ret_tys = []
        ret = []
        args = []

        for p in m.params:
            info = p.rustTypeInfo()

            if p.name == iid_is:
                # If we are the iid parameter, we need to be treated differently
                assert p.paramtype == 'in'
                args.append('&T::iid()')
                continue

            if p.iid_is and p.paramtype == 'out' and iid_is:
                assert p.realtype.name == 'nsQIResult'
                setups.append('let mut %s : GetterAddrefs<T> = GetterAddrefs::new();' % p.name)
                args.append('%s.ptr() as *mut *const T as *mut *const ::libc::c_void' % p.name)
                ret_tys.append('Option<RefPtr<T>>')
                ret.append('%s.refptr()' % p.name)
                continue

            args.append(info['arg'])
            if 'setup' in info:
                setups.append(info['setup'])
            if p.paramtype == 'in':
                params.append(xpidl.rust_sanitize(p.name) + ": " + info['param_ty'])
            else: # p.paramtype == 'out'
                ret_tys.append(info['ret_ty'])
                ret.append(info['ret'])

        # Get info about the return value if present
        if m.realtype.name != 'void':
            info = m.realtype.rustTypeInfo('out', '_retval')
            if m.notxpcom:
                # Return the value we got back from notxpcom
                ret.append(info.get('ret_notxpcom', info['ret']))
            else:
                args.append(info['arg'])
                setups.append(info['setup'])
                ret.append(info['ret'])

            ret_tys.append(info['ret_ty'])

        opts = {
            'name': methodNativeName(m),
            'params': ', '.join(params),
            'ret_ty': '(' + ', '.join(ret_tys) + ')' if len(ret_tys) != 1 else ret_tys[0],
            'setups': '\n'.join(setups),
            'args': ', '.join(args),
            'ret': '(' + ', '.join(ret) + ')' if len(ret) != 1 else ret[0],

            # Parameters which are only used when an nsQIResult is being returned
            'tmpl_args': '<T: XpCom>' if iid_is else '',
        }

        if m.notxpcom:
            return notxpcom_method_impl_tmpl % opts
        return nsresult_method_impl_tmpl % opts

    except xpidl.NonRustType:
        return ""


def attrAsWrapper(iface, m, getter):
    try:
        if m.implicit_jscontext:
            # XXX: Not implemented yet
            raise xpidl.NonRustType()

        # Collect Value parameters
        if getter:
            info = m.realtype.rustTypeInfo('out', '_retval')
        else:
            name = attributeVTableParamName(m)
            info = m.realtype.rustTypeInfo('in', name)
            info['params'] = name + ": " + info['param_ty']

        opts = {
            'name': attributeNativeName(m, getter),
            'params': info.get('params', ''),
            'ret_ty': info.get('ret_ty', '()'),
            'setups': info.get('setup', ''),
            'args': info.get('arg', ''),
            'ret': info.get('ret', '()'),
            'tmpl_args' : '',
        }

        return nsresult_method_impl_tmpl % opts
    except xpidl.NonRustType:
        return ""


header = """\
//
// DO NOT EDIT.  THIS FILE IS GENERATED FROM %(filename)s
//

"""


def idl_basename(f):
    """returns the base name of a file with the last extension stripped"""
    return os.path.basename(f).rpartition('.')[0]


def print_rust_bindings(idl, fd, filename):
    class FDWrapper(object):
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

    fd = FDWrapper(fd)

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
            printComments(fd, p.doccomments, '')
            try:
                # We have to skip the typedef of bool to bool (it doesn't make any sense anyways)
                if p.name == "bool":
                    continue
                fd.write("pub type %s = %s;\n\n" % (p.name, p.realtype.rustTypeInfo('in')['vtable']))
            except xpidl.NonRustType:
                fd.write("/* ignored typedef for non rust type %s */\n\n" % p.name)


uuid_decoder = re.compile(r"""(?P<m0>[a-f0-9]{8})-
                              (?P<m1>[a-f0-9]{4})-
                              (?P<m2>[a-f0-9]{4})-
                              (?P<m3>[a-f0-9]{4})-
                              (?P<m4>[a-f0-9]{12})$""", re.X)


base_vtable_tmpl = """
pub __base: %sVTable,

"""


vtable_tmpl = """\
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
}

unsafe impl XpCom for %(name)s {
    #[inline]
    fn iid() -> nsIID {
        nsID(0x%(m0)s, 0x%(m1)s, 0x%(m2)s,
             [%(m3joined)s])
    }
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

// Enable coercing to ourselves
pub trait %(name)sCoerce {
    fn coerce_from(v: &%(name)s) -> &Self;
}

impl %(name)sCoerce for %(name)s {
    #[inline]
    fn coerce_from(v: &%(name)s) -> &Self {
        v
    }
}

impl %(name)s {
    #[inline]
    pub fn coerce<T: %(name)sCoerce>(&self) -> &T {
        T::coerce_from(self)
    }
}
"""


wrapper_tmpl = """\
impl %(name)s {
%(methods)s}

"""


constants_tmpl = """\
pub mod %(name)s_consts {
%(consts)s}

"""


def write_interface(iface, fd):
    if iface.namemap is None:
        raise Exception("Interface was not resolved.")

    # Get all of the constants
    consts = ""
    for member in iface.members:
        if type(member) == xpidl.ConstMember:
            consts += "pub const %s: i64 = %s;\n" % (member.name, member.getValue())

    if len(consts) > 0:
        fd.write(constants_tmpl % {
            'name': iface.name,
            'consts': consts,
        })

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
    });

    methods = ""
    for member in iface.members:
        if type(member) == xpidl.Attribute:
            methods += "/* %s */\n" % member.toIDL()
            methods += "%s\n" % attrAsWrapper(iface, member, True)
            if not member.readonly:
                pass
                methods += "%s\n" % attrAsWrapper(iface, member, False)
            methods += "\n"

        elif type(member) == xpidl.Method:
            methods += "/* %s */\n" % member.toIDL()
            methods += "%s\n\n" % methodAsWrapper(iface, member)

    fd.write(wrapper_tmpl % {
        'name': iface.name,
        'methods': methods
    })


def main(outputfile):
    cachedir = '.'
    if not os.path.isdir(cachedir):
        os.mkdir(cachedir)
    sys.path.append(cachedir)

    # Delete the lex/yacc files.  Ply is too stupid to regenerate them
    # properly
    for fileglobs in [os.path.join(cachedir, f) for f in ["xpidllex.py*", "xpidlyacc.py*"]]:
        for filename in glob.glob(fileglobs):
            os.remove(filename)

    # Instantiate the parser.
    p = xpidl.IDLParser(outputdir=cachedir)


if __name__ == '__main__':
    main()
