"""pytest configuration for cdsl Python bindings tests."""

import os
import sys

# Ensure the package is importable from the project root
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

# Point to the build directory for the shared library
_build_lib = os.path.join(
    os.path.dirname(__file__), "..", "..", "build",
)
if os.path.isdir(_build_lib):
    os.environ.setdefault("CDSL_LIB", os.path.join(
        _build_lib,
        "libcdsl.so" if sys.platform != "darwin" else "libcdsl.dylib",
    ))
