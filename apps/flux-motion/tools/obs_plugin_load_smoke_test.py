"""Check plugin imports against a real OBS installation, independent of the SDK.

Usage: python obs_plugin_load_smoke_test.py <plugin.dll> <OBS bin/64bit>
This checks Windows DLL loading and module exports; it does not start OBS.
"""

import ctypes
import os
from pathlib import Path
import sys


def main():
    if sys.platform != "win32":
        return 77
    plugin = Path(sys.argv[1]).resolve()
    obs_bin = Path(sys.argv[2]).resolve()
    if not (obs_bin / "obs.dll").is_file():
        return 77
    # Load host libraries explicitly so SDK copies elsewhere on PATH cannot
    # hide imports removed in the installed version of OBS.
    with os.add_dll_directory(str(obs_bin)), os.add_dll_directory(str(plugin.parent)):
        obs = ctypes.CDLL(str(obs_bin / "obs.dll"))
        frontend = ctypes.CDLL(str(obs_bin / "obs-frontend-api.dll"))
        module = ctypes.CDLL(str(plugin))
        obs.obs_get_version_string.restype = ctypes.c_char_p
        module.obs_module_ver.restype = ctypes.c_uint32
        assert module.obs_module_ver(), "Plugin has no API version"
        for export in ("obs_module_load", "obs_module_unload", "obs_module_set_pointer"):
            getattr(module, export)
        print(f"Loaded {plugin.name} with OBS {obs.obs_get_version_string().decode()}")
        # Keep the frontend library alive until module validation completes.
        assert frontend
    return 0


if __name__ == "__main__":
    sys.exit(main())
