import os
import subprocess
import re
import sys
from collections import OrderedDict, defaultdict

import buildconfig
import mozpack.path as mozpath


# Pull the system includes from the environment.
# XXX: Should we also check /imsvc arguments?
winsdkdirs = [mozpath.normpath(i) for i in os.environ['INCLUDE'].split(os.pathsep)]
class FileInfo(object):
    def __init__(self, path):
        self.path = mozpath.normpath(path)
        self.whitelisted = any(self.path.startswith(p) for p in winsdkdirs)


# This isn't perfect, but it's good enough.
ident_seg = r"\b[A-Za-z_][A-Za-z0-9_]*\b"
uppercamel_seg = r"\b[A-Z]+[a-z][A-Za-z0-9]*\b"
lit_seg = r"(\"([^\"]|\\\")*\"|(0x)?[0-9]+L?)"
params_seg = r"^\((?P<params>( *" + ident_seg + r" *,?)*) *\)"

uppercamel_re = re.compile(uppercamel_seg)
funclike_re = re.compile(params_seg + r"(?P<rest>.*)$")
idents_re = re.compile(ident_seg)

# Matches a functionlike macro which just shuffles its arguments and
# (optionally) adds constant arguments. This only matches when we
# forward to an UpperCamelCaseFunction.
simplefunc_re = re.compile(
    params_seg + r" *(?P<target>" + uppercamel_seg + r") *" +
    r"\((?P<args>( *(" + ident_seg + r"|" + lit_seg + r") *,?)*) *\)"
)

class DefineInfo(object):
    def __init__(self, name, rest, file):
        self.name = name
        self.rest = rest
        self.file = file

        # A name is "deceptive" if it is UpperCamelCase, as that identifier
        # style is used by both Gecko and Windows for functions.
        # Names with underscores or all lowercase letters are not #undef-ed.
        self.deceptive = uppercamel_re.match(name)

    def newdecl(self, defines):
        assert self.deceptive, "Don't call newdecl() on a non-deceptive DefineInfo!"

        # If the definition contains a semicolon, it could contain
        # statements, which we can't handle.
        if ';' in self.rest:
            # As a fallback, re-define the macro with a WINDOWS_ prefix.
            return "#define WINDOWS_%s%s" % (self.name, self.rest)

        sf_match = simplefunc_re.match(self.rest)
        if sf_match:
            return self.newdecl_simplefn(sf_match)

        fl_match = funclike_re.match(self.rest)
        if fl_match:
            return self.newdecl_funclike(fl_match)

        return self.newdecl_simple(defines)

    def newdecl_simple(self, defines):
        # We assume simple macros are names of callables (functions or intrinsics).
        # They could be variables, however that goes against the naming style
        # used within windows.h, so it is unlikely.
        #
        # This wrapper allows us to avoid having to write types for any of the
        # arguments or the return value, while also getting the desired
        # reference semantics.
        target = self.rest.strip()

        # Check if this is an ascii/unicode forwarding method, and handle those.
        # We need to handle this case specially, as some compilation units will
        # set UNICODE differently than us, and expect a different alias.
        if target.startswith(self.name) and target[-1] in 'AW':
            return "const auto %s = UNICODE_SUFFIXED(%s);" % (self.name, target[:-1])

        # If the target is a function, we can just forward it with "const auto".
        # We assume that UpperCamelCase names name functions.
        if uppercamel_re.match(target):
            return "const auto %s = %s;" % (self.name, target)

        # It might be an intrinsic, so we need to wrap it in a generic lambda.
        r  = "constexpr auto %s = [] (auto&&... args) {\n" % self.name
        r += "  return %s(mozilla::Forward<decltype(args)>(args)...);\n" % target
        r += "};"
        return r

    def newdecl_funclike(self, match):
        # If the define itself is functionlike, we rewrite it as a generic
        # const lambda.

        body = match.group('rest').strip()
        params_s = match.group('params').strip()

        params = [p.strip() for p in params_s.split(',')] \
            if len(params_s) > 0 else []

        # Rewrite argument references to add std::forward<decltype(x)>(x).
        for p in params:
            fwd = "mozilla::Forward<decltype(%s)>(%s)" % (p, p)
            body = re.sub(r"\b%s\b" % p, fwd, body)

        r = "constexpr auto %s = [] (%s) {\n" % \
            (self.name, ', '.join(["auto&& %s" % p for p in params]))
        r += "  return %s;\n" % body
        r += "};"
        return r

    def newdecl_simplefn(self, match):
        # If we're looking at a simple function, we can actually specify the
        # types of our parameters, which can help avoid breaking some callers.
        # With template wrapping implicit conversion of `0` to pointer types
        # is disabled, while this allows us to preserve it.

        params_s = match.group('params').strip()
        args_s = match.group('args').strip()
        target = match.group('target')

        params = [p.strip() for p in params_s.split(',')] \
            if len(params_s) > 0 else []
        args = [a.strip() for a in args_s.split(',')] \
            if len(args_s) > 0 else []

        pdecls = [
            "ARG(%s, %s) %s" % (target, args.index(p), p) if p in args
                else "auto %s" % p
            for p in params
        ]

        r  = "constexpr auto %s = [] (%s) {\n" % (self.name, ', '.join(pdecls))
        r += "  return %s(%s);\n" % (target, args_s)
        r += "};"
        return r


# Matches both clang/gcc and msvc style line directives outputted by
# the preprocessor.
#
# clang-cl/gcc style: `# LINE_NUM:int FILENAME:str FLAGS:int*`
# msvc style:         `#line LINE_NUM:int FILENAME:str`
line_re = re.compile(r"^#(line)? [0-9]+ \"(?P<file>([^\"]|\\\")+)\"")

# #define and #undef directives.
define_re = re.compile(r"^#define +(?P<name>[A-Za-z_0-9]+)(?P<rest>.*)")
undef_re = re.compile(r"^#undef +(?P<name>[A-Za-z_0-9]+)")


def effective_lines(s):
    # Get the "effective" lines in s, considering \\ characters before
    # newlines as line continuations.
    # Whitespace is stripped from the beginning and end of each line.
    eline = ""
    for line in s.splitlines():
        eline += line
        if len(eline) > 0 and eline[-1] == '\\':
            eline = eline[:-1] + " "
        else:
            yield eline.strip()
            eline = ""


def gather_defines(preprocessed):
    # Collect the set of defines created by the preprocessed file.
    defines = OrderedDict()
    file = None

    for line in effective_lines(preprocessed):
        # Pull file info out of #line directives.
        lmatch = line_re.match(line)
        if lmatch is not None:
            file = FileInfo(lmatch.group('file'))

        # Track what defines windows.h declares, and where.
        dmatch = define_re.match(line)
        if dmatch is not None:
            di = DefineInfo(dmatch.group('name'), dmatch.group('rest'), file)
            defines[di.name] = di

        umatch = undef_re.match(line)
        if umatch is not None:
            if umatch.group('name') in defines:
                del defines[umatch.group('name')]

    return defines


minwin_in = mozpath.join(buildconfig.topsrcdir, 'xpcom/base/MinWin_in.cpp')


def write_winmin(fd, defines):
    out = "\n"
    for define in defines.values():
        if not (define.deceptive and define.file.whitelisted):
            continue
        out += "// NOTE: from \"%s\"\n" % define.file.path
        out += "#ifdef %s\n" % define.name
        out += "#undef %s\n" % define.name
        out += "%s\n" % define.newdecl(defines)
        out += "#endif // defined(%s)\n\n" % define.name

    # Create the final file, by replacing the subst point in MinWin_in.cpp
    with open(minwin_in, "r") as f:
        tmpl = f.read()

    fd.write("// THIS FILE WAS GENERATED BY MinWin.py - DO NOT EDIT\n\n")
    fd.write(tmpl.replace("_MINWIN_WINVER", buildconfig.defines['WINVER'])
                 .replace("_MINWIN_H_SUBST_POINT", out))


def gen_minwin(output):
    # Read the compiler command line from the build config.
    cxx = buildconfig.substs['CXX']
    cmd = [cxx] + buildconfig.substs['OS_CXXFLAGS'] + \
            buildconfig.substs['OS_COMPILE_CXXFLAGS']

    # Attempt to run "$CXX --version" to see if we're working with mingw.
    try:
        ver = subprocess.check_output([cxx, '-v'], stderr=subprocess.STDOUT)
        is_mingw = 'gcc' in ver
    except:
        is_mingw = False

    # NOTE: clang-cl also supports -dD, but msvc does not.
    emit_defines_flag = '-dD' if is_mingw else '-d1PP'

    # Add the arguments required to dump all of the #define and #undef commands
    # which are run.
    cmd += [
        '-E', emit_defines_flag,
        '-D', 'MINWIN_PREPROCESSING',
        '-D', '_MINWIN_WINVER=%s' % buildconfig.defines['WINVER'],
        minwin_in,
    ]
    stdout = subprocess.check_output(cmd)

    # Process the source, and write out the computed #undefs etc.
    found = gather_defines(stdout)
    write_winmin(output, found)
    return set([minwin_in])
