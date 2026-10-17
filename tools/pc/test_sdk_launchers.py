#!/usr/bin/env python3
"""Test the SDK's one-click build (tools/pc/sdk: build-mod.bat, build-mod.sh and
toolchain-pins.json) without downloading anything or starting a compiler.

- The launchers carry, in plain text, the URLs and hashes of toolchain-pins.json
  (a batch file cannot read it), and the JSON follows the game's own toolchain
  (test_build_mod_toolchain.py).
- Windows' embeddable Python starts like `python -I`: the script's folder is not
  on sys.path. tools/build_mod.py imports its sibling modules, so it puts its own
  folder there first; the launcher then leaves the stock python3xx._pth alone.
- Each launcher is run on a fake SDK (a build_mod.py that prints what it was
  given) with archives made here and file:// addresses: the download, the hash
  check, the refusal on a mismatch, the unpacking, the second run that does
  nothing, the arguments (a folder with spaces and parentheses, a trailing
  backslash), the exit code, MEMORIES_SDK_PYTHON and MEMORIES_SDK_TOOLCHAIN.
- package.py puts the three files into an SDK with the right line ends."""
import ast, hashlib, io, json, os, pathlib, re, shutil, stat, subprocess, sys, tarfile, tempfile, unittest, zipfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import package

SOURCE = HERE / "sdk"
PINS = json.loads((SOURCE / "toolchain-pins.json").read_text(encoding="utf-8"))
WINDOWS = os.name == "nt"

# build_mod.py as the SDK's tools folder has it, which the launchers run.
FAKE_BUILD_MOD = '''import os, sys
print("ARGS=" + "|".join(sys.argv[1:]))
print("PATH=" + os.environ["PATH"])
sys.exit(int(os.environ.get("FAKE_EXIT", "0")))
'''


def bat_value(text, name):
    found = re.findall(rf'^set "{name}=(.*)"\s*$', text, re.M)
    assert len(found) == 1, f"{name} is set {len(found)} times in build-mod.bat"
    return found[0]


def sh_value(text, name):
    found = re.findall(rf'^{name}="(.*)"\s*$', text, re.M)
    assert len(found) == 1, f"{name} is set {len(found)} times in build-mod.sh"
    return found[0]


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def posix_shell():
    """A POSIX shell to run build-mod.sh with: sh, on Windows Git for Windows'."""
    if not WINDOWS:
        return shutil.which("sh")
    git = shutil.which("git")
    if git:
        for root in pathlib.Path(git).resolve().parents[:3]:
            for relative in ("usr/bin/bash.exe", "bin/bash.exe"):
                if (root / relative).is_file():
                    return str(root / relative)
    return None


class Pins(unittest.TestCase):
    def setUp(self):
        self.bat = (SOURCE / "build-mod.bat").read_bytes().decode("utf-8")
        self.sh = (SOURCE / "build-mod.sh").read_text(encoding="utf-8")

    def test_batch_file_carries_the_pins(self):
        llvm, python = PINS["llvm_mingw"], PINS["python"]["windows"]
        self.assertEqual(bat_value(self.bat, "LLVM_RELEASE"), PINS["llvm_mingw"]["release"])
        self.assertEqual(bat_value(self.bat, "LLVM_URL"), llvm["windows"]["url"])
        self.assertEqual(bat_value(self.bat, "LLVM_SHA256"), llvm["windows"]["sha256"])
        self.assertEqual(bat_value(self.bat, "LLVM_MEGABYTES"), str(round(llvm["windows"]["bytes"] / 1e6)))
        self.assertEqual(bat_value(self.bat, "PYTHON_VERSION"), python["version"])
        self.assertEqual(bat_value(self.bat, "PYTHON_URL"), python["url"])
        self.assertEqual(bat_value(self.bat, "PYTHON_SHA256"), python["sha256"])
        self.assertEqual(bat_value(self.bat, "PYTHON_MEGABYTES"), str(round(python["bytes"] / 1e6)))

    def test_shell_script_carries_the_pins(self):
        llvm = PINS["llvm_mingw"]
        self.assertEqual(sh_value(self.sh, "LLVM_RELEASE"), llvm["release"])
        self.assertEqual(sh_value(self.sh, "LLVM_URL"), llvm["linux"]["url"])
        self.assertEqual(sh_value(self.sh, "LLVM_SHA256"), llvm["linux"]["sha256"])
        self.assertEqual(sh_value(self.sh, "LLVM_MEGABYTES"), str(round(llvm["linux"]["bytes"] / 1e6)))
        major, minor = PINS["python"]["minimum"].split(".")
        self.assertEqual((sh_value(self.sh, "PYTHON_MINIMUM_MAJOR"), sh_value(self.sh, "PYTHON_MINIMUM_MINOR")),
                         (major, minor))

    def test_pins_are_consistent(self):
        python = PINS["python"]["windows"]
        major, minor, _ = python["version"].split(".")
        self.assertEqual(python["pth"], f"python{major}{minor}._pth")
        self.assertIn(f"/python/{python['version']}/python-{python['version']}-embed-amd64.zip", python["url"])
        for entry in (python, PINS["llvm_mingw"]["windows"], PINS["llvm_mingw"]["linux"]):
            self.assertTrue(entry["url"].startswith("https://"))
            self.assertRegex(entry["sha256"], r"^[0-9a-f]{64}$")

    def test_what_the_python_sources_need(self):
        """The minimum the launchers state is what the SDK's scripts parse as."""
        minimum = tuple(int(part) for part in PINS["python"]["minimum"].split("."))
        for name in ("build_mod.py", "build_process.py", "ptr32_stores.py"):
            ast.parse((HERE / name).read_text(encoding="utf-8"), name, feature_version=minimum)

    def test_plain_scripts(self):
        """Antivirus programs flagged v0.1.4-preview.1: nothing here that looks like a
        dropper. One address per download, the hashes in the file."""
        for text in (self.bat, self.sh):
            lowered = text.lower()
            for word in ("powershell", "-enc", "base64", "invoke-", "iex ", "bitsadmin", "mshta", "wscript", "regsvr32",
                         "urlcache", "-split"):
                self.assertNotIn(word, lowered)
        self.assertEqual(len(re.findall(r'^set "(?:PYTHON|LLVM)_URL=', self.bat, re.M)), 2)
        self.assertEqual(len(re.findall(r'^LLVM_URL=', self.sh, re.M)), 1)

    def test_batch_file_keeps_windows_line_ends(self):
        raw = (SOURCE / "build-mod.bat").read_bytes()
        self.assertEqual(raw.count(b"\n"), raw.count(b"\r\n"), "a batch file with bare LF mis-reads its labels")
        self.assertNotIn(b"\r", (SOURCE / "build-mod.sh").read_bytes())
        self.assertTrue(self.sh.startswith("#!/bin/sh\n"))
        attributes = (HERE.parent.parent / ".gitattributes").read_text(encoding="utf-8").splitlines()
        self.assertIn("*.bat text eol=crlf", attributes)
        self.assertIn("*.sh text eol=lf", attributes)

    def test_no_block_prints_a_path(self):
        """A folder called "yfm (1)" ends a parenthesized block at its ")": no line
        that opens a block (cmd's parser) may be followed by one that echoes a variable."""
        lines = self.bat.splitlines()
        for number, line in enumerate(lines):
            if line.rstrip().endswith("("):
                body = []
                for following in lines[number + 1:]:
                    if following.strip() == ")":
                        break
                    body.append(following)
                for text in body:
                    self.assertFalse(text.lstrip().lower().startswith("echo") and "%" in text.replace("%%", ""),
                                     f"a block echoes a variable: {text}")


class EmbeddedPython(unittest.TestCase):
    def test_siblings_are_found_without_the_scripts_folder(self):
        """The embeddable Python (python3xx._pth: isolated mode) and `python -I`
        have no script folder on sys.path; build_mod.py runs from the SDK's tools
        folder, whose build_process.py and ptr32_stores.py it imports."""
        with tempfile.TemporaryDirectory(prefix="sdk-layout-") as folder:
            tools = pathlib.Path(folder) / "sdk" / "tools"
            tools.mkdir(parents=True)
            for name in ("build_mod.py", "build_process.py", "ptr32_stores.py"):
                shutil.copyfile(HERE / name, tools / name)
            elsewhere = pathlib.Path(folder) / "elsewhere"
            elsewhere.mkdir()
            result = subprocess.run([sys.executable, "-I", str(tools / "build_mod.py"), "--help"], cwd=elsewhere,
                                    capture_output=True, text=True, encoding="utf-8", errors="replace")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("usage:", result.stdout)

    def test_the_sibling_import_follows_the_path_insert(self):
        text = (HERE / "build_mod.py").read_text(encoding="utf-8")
        insert = text.index("sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))")
        for module in ("build_process",):
            self.assertLess(insert, text.index(f"\nimport {module}\n"))
        self.assertEqual(text[:insert].count("import build_process"), 0)


class Packaged(unittest.TestCase):
    def test_package_puts_the_files_in_the_sdk(self):
        with tempfile.TemporaryDirectory(prefix="sdk-package-") as folder:
            package.add_launchers(folder)
            names = sorted(os.listdir(folder))
            self.assertEqual(names, sorted(package.SDK_LAUNCHERS))
            bat = pathlib.Path(folder, "build-mod.bat").read_bytes()
            self.assertEqual(bat.count(b"\n"), bat.count(b"\r\n"))
            self.assertGreater(bat.count(b"\r\n"), 100)
            sh = pathlib.Path(folder, "build-mod.sh").read_bytes()
            self.assertNotIn(b"\r", sh)
            self.assertEqual(json.loads(pathlib.Path(folder, "toolchain-pins.json").read_text(encoding="utf-8")), PINS)
            if not WINDOWS:
                self.assertTrue(os.stat(os.path.join(folder, "build-mod.sh")).st_mode & stat.S_IXUSR)


    def test_the_archive_check_accepts_them_and_refuses_a_bare_lf(self):
        """test_package.py (the release workflow's check of the archives) on a
        synthetic SDK of each kind."""
        import test_package
        with tempfile.TemporaryDirectory(prefix="sdk-archive-") as folder:
            sdk = pathlib.Path(folder, "yfm-redecomp-x", "sdk")
            sdk.mkdir(parents=True)
            package.add_launchers(str(sdk))
            contents = {"sdk/" + name for name in package.SDK_LAUNCHERS}
            zipped = pathlib.Path(folder, "x-windows.zip")
            with zipfile.ZipFile(zipped, "w") as archive:
                for name in package.SDK_LAUNCHERS:
                    archive.write(sdk / name, f"yfm-redecomp-x/sdk/{name}")
            test_package.check_launchers(zipped, "yfm-redecomp-x", contents, True)
            tarred = pathlib.Path(folder, "x-linux.tar.gz")
            with tarfile.open(tarred, "w:gz") as archive:
                for name in package.SDK_LAUNCHERS:
                    info = archive.gettarinfo(sdk / name, f"yfm-redecomp-x/sdk/{name}")
                    info.mode = 0o755 if name.endswith(".sh") else 0o644
                    archive.addfile(info, io.BytesIO((sdk / name).read_bytes()))
            test_package.check_launchers(tarred, "yfm-redecomp-x", contents, False)
            bare = (sdk / "build-mod.bat").read_bytes().replace(bytes([13, 10]), bytes([10]))
            (sdk / "build-mod.bat").write_bytes(bare)
            with zipfile.ZipFile(zipped, "w") as archive:
                for name in package.SDK_LAUNCHERS:
                    archive.write(sdk / name, f"yfm-redecomp-x/sdk/{name}")
            with self.assertRaises(AssertionError):
                test_package.check_launchers(zipped, "yfm-redecomp-x", contents, True)
            with self.assertRaises(AssertionError):
                test_package.check_launchers(zipped, "yfm-redecomp-x", contents - {"sdk/build-mod.sh"}, True)


class Launcher:
    """A launcher run on a fake SDK. Subclasses say how to start it."""

    def setUp(self):
        self.folder = pathlib.Path(tempfile.mkdtemp(prefix="sdk-launcher-"))
        self.addCleanup(shutil.rmtree, self.folder, ignore_errors=True)
        self.sdk = self.folder / "game (1)" / "sdk"      # a ")" in the way, as a browser's "(1)" copy has
        (self.sdk / "tools").mkdir(parents=True)
        (self.sdk / "tools" / "build_mod.py").write_text(FAKE_BUILD_MOD, encoding="utf-8")
        self.serve = self.folder / "serve"
        self.serve.mkdir()
        self.mod = self.folder / "my mod (2)"
        self.mod.mkdir()
        (self.mod / "mod.json").write_text("{}", encoding="utf-8")

    def environment(self, **extra):
        environment = {name: value for name, value in os.environ.items() if not name.startswith("MEMORIES_SDK_")}
        environment["MEMORIES_SDK_NOPAUSE"] = "1"
        environment.update(extra)
        return environment

    def own_tools(self):
        """A python and a toolchain of the test's own (MEMORIES_SDK_*)."""
        bin = self.folder / "own" / "bin"
        bin.mkdir(parents=True)
        self.make_clang(bin)
        return {"MEMORIES_SDK_PYTHON": self.python_path(), "MEMORIES_SDK_TOOLCHAIN": self.native(bin.parent)}, bin

    def test_arguments_go_to_build_mod(self):
        tools, bin = self.own_tools()
        result = self.run_launcher([str(self.mod), "--target", "i386"], tools)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f"ARGS={self.mod}|--target|i386".replace("\\", "/"), result.stdout.replace("\\", "/"))
        self.assertEqual(os.path.normcase(os.path.normpath(self.pathfirst(result.stdout))),
                         os.path.normcase(os.path.normpath(str(bin))))
        self.assertIn("done", result.stdout)
        self.assertNotIn("downloading", result.stdout)
        self.assertFalse((self.sdk / "toolchain").exists(), "nothing is fetched when both tools are named")

    def test_a_failed_build_is_reported(self):
        tools, _ = self.own_tools()
        result = self.run_launcher([str(self.mod)], dict(tools, FAKE_EXIT="3"))
        self.assertEqual(result.returncode, 3)
        self.assertIn("stopped", result.stdout + result.stderr)
        self.assertNotIn("done", result.stdout)

    def test_not_a_folder(self):
        tools, _ = self.own_tools()
        result = self.run_launcher([str(self.folder / "missing")], tools)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("is not a folder", result.stdout + result.stderr)

    def test_named_tools_must_exist(self):
        tools, _ = self.own_tools()
        result = self.run_launcher([str(self.mod)], dict(tools, MEMORIES_SDK_TOOLCHAIN=str(self.folder / "nothing")))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("MEMORIES_SDK_TOOLCHAIN", result.stdout + result.stderr)
        result = self.run_launcher([str(self.mod)], dict(tools, MEMORIES_SDK_PYTHON=str(self.folder / "nopython")))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("MEMORIES_SDK_PYTHON", result.stdout + result.stderr)

    def test_help(self):
        result = self.run_launcher(["--help"], {})
        self.assertEqual(result.returncode, 0)
        for text in ("MEMORIES_SDK_PYTHON", "MEMORIES_SDK_TOOLCHAIN", "--setup"):
            self.assertIn(text, result.stdout)

    def test_download_unpack_and_second_run(self):
        self.patch_pins()
        first = self.run_launcher(["--setup"], {})
        self.assertEqual(first.returncode, 0, first.stdout + first.stderr)
        self.assertEqual(first.stdout.count("downloading"), self.downloads)
        self.assertIn("SHA-256 matches", first.stdout)
        toolchain = self.sdk / "toolchain"
        self.check_unpacked(toolchain)
        leftovers = sorted(path.name for path in toolchain.iterdir() if path.name.startswith(".unpack")) +             sorted(path.name for path in (toolchain / "downloads").iterdir())
        self.assertEqual(leftovers, [], "the archives and the half-unpacked folders are gone")
        # A second run finds everything there and fetches nothing: the archives are gone from the server.
        shutil.rmtree(self.serve)
        second = self.run_launcher(["--setup"], {})
        self.assertEqual(second.returncode, 0, second.stdout + second.stderr)
        self.assertNotIn("downloading", second.stdout)

    def test_a_wrong_hash_is_refused(self):
        self.patch_pins(corrupt="llvm")
        result = self.run_launcher(["--setup"], {})
        self.assertNotEqual(result.returncode, 0)
        text = result.stdout + result.stderr
        self.assertIn("SHA-256 of the download is wrong", text)
        self.assertIn("nothing was unpacked", text)
        toolchain = self.sdk / "toolchain"
        self.assertEqual([path.name for path in toolchain.glob("llvm-mingw-*")], [], "nothing of the compiler was unpacked")
        self.assertEqual([path.name for path in (toolchain / "downloads").iterdir() if "llvm" in path.name], [],
                         "the file that does not match is deleted")
        # And the build does not start.
        tools = {"MEMORIES_SDK_PYTHON": self.python_path()}
        result = self.run_launcher([str(self.mod)], dict(tools))
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("ARGS=", result.stdout)


@unittest.skipUnless(WINDOWS, "build-mod.bat runs on Windows")
class Batch(Launcher, unittest.TestCase):
    downloads = 2

    def setUp(self):
        Launcher.setUp(self)
        text = (SOURCE / "build-mod.bat").read_bytes()
        (self.sdk / "build-mod.bat").write_bytes(text)

    def python_path(self):
        return sys.executable

    def native(self, path):
        return str(path)

    def make_clang(self, bin):
        (bin / "clang.exe").write_bytes(b"")

    def pathfirst(self, output):
        return re.search(r"^PATH=(.*)$", output, re.M).group(1).split(os.pathsep)[0].strip()

    def run_launcher(self, arguments, extra):
        # The batch file itself, as a double-click starts it (no "cmd /c" in front of
        # a quoted name, which cmd would strip the outer quotes of). A string, not a
        # list, for the line as Explorer writes it: Python doubles a trailing backslash.
        line = " ".join(f'"{argument}"' for argument in (self.sdk / "build-mod.bat", *arguments))
        return subprocess.run(line, env=self.environment(**extra),
                              capture_output=True, text=True, encoding="utf-8", errors="replace", stdin=subprocess.DEVNULL,
                              cwd=self.folder, timeout=300)

    def patch_pins(self, corrupt=None):
        python = io.BytesIO()
        with zipfile.ZipFile(python, "w") as archive:
            archive.writestr("python.exe", b"MZ fake")
            archive.writestr("python314.zip", b"stdlib")
            archive.writestr(PINS["python"]["windows"]["pth"], "python314.zip\n.\n\n# Uncomment to run site.main() automatically\n#import site\n")
        llvm = io.BytesIO()
        top = pathlib.PurePosixPath(PINS["llvm_mingw"]["windows"]["url"]).name[:-len(".zip")]
        with zipfile.ZipFile(llvm, "w") as archive:
            archive.writestr(f"{top}/bin/clang.exe", b"")
            archive.writestr(f"{top}/bin/ld.lld.exe", b"")
        text = (SOURCE / "build-mod.bat").read_bytes().decode("utf-8")
        for name, data, bad in (("PYTHON", python.getvalue(), corrupt == "python"), ("LLVM", llvm.getvalue(), corrupt == "llvm")):
            file = self.serve / pathlib.PurePosixPath(bat_value(text, f"{name}_URL")).name
            file.write_bytes(data)
            text = text.replace(f'set "{name}_URL={bat_value(text, name + "_URL")}"', f'set "{name}_URL={file.as_uri()}"')
            text = text.replace(f'set "{name}_SHA256={bat_value(text, name + "_SHA256")}"',
                                f'set "{name}_SHA256={sha256(data + (b"x" if bad else b""))}"')
        (self.sdk / "build-mod.bat").write_bytes(text.encode("utf-8"))

    def check_unpacked(self, toolchain):
        python = toolchain / f"python-{PINS['python']['windows']['version']}"
        self.assertTrue((python / "python.exe").is_file())
        # The stock ._pth is left alone: build_mod.py puts its own folder on sys.path.
        self.assertEqual((python / PINS["python"]["windows"]["pth"]).read_text().splitlines()[:2], ["python314.zip", "."])
        self.assertTrue((toolchain / f"llvm-mingw-{PINS['llvm_mingw']['release']}" / "bin" / "clang.exe").is_file())

    def test_asks_for_the_folder(self):
        tools, _ = self.own_tools()
        result = subprocess.run([str(self.sdk / "build-mod.bat")], env=self.environment(**tools), input=f'"{self.mod}"\n',
                                capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=self.folder, timeout=300)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f"ARGS={self.mod}\n", result.stdout.replace("\r\n", "\n"))

    def test_trailing_backslash_does_not_eat_the_quote(self):
        tools, _ = self.own_tools()
        result = self.run_launcher([str(self.mod) + "\\", "--out", str(self.folder / "o u t")], tools)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f"ARGS={self.mod}|--out|{self.folder / 'o u t'}", result.stdout)

    def test_waits_for_a_key_only_when_opened_by_a_double_click(self):
        tools, _ = self.own_tools()
        environment = self.environment(**tools)
        del environment["MEMORIES_SDK_NOPAUSE"]
        run = lambda: subprocess.run([str(self.sdk / "build-mod.bat"), str(self.mod)], env=environment, capture_output=True,
                                     text=True, encoding="utf-8", errors="replace", stdin=subprocess.DEVNULL, cwd=self.folder, timeout=300)
        waited = run().stdout.strip().splitlines()    # started as a double-click starts it: cmd /c and the file
        environment["MEMORIES_SDK_NOPAUSE"] = "1"
        not_waited = run().stdout.strip().splitlines()
        # pause's line is in the language of Windows: only that it is there is checked.
        self.assertGreater(len(waited), len(not_waited), waited)
        self.assertEqual(waited[:len(not_waited)], not_waited)


@unittest.skipUnless(posix_shell(), "no POSIX shell")
class Shell(Launcher, unittest.TestCase):
    downloads = 1

    def setUp(self):
        Launcher.setUp(self)
        self.shell = posix_shell()
        self.uname = self.folder / "fake-uname"
        self.uname.mkdir()
        # build-mod.sh downloads for Linux x86-64 only; the test fakes uname so that it runs anywhere.
        (self.uname / "uname").write_text('#!/bin/sh\ncase "$1" in -s) echo Linux;; -m) echo x86_64;; *) echo Linux;; esac\n', newline="\n")
        os.chmod(self.uname / "uname", 0o755)
        (self.sdk / "build-mod.sh").write_bytes((SOURCE / "build-mod.sh").read_bytes())

    def python_path(self):
        return sys.executable.replace("\\", "/")

    def native(self, path):
        """The path as the shell writes it: /c/Users/... in an MSYS shell on Windows."""
        text = str(path).replace("\\", "/")
        return re.sub(r"^([A-Za-z]):", lambda match: "/" + match.group(1).lower(), text) if WINDOWS else text

    def make_clang(self, bin):
        (bin / "clang").write_text("#!/bin/sh\n", newline="\n")
        os.chmod(bin / "clang", 0o755)

    def pathfirst(self, output):
        whole = re.search(r"^PATH=(.*)$", output, re.M).group(1).strip()
        return re.split(r";|:(?![/\\])", whole)[0]   # ":" also separates, in a shell that is not Windows' own

    def run_launcher(self, arguments, extra):
        environment = self.environment(**extra)
        environment["PATH"] = self.native(self.uname) + os.pathsep + environment["PATH"]
        script = str(self.sdk / "build-mod.sh").replace("\\", "/")
        return subprocess.run([self.shell, script, *[a.replace("\\", "/") for a in arguments]], env=environment,
                              capture_output=True, text=True, encoding="utf-8", errors="replace", stdin=subprocess.DEVNULL,
                              cwd=self.folder, timeout=300)

    def patch_pins(self, corrupt=None):
        llvm = io.BytesIO()
        top = pathlib.PurePosixPath(PINS["llvm_mingw"]["linux"]["url"]).name[:-len(".tar.xz")]
        with tarfile.open(fileobj=llvm, mode="w:xz") as archive:
            for name in ("bin/clang", "bin/ld.lld"):
                info = tarfile.TarInfo(f"{top}/{name}")
                info.mode = 0o755
                info.size = 12
                archive.addfile(info, io.BytesIO(b"#!/bin/sh\n:\n\n"))
        data = llvm.getvalue()
        text = (SOURCE / "build-mod.sh").read_text(encoding="utf-8")
        file = self.serve / pathlib.PurePosixPath(sh_value(text, "LLVM_URL")).name
        file.write_bytes(data)
        text = text.replace(f'LLVM_URL="{sh_value(text, "LLVM_URL")}"', f'LLVM_URL="{file.as_uri()}"')
        text = text.replace(f'LLVM_SHA256="{sh_value(text, "LLVM_SHA256")}"',
                            f'LLVM_SHA256="{sha256(data + (b"x" if corrupt == "llvm" else b""))}"')
        (self.sdk / "build-mod.sh").write_bytes(text.encode("utf-8"))

    def check_unpacked(self, toolchain):
        self.assertTrue((toolchain / f"llvm-mingw-{PINS['llvm_mingw']['release']}" / "bin" / "clang").is_file())

    def test_other_systems_are_told_to_name_a_toolchain(self):
        (self.uname / "uname").write_text('#!/bin/sh\necho Darwin\n', newline="\n")
        result = self.run_launcher(["--setup"], {"MEMORIES_SDK_PYTHON": self.python_path()})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("MEMORIES_SDK_TOOLCHAIN", result.stdout + result.stderr)

    def test_too_old_a_python(self):
        old = self.folder / "old-python"
        old.write_text('#!/bin/sh\nexit 1\n', newline="\n")
        os.chmod(old, 0o755)
        result = self.run_launcher([str(self.mod)], {"MEMORIES_SDK_PYTHON": self.native(old)})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("older than python 3.8", result.stdout + result.stderr)

    def test_current_folder(self):
        tools, _ = self.own_tools()
        script = str(self.sdk / "build-mod.sh").replace("\\", "/")
        environment = self.environment(**tools)
        result = subprocess.run([self.shell, script], env=environment, cwd=self.mod, capture_output=True, text=True,
                                encoding="utf-8", errors="replace", stdin=subprocess.DEVNULL, timeout=300)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("ARGS=.", result.stdout)
        result = subprocess.run([self.shell, script], env=environment, cwd=self.folder, capture_output=True, text=True,
                                encoding="utf-8", errors="replace", stdin=subprocess.DEVNULL, timeout=300)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no mod folder given", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
