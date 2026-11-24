"""Opt-in native C++ core (`native/cpp`, extension module `_pkc_native`).

`PKC_NATIVE_CORE` names the directory holding a built module (`tools.native_cpp build`
writes `build/native/cpp`). Unset, every ported unit runs its Python implementation,
which stays the parity oracle.
"""
import os
import sys


def load(directory):
    """Import `_pkc_native` from `directory`; one build per process, since the module name is fixed."""
    directory = str(directory)
    if directory not in sys.path:
        sys.path.insert(0, directory)
    import _pkc_native
    return _pkc_native


def from_environment():
    """The module `PKC_NATIVE_CORE` selects, or None for the Python implementations."""
    directory = os.environ.get('PKC_NATIVE_CORE')
    return load(directory) if directory else None
