"""Versioned LLVM guest compiler; Python orchestrates, LLVM owns IR semantics."""

import functools
import hashlib
import json
import shlex
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "tmp/pc/guest-compiler"
CONFIG = Path(__file__).with_name("macos_toolchain.json")
LINUX_CONFIG = Path(__file__).with_name("linux_toolchain.json")


class TranslationError(ValueError):
    pass


# Whether a missing LLVM 21 may be fetched (linux_llvm, the macOS archive).
# A game build and `python tools/pc/llvm_guest.py` may; build_mod.py sets it
# false: building a mod never downloads anything (it says what to get).
FETCH = True


def no_fetch(folder):
    raise RuntimeError(
        f"LLVM {json.loads(CONFIG.read_text())['version']} is not in {folder}; `python tools/pc/llvm_guest.py` "
        "fetches it (or set MEMORIES_GUEST_LLVM to a build of it)")


def host_llvm():
    """A pinned LLVM built on this host (MEMORIES_GUEST_LLVM: a build or install
    folder with bin/llvm-config), for hosts without an official archive
    (Windows), or to use another copy of the same version."""
    folder = os.environ.get("MEMORIES_GUEST_LLVM")
    return Path(folder) if folder else None


# What the pass (host_compiler) is built from in the official Linux x86-64
# archive (linux_toolchain.json): llvm-config, the headers and the static
# libraries, about 440 MB of its 2 GB; the rest (clang, lld, the tools) is
# never unpacked.
LINUX_PARTS = ("bin/llvm-config", "include/llvm/", "include/llvm-c/", "lib/libLLVM")


def linux_llvm(version):
    """The official LLVM release for Linux x86-64 (the release workflow's
    Android job), fetched and checked by its SHA-256 the first time, of which
    only LINUX_PARTS is unpacked, into tmp/pc/llvm-linux."""
    config = json.loads(LINUX_CONFIG.read_text())
    if config["version"] != version:
        raise RuntimeError(f"{LINUX_CONFIG.name} pins LLVM {config['version']}, {CONFIG.name} {version}")
    top = config["archive"][: -len(".tar.xz")]
    folder = ROOT / "tmp/pc/llvm-linux" / top
    if not (folder / "bin/llvm-config").is_file() and not FETCH:
        no_fetch(folder)
    if not (folder / "bin/llvm-config").is_file():
        import shutil
        import tarfile
        from fetch_tools import download

        archive = download(config["url"], config["sha256"])
        staging = folder.parent / (top + ".part")
        shutil.rmtree(staging, ignore_errors=True)
        # As a stream: one pass through the archive, each part as it comes.
        with tarfile.open(archive, "r|xz") as bundle:
            for member in bundle:
                head, _, relative = member.name.partition("/")
                if head != top or not (member.isfile() or member.issym()) or not relative.startswith(LINUX_PARTS):
                    continue
                member.name = relative
                if hasattr(tarfile, "data_filter"):
                    bundle.extract(member, staging, filter="data")
                else:
                    bundle.extract(member, staging)
        shutil.rmtree(folder, ignore_errors=True)
        staging.replace(folder)
        os.remove(archive)  # 2 GB, and the unpacked parts are all that is used
    version_found = subprocess.check_output([str(folder / "bin/llvm-config"), "--version"], text=True).strip()
    if version_found != version:
        raise RuntimeError(f"Expected LLVM {version}, found {version_found}")
    return folder


@functools.lru_cache(maxsize=1)
def toolchain():
    config = json.loads(CONFIG.read_text())
    if host_llvm():
        folder = host_llvm()
        config_tool = folder / ("bin/llvm-config" + (".exe" if sys.platform == "win32" else ""))
        version = subprocess.check_output([str(config_tool), "--version"], text=True).strip()
        if version != config["version"]:
            raise RuntimeError(f"Expected LLVM {config['version']}, found {version}")
        return folder
    if sys.platform.startswith("linux"):
        return linux_llvm(config["version"])
    if sys.platform != "darwin":
        # No official archive has the libraries for this host (Windows).
        raise RuntimeError(f"no LLVM {config['version']} for this host: set MEMORIES_GUEST_LLVM to a build of it "
                           "(and MEMORIES_GUEST_CXX to its C++ compiler)")
    folder = ROOT / "tmp/pc/llvm-macos" / ("LLVM-" + config["version"] + "-macOS-ARM64")
    if not (folder / "bin/llvm-config").is_file() and not FETCH:
        no_fetch(folder)
    if not (folder / "bin/llvm-config").is_file():
        from fetch_tools import download, unpack

        unpack(download(config["url"], config["sha256"]), str(folder.parent))
    version = subprocess.check_output(
        [str(folder / "bin/llvm-config"), "--version"], text=True
    ).strip()
    if version != config["version"]:
        raise RuntimeError(f"Expected LLVM {config['version']}, found {version}")
    return folder


# A build uses one toolchain snapshot. Revalidate the full persistent key
# at the start of each process, rather than spawning SDK/Git/LLVM queries
# for every IR operation in the same build.
@functools.lru_cache(maxsize=1)
def compiler():
    OUT.mkdir(parents=True, exist_ok=True)
    binary = OUT / ("memories-guest-ir" + (".exe" if sys.platform == "win32" else ""))
    source = ROOT / "tools/pc/llvm/guest_ir.cpp"
    if not FETCH:
        # Before anything that could fetch: without the toolchain here, a
        # pass built before in this checkout is taken as it is (its stamp
        # cannot be checked without llvm-config), else the reason.
        try:
            toolchain()
        except RuntimeError:
            if binary.is_file() and (OUT / "compiler.fingerprint").is_file():
                return binary
            raise
    if host_llvm() or sys.platform.startswith("linux"):
        return host_compiler(binary, source)
    deps_source = Path(__file__).with_name("macos_deps.py")
    llvm = toolchain()
    llvm_config = subprocess.check_output(
        [
            str(llvm / "bin/llvm-config"),
            "--cxxflags",
            "--ldflags",
            "--libs",
            "core",
            "irreader",
            "passes",
            "support",
            "--system-libs",
        ],
        text=True,
    )
    flags = shlex.split(llvm_config)
    from macos_deps import ensure, sdk_path

    dependencies = ROOT / "tmp/pc/macos-deps/install"
    if not (dependencies / "lib/libzstd.a").is_file():
        dependencies = ensure(("zstd",))
    # LLVM's official archive encodes its build machine's absolute zstd
    # path. Replace that dependency by identity, regardless of its prefix.
    flags = [
        str(dependencies / "lib/libzstd.a") if Path(flag).name == "libzstd.a" else flag
        for flag in flags
    ]
    unexpected = [
        flag
        for flag in flags
        if flag.startswith("/")
        and not Path(flag).is_relative_to(llvm)
        and not Path(flag).is_relative_to(dependencies)
    ]
    if unexpected:
        raise RuntimeError(f"Unsupported external LLVM dependencies: {unexpected}")
    sdk = str(sdk_path())
    command = [
        str(llvm / "bin/clang++"),
        "-isysroot",
        sdk,
        "-nostdlib++",
        str(source),
        *flags,
        str(llvm / "lib/libc++.a"),
        str(llvm / "lib/libc++abi.a"),
        "-Wl,-rpath,@loader_path/../llvm-macos/" + llvm.name + "/lib",
        "-O2",
        "-o",
        str(binary),
    ]
    revision = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True
    ).strip()
    fingerprint_data = {
        "revision": revision,
        "source": hashlib.sha256(source.read_bytes()).hexdigest(),
        "memory_map": hashlib.sha256((ROOT / "src/pc/memory_map.h").read_bytes()).hexdigest(),
        "config": hashlib.sha256(CONFIG.read_bytes()).hexdigest(),
        "driver": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "dependencies_script": hashlib.sha256(deps_source.read_bytes()).hexdigest(),
        "llvm_config": llvm_config,
        "sdk": sdk,
        "command": command,
    }
    fingerprint = hashlib.sha256(
        json.dumps(fingerprint_data, sort_keys=True).encode()
    ).hexdigest()
    stamp = OUT / "compiler.fingerprint"
    if (
        binary.is_file()
        and stamp.is_file()
        and stamp.read_text().strip() == fingerprint
    ):
        return binary
    # Publish a complete executable: parallel runners can still use the old
    # compiler while another process rebuilds it for the same fingerprint.
    with tempfile.TemporaryDirectory(dir=OUT) as temporary:
        staged = Path(temporary) / binary.name
        subprocess.run([*command[:-1], str(staged)], check=True)
        staged.replace(binary)
    stamp.write_text(fingerprint + "\n")
    return binary


def host_compiler(binary, source):
    """The pass built with this host's C++ compiler (MEMORIES_GUEST_CXX, else
    c++: on Linux the system's g++, whose libstdc++ the official archive's
    libraries are built against) and MEMORIES_GUEST_LLVM or the official
    Linux archive (linux_llvm): the same source and LLVM version as on macOS.
    On Linux the archive's --system-libs want zlib and zstd (zlib1g-dev,
    libzstd-dev)."""
    llvm = toolchain()
    config_tool = llvm / ("bin/llvm-config" + (".exe" if sys.platform == "win32" else ""))
    llvm_config = subprocess.check_output(
        [str(config_tool), "--cxxflags", "--ldflags", "--libs", "core", "irreader",
         "passes", "support", "--system-libs"],
        text=True,
    )
    cxx = os.environ.get("MEMORIES_GUEST_CXX", "c++")
    command = [cxx, str(source), *shlex.split(llvm_config.replace("\\", "/")), "-O2",
               *(["-static"] if sys.platform == "win32" else []), "-o", str(binary)]
    fingerprint = hashlib.sha256(json.dumps({
        "source": hashlib.sha256(source.read_bytes()).hexdigest(),
        "memory_map": hashlib.sha256((ROOT / "src/pc/memory_map.h").read_bytes()).hexdigest(),
        "config": hashlib.sha256(CONFIG.read_bytes()).hexdigest(),
        "linux_config": hashlib.sha256(LINUX_CONFIG.read_bytes()).hexdigest(),
        "driver": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "command": command,
    }, sort_keys=True).encode()).hexdigest()
    stamp = OUT / "compiler.fingerprint"
    if binary.is_file() and stamp.is_file() and stamp.read_text().strip() == fingerprint:
        return binary
    with tempfile.TemporaryDirectory(dir=OUT) as temporary:
        staged = Path(temporary) / binary.name
        subprocess.run([*command[:-1], str(staged)], check=True)
        staged.replace(binary)
    stamp.write_text(fingerprint + "\n")
    return binary


def process(operation, text, **options):
    binary = compiler()
    identity = (OUT / "compiler.fingerprint").read_text().strip()
    digest = hashlib.sha256(
        (identity + operation + text + json.dumps(options, sort_keys=True)).encode()
    ).hexdigest()
    cache = OUT / "cache"
    cache.mkdir(exist_ok=True)
    cached = cache / (digest + (".json" if operation == "inspect" else ".ll"))
    if cached.is_file():
        result = cached.read_text()
    else:
        with tempfile.TemporaryDirectory(dir=OUT) as temporary:
            folder = Path(temporary)
            source = folder / "input.ll"
            source.write_text(text)
            config = folder / "options.json"
            config.write_text(json.dumps(options))
            output = folder / "output"
            result = subprocess.run(
                [str(binary), operation, str(source), str(output), str(config)],
                capture_output=True,
                text=True,
            )
            if result.returncode:
                raise TranslationError(result.stderr.strip())
            result = output.read_text()
            with tempfile.NamedTemporaryFile(
                mode="w", dir=cache, delete=False
            ) as staging:
                staging.write(result)
                staged = Path(staging.name)
            staged.replace(cached)
    return json.loads(result) if operation == "inspect" else result


def translate(text, pinned=None, registration=None, *, game_unit=False, hooks=False):
    return process(
        "translate",
        text,
        pins=pinned or {},
        game_unit=game_unit,
        hooks=hooks,
        **({"registration": registration} if registration else {}),
    )


def normalize(
    text, renames=None, drop_definitions=(), *, export_functions=(), keep_functions=None
):
    return process(
        "normalize",
        text,
        renames=renames or {},
        drop_definitions=list(drop_definitions),
        export_functions=list(export_functions),
        **(
            {"keep_functions": sorted(keep_functions)}
            if keep_functions is not None
            else {}
        ),
    )


def inspect(text):
    return process("inspect", text)


def validate_c_abi(name, signature):
    if signature["calling_convention"] != 0:
        raise ValueError(
            f"Unsupported calling convention: {name}: {signature['calling_convention']}"
        )
    if signature["unsupported_abi_attributes"]:
        raise ValueError(
            f"Unsupported native ABI attributes: {name}: "
            + ", ".join(signature["unsupported_abi_attributes"])
        )


if __name__ == "__main__":
    # The guest pass, built when it is not up to date, for build_mod.py's
    # aarch64-translated target outside this checkout (MEMORIES_GUEST_IR).
    print(compiler())
