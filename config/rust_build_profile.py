from buildconfig import substs

_MOZ_OPTIMIZE = bool(substs.get('MOZ_OPTIMIZE', False))
_MOZ_DEBUG = bool(substs.get('MOZ_DEBUG', False))

RUST_BUILD_PROFILE = {
    'opt-level': 2 if _MOZ_OPTIMIZE else 0,
    'debug': True,
    'rpath': False,
    'lto': _MOZ_OPTIMIZE,
    'debug-assertions': _MOZ_DEBUG,
    'codegen-units': 1,
    'panic': 'abort',
}
