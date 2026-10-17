#!/usr/bin/env python3
"""Test what tools/pc/build_mod.py tells a modder who cannot build the 64-bit
objects (no compiler starts here: the programs are empty files on a PATH of
the test's own).

- The llvm-mingw it points to is the one the repository builds the game
  with (tools/pc/fetch_tools.py on Windows, build_win32_deps.py on Linux).
- It names exactly what is missing of clang, ld.lld and llvm-objcopy.
- Without any of them, and no gcc, it builds nothing and says so; asked for
  a 64-bit target by name without them, it builds nothing either."""
import contextlib, io, json, os, pathlib, shutil, stat, subprocess, sys, tempfile, unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_mod, build_win32_deps, fetch_tools

TOOL_LLVM_MAJOR = build_mod.tool_llvm_major.__wrapped__   # what the tests below replace

EXE = ".exe" if os.name == "nt" else ""


class Toolchain(unittest.TestCase):
    def setUp(self):
        self.bin = tempfile.mkdtemp(prefix="build-mod-toolchain-")
        self.addCleanup(shutil.rmtree, self.bin, ignore_errors=True)
        environment = {name: value for name, value in os.environ.items() if name != "MEMORIES_MOD_CC"}
        environment["PATH"] = self.bin
        # The LLVM the guest pass says it is built against (--llvm-major): the
        # programs here are empty files that do not run, so tests set it
        # (None: a pass that cannot say).
        self.pass_major = None
        patches = [mock.patch.dict(os.environ, environment, clear=True),
                   mock.patch.object(build_mod, "LLVM_MINGW", os.path.join(self.bin, "no-llvm-mingw")),
                   mock.patch.object(build_mod, "tool_llvm_major", lambda tool: self.pass_major)]
        for patch in patches:
            patch.start()
            self.addCleanup(patch.stop)
        build_mod.find_programs.cache_clear()
        build_mod.find_objcopy.cache_clear()
        self.addCleanup(build_mod.find_programs.cache_clear)
        self.addCleanup(build_mod.find_objcopy.cache_clear)

    def program(self, name):
        path = os.path.join(self.bin, name + EXE)
        with open(path, "w"):
            pass
        os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)
        return path

    def test_pinned_release(self):
        self.assertEqual(build_mod.LLVM_MINGW_DOWNLOADS["windows"], fetch_tools.RELEASES["llvm-mingw"][0])
        self.assertEqual(build_mod.LLVM_MINGW_DOWNLOADS["linux"], build_win32_deps.TOOLCHAIN_ARCHIVE[0])
        for url in build_mod.LLVM_MINGW_DOWNLOADS.values():
            self.assertIn(f"/download/{build_mod.LLVM_MINGW_RELEASE}/llvm-mingw-{build_mod.LLVM_MINGW_RELEASE}-", url)

    def test_names_what_is_missing(self):
        self.assertEqual(build_mod.missing_64bit(), ["clang", "ld.lld", "llvm-objcopy"])
        self.program("clang")
        self.assertEqual(build_mod.missing_64bit(), ["ld.lld", "llvm-objcopy"])
        self.program("ld.lld")
        self.assertEqual(build_mod.missing_64bit(), ["llvm-objcopy"])
        self.program("llvm-objcopy")
        self.assertEqual(build_mod.missing_64bit(), [])
        with mock.patch.dict(os.environ, {"MEMORIES_MOD_CC": self.program("gcc")}):
            self.assertEqual(build_mod.missing_64bit(), ["clang"])
            self.assertIn("MEMORIES_MOD_CC names", build_mod.toolchain_help(["clang"]))

    def test_help(self):
        text = build_mod.toolchain_help(["clang", "ld.lld"], built=os.path.join("my-mod", "rules.o"))
        self.assertIn("only the 32-bit object was built, " + os.path.join("my-mod", "rules.o"), text)
        self.assertIn("Not found: clang, ld.lld.", text)
        self.assertIn(build_mod.LLVM_MINGW_DOWNLOADS["windows" if sys.platform == "win32" else "linux"], text)
        if sys.platform != "win32":
            self.assertIn("sudo apt install clang lld llvm", text)
        self.assertIn("nothing was built", build_mod.toolchain_help(["clang"], wanted=("aarch64",)))

    def run_main(self, *arguments):
        error = io.StringIO()
        with mock.patch.object(sys, "argv", ["build_mod.py", *arguments]), contextlib.redirect_stderr(error), \
                self.assertRaises(SystemExit) as stopped:
            build_mod.main()
        return str(stopped.exception.code) + error.getvalue()

    def mod(self):
        """A code mod (its source is never compiled here)."""
        mod = os.path.join(self.bin, "mod")
        os.makedirs(mod, exist_ok=True)
        with open(os.path.join(mod, "mod.json"), "w") as handle:
            handle.write('{"id": "mod", "library": "mod"}\n')
        with open(os.path.join(mod, "mod.c"), "w") as handle:
            handle.write("int x;\n")
        return mod

    def test_no_compiler(self):
        text = self.run_main(self.mod())
        self.assertIn("nothing was built: no compiler found", text)
        self.assertIn("Not found: clang, ld.lld, llvm-objcopy.", text)

    def test_asked_for_a_64bit_target(self):
        self.program("gcc")   # the 32-bit object could be built; the one asked for cannot
        text = self.run_main(self.mod(), "--target", "aarch64")
        self.assertIn("nothing was built", text)
        self.assertIn("The 64-bit object (aarch64: Android) needs", text)
        self.assertNotIn("x86_64-windows:", text)

    def test_data_only_needs_no_compiler(self):
        data = os.path.join(self.bin, "data")
        os.makedirs(data)
        output = io.StringIO()
        with mock.patch.object(sys, "argv", ["build_mod.py", data, "--target", "aarch64"]), \
                contextlib.redirect_stdout(output):
            build_mod.main()
        self.assertIn("a data-only mod needs no build", output.getvalue())

    def test_named_compiler(self):
        with mock.patch.dict(os.environ, {"MEMORIES_MOD_CC": os.path.join(self.bin, "nowhere", "clang" + EXE)}):
            self.assertIn("which is not there", build_mod.toolchain_help(build_mod.missing_64bit()))

    def with_pass(self, candidates, major):
        """translation_problem(None) for a guest pass built against LLVM
        `major` (None: one that cannot say) and these [(clang, its major)]."""
        self.pass_major = major
        tool = self.program("memories-guest-ir")
        with mock.patch.object(build_mod, "translated_candidates", lambda: candidates), \
                mock.patch.dict(os.environ, {"MEMORIES_GUEST_IR": tool}):
            return build_mod.translation_problem(None)

    def test_translated_clang_follows_the_pass(self):
        """aarch64-translated: the clang may not be newer than the LLVM the
        guest pass is built against (the pass reads that clang's IR, and the
        clang reads what the pass writes). The game's own pass is LLVM 21:
        llvm-mingw's clang 22 is refused, with the NDK's as the way out; an
        SDK's pass is LLVM 22, built for llvm-mingw's clang."""
        clang = self.program("clang")
        self.program("ld.lld")
        text = self.with_pass([(clang, 22)], 21)
        self.assertIn("needs a clang of LLVM 21 or older", text)
        self.assertIn(f"{clang} is clang 22", text)
        self.assertIn("ANDROID_NDK_ROOT", text)
        self.assertNotIn("llvm-mingw " + build_mod.LLVM_MINGW_RELEASE, text)   # newer than the pass: no way out
        self.assertIsNone(self.with_pass([(clang, 21)], 21))
        self.assertIsNone(self.with_pass([(clang, 22)], 22))
        self.assertIsNone(self.with_pass([(clang, 21)], 22))   # older than the pass: allowed
        text = self.with_pass([(clang, 23)], 22)
        self.assertIn("needs a clang of LLVM 22 or older", text)
        self.assertIn("llvm-mingw " + build_mod.LLVM_MINGW_RELEASE, text)
        self.assertIn(build_mod.LLVM_MINGW_DOWNLOADS["windows" if os.name == "nt" else "linux"], text)
        self.assertIn("MEMORIES_MOD_CC", text)
        # A pass that cannot say is taken for the game's LLVM.
        self.assertIsNone(self.with_pass([(clang, 21)], None))
        self.assertIn("needs a clang of LLVM 21 or older", self.with_pass([(clang, 22)], None))
        # Without ld.lld no clang fits, whatever its version.
        os.remove(os.path.join(self.bin, "ld.lld" + EXE))
        self.assertIn("needs a clang of LLVM 22 or older", self.with_pass([(clang, 22)], 22))

    def test_translated_apple_clang(self):
        """Apple's clang has Xcode's version numbers, no LLVM major: it is not
        compared, only tried, and after any clang with a version that fits."""
        apple = self.program("clang")
        self.program("ld.lld")
        other = self.program("other-clang")
        said = {apple: "Apple clang version 17.0.0 (clang-1700.0.13.5)\nTarget: arm64-apple-darwin24.0.0\n"}
        with mock.patch.object(build_mod, "clang_version_text", lambda path: said.get(path, "")):
            self.assertIsNone(build_mod.clang_major(apple))   # not 17
            self.assertTrue(build_mod.is_apple_clang(apple))
            self.assertIsNone(self.with_pass([(apple, None)], 21))
            self.assertIsNone(self.with_pass([(apple, None)], 22))
            self.assertEqual(build_mod.pick_clang([(apple, None), (other, 21)], 21), other)   # the numbered one first
            self.assertEqual(build_mod.pick_clang([(apple, None), (other, 22)], 21), apple)
            hint = build_mod.hint_for(apple, "pass")
            self.assertIn("Apple's clang", hint)
            self.assertIn("LLVM 21", hint)
            self.assertIn("brew install llvm", hint)
            # A program that prints no version at all is not Apple's.
            self.assertIn("gives no version", self.with_pass([(other, None)], 21))

    def test_clang_version_strings(self):
        said = {"a": "clang version 22.1.8 (https://github.com/llvm/llvm-project.git ca79)\nTarget: x86_64-w64-windows-gnu\n",
                "b": "Ubuntu clang version 18.1.3 (1ubuntu1)\nTarget: x86_64-pc-linux-gnu\n",
                "c": "Homebrew clang version 21.1.8\nTarget: arm64-apple-darwin24.0.0\n",
                "d": "Apple clang version 16.0.0 (clang-1600.0.26.6)\n",
                "e": "gcc (Ubuntu 13.2.0) 13.2.0\n"}
        with mock.patch.object(build_mod, "clang_version_text", lambda path: said[path]):
            self.assertEqual([build_mod.clang_major(name) for name in "abcde"], [22, 18, 21, None, None])
            self.assertEqual([build_mod.is_apple_clang(name) for name in "abcde"], [False, False, False, True, False])

    def test_hint_for_a_clang_the_pass_cannot_work_with(self):
        clang = self.program("clang")
        self.pass_major = 21
        with mock.patch.dict(os.environ, {"MEMORIES_GUEST_IR": self.program("memories-guest-ir")}):
            with mock.patch.object(build_mod, "clang_version_text", lambda path: "clang version 22.1.8\n"):
                self.assertIn("newer than the guest pass's LLVM 21", build_mod.hint_for(clang, "pass"))
                self.assertEqual(build_mod.hint_for(clang, "clang"), "")
            with mock.patch.object(build_mod, "clang_version_text", lambda path: "clang version 19.1.0\n"):
                self.assertIn("older than the guest pass's LLVM 21", build_mod.hint_for(clang, "clang"))
                self.assertEqual(build_mod.hint_for(clang, "pass"), "")

    def test_the_pass_says_its_llvm(self):
        """tool_llvm_major asks the pass (--llvm-major); a pass that cannot
        say (an older build, a program that does not run) is None, which
        build_mod.py takes for the game's LLVM."""
        def run(code, out):
            return lambda command, **options: subprocess.CompletedProcess(command, code, out, "")
        for code, out, expected in ((0, "22\n", 22), (0, "21", 21), (1, "", None), (1, "22\n", None),
                                    (0, "LLVM 22", None), (0, "", None)):
            with mock.patch.object(build_mod.subprocess, "run", run(code, out)):
                self.assertEqual(TOOL_LLVM_MAJOR("tool"), expected, (code, out))
        def missing(command, **options):
            raise FileNotFoundError(command[0])
        with mock.patch.object(build_mod.subprocess, "run", missing):
            self.assertIsNone(TOOL_LLVM_MAJOR("tool"))
        tool = self.program("memories-guest-ir")
        for major, expected in ((22, 22), (None, build_mod.GUEST_LLVM_DEFAULT)):
            self.pass_major = major
            self.assertEqual(build_mod.guest_pass_major(tool), expected)

    def test_the_sdk_pass_is_built_for_its_clang(self):
        """tools/pc/sdk_llvm.json pins the LLVM of the SDK's guest pass: the
        major of the clang in the toolchain the SDK names (llvm-mingw)."""
        with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "sdk_llvm.json"), encoding="utf-8") as handle:
            config = json.load(handle)
        self.assertEqual(config["llvm_major"], build_mod.LLVM_MINGW_CLANG)
        self.assertEqual(config["version"].split(".")[0], str(config["llvm_major"]))
        for name, entry in config["platforms"].items():
            self.assertIn(f"/llvmorg-{config['version']}/", entry["url"], name)
            self.assertIn(entry["method"], ("release", "source"), name)
        self.assertEqual(set(config["platforms"]), {"linux", "windows"})

    def test_translated_needs_the_guest_pass(self):
        clang = self.program("clang")
        self.program("ld.lld")
        missing = os.path.join(self.bin, "nowhere", "memories-guest-ir" + EXE)
        with mock.patch.object(build_mod, "translated_candidates", lambda: [(clang, 21)]), \
                mock.patch.dict(os.environ, {"MEMORIES_GUEST_IR": missing}):
            text = build_mod.translation_problem(None)
        self.assertIn("needs the game's guest pass", text)
        self.assertIn(f"MEMORIES_GUEST_IR names {missing}", text)
        self.assertIn("python tools/pc/llvm_guest.py", text)

    def test_translated_not_named_builds_no_pass(self):
        """Not asked for by name, the target takes a guest pass built before
        and never starts llvm_guest.py's build (which may fetch LLVM)."""
        import llvm_guest
        clang = self.program("clang")
        self.program("ld.lld")
        environment = {name: value for name, value in os.environ.items() if name != "MEMORIES_GUEST_IR"}
        with mock.patch.object(build_mod, "translated_candidates", lambda: [(clang, 21)]), \
                mock.patch.dict(os.environ, environment, clear=True), \
                mock.patch.object(llvm_guest, "OUT", pathlib.Path(self.bin)), \
                mock.patch.object(llvm_guest, "compiler", side_effect=AssertionError("built")):
            text = build_mod.translation_problem(None, build_tool=False)
        self.assertIn("it is not built yet in this checkout", text)

    def test_translated_never_downloads(self):
        """No path of build_mod.py fetches LLVM: neither the default one
        nor the target named, where llvm_guest.py would build the pass."""
        import llvm_guest, types
        attempts = []
        stub = types.ModuleType("fetch_tools")
        stub.download = lambda url, digest: attempts.append(url) or (_ for _ in ()).throw(RuntimeError("download"))
        stub.unpack = lambda *arguments: None
        clang = self.program("clang")
        self.program("ld.lld")
        environment = {name: value for name, value in os.environ.items()
                       if name not in ("MEMORIES_GUEST_IR", "MEMORIES_GUEST_LLVM")}
        empty = tempfile.mkdtemp(prefix="build-mod-guest-")
        self.addCleanup(shutil.rmtree, empty, ignore_errors=True)
        llvm_guest.toolchain.cache_clear()
        llvm_guest.compiler.cache_clear()
        self.addCleanup(llvm_guest.toolchain.cache_clear)
        self.addCleanup(llvm_guest.compiler.cache_clear)
        with mock.patch.dict(sys.modules, {"fetch_tools": stub}), \
                mock.patch.object(build_mod, "translated_candidates", lambda: [(clang, 21)]), \
                mock.patch.dict(os.environ, environment, clear=True), \
                mock.patch.object(llvm_guest, "OUT", pathlib.Path(empty)), \
                mock.patch.object(llvm_guest, "ROOT", pathlib.Path(empty)), \
                mock.patch.object(llvm_guest, "FETCH", True):
            for build_tool in (False, True):
                text = build_mod.translation_problem(None, build_tool=build_tool)
                self.assertIn("needs the game's guest pass", text)
        self.assertEqual(attempts, [])

    def test_translated_needs_the_game_tables(self):
        clang = self.program("clang")
        self.program("ld.lld")
        tool = self.program("memories-guest-ir")
        with mock.patch.object(build_mod, "translated_candidates", lambda: [(clang, 21)]), \
                mock.patch.dict(os.environ, {"MEMORIES_GUEST_IR": tool}):
            text = build_mod.translation_problem([self.bin])
        self.assertIn("mod_translation.json, mod_signatures.json", text)
        self.assertIn("--target android-arm64-v8a", text)


if __name__ == "__main__":
    unittest.main()
