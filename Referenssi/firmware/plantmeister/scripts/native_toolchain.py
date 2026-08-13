import os

from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()

# Only configure toolchain for native platform on Windows.
# On Linux/macOS, system default GCC works fine - do nothing.
if env.get("PIOPLATFORM") != "native" or os.name != "nt":
    if env.get("PIOPLATFORM") == "native" and os.name != "nt":
        print("[native_toolchain] Non-Windows host, keeping default toolchain")
else:
    # Windows: prefer MSYS2 toolchains, in order of preference.
    # ucrt64 ships with gcc 15+ and works with PlatformIO's native build.
    # mingw64 with clang is kept as a fallback for legacy installs.
    candidates = [
        ("ucrt64", r"C:\msys64\ucrt64\bin", "gcc.exe", "g++.exe", "ar.exe", "ranlib.exe"),
        ("mingw64-clang", r"C:\msys64\mingw64\bin", "clang.exe", "clang++.exe", "llvm-ar.exe", "llvm-ranlib.exe"),
        ("mingw64", r"C:\msys64\mingw64\bin", "gcc.exe", "g++.exe", "ar.exe", "ranlib.exe"),
    ]

    chosen = None
    for label, bin_dir, cc_name, cxx_name, ar_name, ranlib_name in candidates:
        cc = os.path.join(bin_dir, cc_name)
        cxx = os.path.join(bin_dir, cxx_name)
        if os.path.exists(cc) and os.path.exists(cxx):
            chosen = (label, bin_dir, cc, cxx, ar_name, ranlib_name)
            break

    if chosen is None:
        print("[native_toolchain] no MSYS2 toolchain found, keeping default")
    else:
        label, bin_dir, cc, cxx, ar_name, ranlib_name = chosen
        env.Replace(CC=cc, CXX=cxx)

        ar = os.path.join(bin_dir, ar_name)
        ranlib = os.path.join(bin_dir, ranlib_name)
        if os.path.exists(ar):
            env.Replace(AR=ar)
        if os.path.exists(ranlib):
            env.Replace(RANLIB=ranlib)

        # The MSYS2 toolchain depends on its own runtime DLLs (libstdc++-6.dll
        # etc.) being on PATH; without this, executables built here exit with
        # status 1 and no stderr, which is incredibly hard to debug.
        # Patch both the SCons env PATH (for subprocesses SCons spawns) AND
        # the parent os.environ (for subprocesses spawned outside SCons, e.g.
        # the pio test runner's own subprocess invocation of the test binary).
        for ev in (env["ENV"], os.environ):
            path = ev.get("PATH", "")
            if bin_dir.lower() not in path.lower():
                ev["PATH"] = bin_dir + os.pathsep + path

        print("[native_toolchain] using", label, "from", bin_dir)
