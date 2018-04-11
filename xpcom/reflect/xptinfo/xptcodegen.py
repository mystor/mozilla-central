#!/usr/bin/env python
# jsonlink.py - Merge JSON typelib files into a .cpp file
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# NOTE: Once shims are removed, this code can be cleaned up, removing all
# reference to them.

import json
import phf
import time
from collections import OrderedDict

# We fix the number of entries in our intermediate table used by the perfect
# hashes to 256. This number is constant in xptinfo, allowing the compiler to
# generate a more efficient modulo due to it being a power of 2.
PHFSIZE = 256


# Helper functions for dealing with IIDs
def split_at_idxs(s, lengths):
    idx = 0
    for length in lengths:
        yield s[idx:idx+length]
        idx += length
    assert idx == len(s)

def split_iid(iid): # Get the individual components out of an IID string.
    iid = iid.replace('-', '') # Strip any '-' delimiters
    return tuple(split_at_idxs(iid, (8, 4, 4, 2, 2, 2, 2, 2, 2, 2, 2)))

def iid_bytes(iid): # Get the byte representation of the IID for hashing.
    bs = bytearray()
    # We store the bytes in little-endian (XXX: Big endian support?)
    for num in split_iid(iid):
        b = bytearray.fromhex(num)
        b.reverse()
        bs += b
    return bs


# Convert the passed in argument to a string, and indent it by 1 level.
def indented(s):
    if isinstance(s, bool):
        s = int(s) # Translate bools as integers
    return str(s).replace('\n', '\n  ')


class Instance(object):
    # Represents a single instance of an object constructed with Constructor.
    def __init__(self, ctor, comment, fields):
        self.ctor = ctor
        self.comment = comment
        self.fields = fields
        assert len(self.fields) == len(self.ctor.fields)

    def __str__(self):
        r = "XPTConstruct::Mk_%s( // %s" % (self.ctor.name, self.comment)
        r += indented(','.join("\n/* %s */ %s" % (k, indented(self.fields[k]))
                               for k in self.ctor.fields))
        r += ")"
        return r


class Constructor(object):
    # Helper object for defining and using constexpr methods create xpt types.
    # This is used for a few reasons:
    #  1. Make the data members private.
    #  2. Make the order of the data members irrelevant to this file.
    #  3. Ensure that xptcodegen.py and xptinfo.h remain in sync.
    def __init__(self, name, fields):
        self.name = name
        self.fields = list(sorted(fields))

    def construct(self, comment, fields):
        return Instance(self, comment, fields)

    def decl(self):
        pnames = ["a" + field[1:].replace('.', '_') for field in self.fields]

        params = indented(',\n'.join(
            "MTYPE(%s, %s) %s" % (self.name, field, pname)
            for field, pname in zip(self.fields, pnames)))
        fields = indented('\n'.join(
            "obj.%s = %s;" % (field, pname)
            for field, pname in zip(self.fields, pnames)))

        return """
static constexpr %(name)s Mk_%(name)s(
  %(params)s)
{
  %(name)s obj;
  %(fields)s
  return obj;
}""" % { 'params': params, 'fields': fields, 'name': self.name }


class Flags(object):
    def __init__(self, d):
        self.flags = d['flags']

    def __getattr__(self, name):
        if name[-1] == '_': # XXX: Strip optional trailing _ for keywords (in)
            name = name[:-1]
        return name in self.flags


def link_to_cpp(interfaces, fd):
    iid_phf = phf.PHF(PHFSIZE, [ # Perfect Hash from IID into the ifaces array.
        (iid_bytes(iface['uuid']), iface)
        for iface in interfaces
    ])
    name_phf = phf.PHF(PHFSIZE, [ # Perfect Hash from name to index in the ifaces array.
        (bytearray(iface['name'], 'ascii'), idx)
        for idx, iface in enumerate(iid_phf.values)
    ])

    def interface_idx(name):
        if name is not None:
            idx = name_phf.lookup(bytearray(name, 'ascii'))
            if iid_phf.values[idx]['name'] == name:
                return idx + 1 # One-based, so we can use 0 as a sentinel.
        return 0

    # NOTE: State used while linking. This is done with closures rather than a
    # class due to how this file's code evolved.
    includes = []
    types = []
    ifaces = []
    params = []
    methods = []
    consts = []
    prophooks = []
    domobjects = OrderedDict()
    strings = OrderedDict()
    constructors = {}

    def struct(ty, comment, fields):
        # Make sure we have generated a constructor for this type
        if ty not in constructors:
            constructors[ty] = Constructor(ty, fields.keys())
        return constructors[ty].construct(comment, fields) # Construct it

    def lower_uuid(uuid):
        return "{0x%s, 0x%s, 0x%s, {0x%s, 0x%s, 0x%s, 0x%s, 0x%s, 0x%s, 0x%s, 0x%s}}" % split_iid(uuid)

    def lower_domobject(do):
        assert do['tag'] == 'TD_DOMOBJECT'
        if do['name'] in domobjects:
            return domobjects[do['name']]['idx']

        # Make sure we include the native object in question's header.
        includes.append(do['headerFile'])

        idx = len(domobjects)
        domobjects[do['name']] = {
            'idx': idx,
            'instance': struct(
                "nsXPTDOMObjectInfo",
                "%d = %s" % (idx, do['name']),
                {
                    # These methods are defined at the top of the generated file.
                    'mUnwrap': "UnwrapDOMObject<dom::prototypes::id::%s, %s>" %
                        (do['name'], do['native']),
                    'mWrap': "WrapDOMObject<%s>" % do['native'],
                    'mCleanup': "CleanupDOMObject<%s>" % do['native'],
                }
            ),
        }
        return idx

    def lower_string(s):
        if s in strings:
            # We've already seen this string.
            return strings[s]
        elif len(strings):
            # Get the last string we inserted (should be O(1) on OrderedDict).
            last_s = next(reversed(strings))
            strings[s] = strings[last_s] + len(last_s) + 1
        else:
            strings[s] = 0
        return strings[s]

    def describe_type(type): # Create the type's documentation comment.
        tag = type['tag'][3:].lower()
        if tag == 'array':
            return '%s[size_is=%d]' % (
                describe_type(type['element']), type['size_is'])
        elif tag == 'interface_type':
            return type['name']
        elif tag == 'interface_is_type':
            return 'iid_is(%d)' % type['iid_is']
        elif tag.endswith('_size_is'):
            return '%s(size_is=%d)' % (tag, type['size_is'])
        return tag

    def lower_type(type):
        tag = type['tag']
        d1 = d2 = 0

        if tag == 'TD_ARRAY':
            d1 = type['size_is']
            # Add the type to the extra types list
            elty = lower_type(type['element'])
            try:
                d2 = types.index(elty)
            except:
                d2 = len(types)
                types.append(elty)

        elif tag == 'TD_INTERFACE_TYPE':
            idx = interface_idx(type['name'])
            d1 = idx >> 8
            d2 = idx & 0xff

        elif tag == 'TD_INTERFACE_IS_TYPE':
            d1 = type['iid_is']

        elif tag == 'TD_DOMOBJECT':
            idx = lower_domobject(type)
            d1 = idx >> 8
            d2 = idx & 0xff

        elif tag.endswith('_SIZE_IS'):
            d1 = type['size_is']

        assert d1 < 256 and d2 < 256, "Data values too large"
        return struct(
            "nsXPTType",
            describe_type(type),
            {
                'mTag': tag,
                'mData1': d1,
                'mData2': d2,
            }
        )

    def lower_param(param, paramname):
        flags = Flags(param)

        params.append(struct(
            "nsXPTParamInfo",
            "%d = %s" % (len(params), paramname),
            {
                'mType': lower_type(param['type']),
                'mType.mInParam': flags.in_,
                'mType.mOutParam': flags.out,
                'mType.mOptionalParam': flags.optional,
            },
        ))


    def lower_method(method, ifacename):
        flags = Flags(method)

        hideparams = flags.notxpcom or flags.hidden
        methodname = "%s::%s" % (ifacename, method['name'])
        methods.append(struct(
            "nsXPTMethodInfo",
            "%d = %s" % (len(methods), methodname),
            {
                'mName': lower_string(method['name']),

                # If our method is hidden, we can save some memory by not
                # generating parameter info about it.
                'mParams': 0 if hideparams else len(params),
                'mNumParams': 0 if hideparams else len(method['params']),

                # Flags
                'mGetter': flags.getter,
                'mSetter': flags.setter,
                'mNotXPCOM': flags.notxpcom,
                'mHidden': flags.hidden,
                'mOptArgc': flags.optargc,
                'mContext': flags.jscontext,
                'mHasRetval': flags.hasretval,
            }
        ))

        if not hideparams:
            for idx, param in enumerate(method['params']):
                lower_param(param, "%s[%d]" % (methodname, idx))

    def lower_const(const, ifacename):
        assert const['type']['tag'] in \
            ['TD_INT16', 'TD_INT32', 'TD_UINT16', 'TD_UINT32']
        is_signed = const['type']['tag'] in ['TD_INT16', 'TD_INT32']

        # Constants are always either signed or unsigned 16 or 32 bit integers,
        # which we will only need to convert to JS values. To save on space,
        # don't bother storing the type, and instead just store a 32-bit
        # unsigned integer, and stash whether to interpret it as signed.
        consts.append(struct(
            "ConstInfo",
            "%d = %s::%s" % (len(consts), ifacename, const['name']),
            {
                'mName': lower_string(const['name']),
                'mSigned': is_signed,
                'mValue': "(uint32_t)%d" % const['value'],
            }
        ))

    def lower_prop_hooks(iface): # XXX: Used by xpt shims
        assert iface['shim'] is not None

        # Add an include for the Binding file for the shim.
        includes.append("mozilla/dom/%sBinding.h" %
            (iface['shimfile'] or iface['shim']))

        # Add the property hook reference to the sPropHooks table.
        prophooks.append(
            "mozilla::dom::%sBinding::sNativePropertyHooks, // %d = %s(%s)" % \
                (iface['shim'], len(prophooks), iface['name'], iface['shim']))

    def collect_base_info(iface):
        methods = 0
        consts = 0
        builtinclass = False
        while iface is not None:
            methods += len(iface['methods'])
            consts += len(iface['consts'])
            # We're builtinclass if any of our bases are.
            builtinclass = builtinclass or Flags(iface).builtinclass
            idx = interface_idx(iface['parent'])
            if idx == 0:
                break
            iface = iid_phf.values[idx - 1]

        return methods, consts, builtinclass

    def lower_iface(iface):
        flags = Flags(iface)

        isshim = iface['shim'] is not None
        assert isshim or flags.scriptable

        method_off = len(methods)
        consts_off = len(consts)
        method_cnt = const_cnt = 0
        if isshim:
            # If we are looking at a shim, don't lower any methods or constants,
            # as they will be pulled from the WebIDL binding instead. Instead,
            # we use the constants offset field to store the index into the prop
            # hooks table.
            consts_off = len(prophooks)
            builtinclass = True  # All shims are builtinclass
        else:
            method_cnt, const_cnt, builtinclass = collect_base_info(iface)

        ifaces.append(struct(
            "nsXPTInterfaceInfo",
            "%d = %s" % (len(ifaces), iface['name']),
            {
                'mIID': lower_uuid(iface['uuid']),
                'mName': lower_string(iface['name']),
                'mParent': interface_idx(iface['parent']),

                'mMethods': method_off,
                'mNumMethods': method_cnt,
                'mConsts': consts_off,
                'mNumConsts': const_cnt,

                # Flags
                'mIsShim': isshim,
                'mBuiltinClass': builtinclass,
                'mMainProcessScriptableOnly': flags.main_process_only,
                'mFunction': flags.function,
            }
        ))

        if isshim:
            lower_prop_hooks(iface)
            return

        # Lower the methods and constants used by this interface
        for method in iface['methods']:
            lower_method(method, iface['name'])
        for const in iface['consts']:
            lower_const(const, iface['name'])

    # Lower interfaces in the order of the IID phf's values lookup.
    for iface in iid_phf.values:
        lower_iface(iface)

    # Write out the final output file
    fd.write("/* THIS FILE WAS GENERATED BY xptcodegen.py - DO NOT EDIT */\n\n")

    # Include any bindings files which we need to include due to XPT shims.
    for include in includes:
        fd.write('#include "%s"\n' % include)

    # Write our out header
    fd.write("""
#include "xptinfo.h"
#include "mozilla/TypeTraits.h"
#include "mozilla/dom/BindingUtils.h"

using namespace mozilla; // For mozilla::ArrayLength and mozilla::DeclVal.

// This macro resolves to the type of the non-static data member `m` of `T`.
// It's used by generated data type constructors.
#define MTYPE(T, m) decltype(DeclVal<T>().m)

// These template methods are specialized to be used in the sDOMObjects table.
template<dom::prototypes::ID PrototypeID, typename T>
static nsresult UnwrapDOMObject(JS::HandleValue aHandle, void** aObj)
{
  RefPtr<T> p;
  nsresult rv = dom::UnwrapObject<PrototypeID, T>(aHandle, p);
  p.forget(aObj);
  return rv;
}

template<typename T>
static bool WrapDOMObject(JSContext* aCx, void* aObj, JS::MutableHandleValue aHandle)
{
  return dom::GetOrCreateDOMReflector(aCx, reinterpret_cast<T*>(aObj), aHandle);
}

template<typename T>
static void CleanupDOMObject(void* aObj)
{
  RefPtr<T> p = already_AddRefed<T>(reinterpret_cast<T*>(aObj));
}

namespace xpt {
namespace detail {

""")

    # xptcodegen takes a slightly odd approach to generating values. We generate
    # constructor methods for each of the structs we generate, and add them as
    # static methods on the XPTConstruct struct.
    #
    # This is done for the following reasons:
    #  1. Writing constructors this way means that xptcodegen.py is not
    #     dependent on the order of fields in xptinfo.h
    #  2. As a member of the XPTConstruct struct, we are `friend class` of the
    #     data structures, allowing us to initialize private fields.
    #  3. Stating the name of fields in this python code makes it more
    #     self-documenting.
    fd.write("struct XPTConstruct {")
    for constructor in constructors.values():
        fd.write(indented(constructor.decl()))
    fd.write("\n};\n\n")

    # Static data arrays
    def array(ty, name, els):
        fd.write("const %s %s[] = {%s\n};\n\n" %
            (ty, name, ','.join(indented('\n' + str(e)) for e in els)))
    array("nsXPTInterfaceInfo", "sInterfaces", ifaces)
    array("nsXPTType", "sTypes", types)
    array("nsXPTParamInfo", "sParams", params)
    array("nsXPTMethodInfo", "sMethods", methods)
    array("nsXPTDOMObjectInfo", "sDOMObjects",
          (do['instance'] for do in domobjects.values()))
    array("ConstInfo", "sConsts", consts)
    array("mozilla::dom::NativePropertyHooks*", "sPropHooks", prophooks)

    # The strings array. We write out individual characters to avoid msvc restrictions.
    fd.write("const char sStrings[] = {\n")
    for s, off in strings.iteritems():
        fd.write("  // %d = %s\n  '%s','\\0',\n" % (off, s, "','".join(s)))
    fd.write("};\n\n")

    # Record the information required for perfect hashing.
    def phfarr(name, ty, it):
        fd.write("const %s %s[] = {" % (ty, name))
        for idx, v in enumerate(it):
            if idx % 8 == 0:
                fd.write('\n ')
            fd.write(" 0x%08x," % v)
        fd.write("\n};\n\n")
    phfarr("sPHF_IIDs", "uint32_t", iid_phf.inter)
    phfarr("sPHF_Names", "uint32_t", name_phf.inter)
    phfarr("sPHF_NamesIdxs", "uint16_t", name_phf.values)

    # The footer contains some checks re: the size of the generated arrays.
    fd.write("""\
const uint16_t sInterfacesSize = ArrayLength(sInterfaces);
static_assert(sInterfacesSize == ArrayLength(sPHF_NamesIdxs),
              "sPHF_NamesIdxs must have same size as sInterfaces");

static_assert(kPHFSize == ArrayLength(sPHF_Names),
              "sPHF_IIDs must have size kPHFSize");
static_assert(kPHFSize == ArrayLength(sPHF_IIDs),
              "sPHF_Names must have size kPHFSize");

} // namespace detail
} // namespace xpt
""")


def link_and_write(files, outfile):
    interfaces = []
    for file in files:
        with open(file, 'r') as fd:
            interfaces += json.load(fd)

    link_to_cpp(interfaces, outfile)


if __name__ == '__main__':
    from argparse import ArgumentParser
    import sys

    parser = ArgumentParser()
    parser.add_argument('outfile', help='Output C++ file to generate')
    parser.add_argument('xpts', nargs='*', help='source xpt files')

    args = parser.parse_args(sys.argv[1:])
    with open(args.outfile, 'w') as fd:
        link_and_write(args.xpts, fd)
