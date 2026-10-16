"""Where the editor looks beside its program, and the port's user directory,
on each platform (disc.program_dir, disc.user_dir, disc.candidates)."""
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from fm_editor import disc, validate

APP = "/Users/p/Games/fm-editor.app/Contents/MacOS/fm-editor"


class ProgramDir(unittest.TestCase):
    def run_as(self, platform, executable):
        patches = [mock.patch.object(sys, "platform", platform), mock.patch.object(sys, "executable", executable),
                   mock.patch.object(sys, "frozen", True, create=True)]
        for patch in patches:
            patch.start()
            self.addCleanup(patch.stop)
        return disc.program_dir()

    def test_mac_app_bundle_is_the_folder_holding_the_app(self):
        self.assertEqual(self.run_as("darwin", APP), Path(APP).resolve().parents[3])

    def test_mac_frozen_outside_a_bundle_is_the_executable_folder(self):
        self.assertEqual(self.run_as("darwin", "/opt/fm/fm-editor"), Path("/opt/fm").resolve())

    def test_mac_bundle_shape_needs_the_app_suffix(self):
        self.assertEqual(self.run_as("darwin", "/x/Foo/Contents/MacOS/fm-editor"), Path("/x/Foo/Contents/MacOS").resolve())

    def test_windows_and_linux_frozen_are_the_executable_folder(self):
        self.assertEqual(self.run_as("win32", "C:/g/fm-editor.exe"), Path("C:/g/fm-editor.exe").resolve().parent)
        self.assertEqual(self.run_as("linux", "/g/fm-editor"), Path("/g/fm-editor").resolve().parent)

    def test_linux_even_with_bundle_shaped_path_is_unchanged(self):
        self.assertEqual(self.run_as("linux", APP), Path(APP).resolve().parent)

    def test_source_run_is_the_script_folder(self):
        for platform in ("darwin", "linux", "win32"):
            with mock.patch.object(sys, "platform", platform), mock.patch.object(sys, "argv", ["/src/fm_editor/__main__.py"]):
                self.assertEqual(disc.program_dir(), Path("/src/fm_editor/__main__.py").resolve().parent)

    def test_candidates_find_game_beside_the_app(self):
        with tempfile.TemporaryDirectory() as tmp:
            beside = Path(tmp).resolve()
            executable = str(beside / "fm-editor.app" / "Contents" / "MacOS" / "fm-editor")
            env = {"HOME": tmp, "MEMORIES_USER_DIR": str(beside / "user")}
            with mock.patch.dict(os.environ, env), mock.patch.object(sys, "platform", "darwin"), \
                    mock.patch.object(sys, "frozen", True, create=True), mock.patch.object(sys, "executable", executable):
                found = disc.candidates()
        self.assertIn(beside / "game", found)
        self.assertIn(beside, found)
        self.assertNotIn(Path(executable).parent / "game", found)

    def test_mods_are_found_beside_the_app(self):
        with tempfile.TemporaryDirectory() as tmp:
            beside = Path(tmp).resolve()
            (beside / "mods" / "one").mkdir(parents=True)
            (beside / "mods" / "one" / "mod.json").write_text("{}")
            executable = str(beside / "fm-editor.app" / "Contents" / "MacOS" / "fm-editor")
            env = {"HOME": tmp, "MEMORIES_USER_DIR": str(beside / "user")}
            with mock.patch.dict(os.environ, env), mock.patch.object(sys, "platform", "darwin"), \
                    mock.patch.object(sys, "frozen", True, create=True), mock.patch.object(sys, "executable", executable):
                self.assertEqual(validate.shipped_folder(), beside / "mods")


class UserDir(unittest.TestCase):
    def setUp(self):
        saved = dict(os.environ)
        self.addCleanup(lambda: (os.environ.clear(), os.environ.update(saved)))
        for name in ("MEMORIES_USER_DIR", "XDG_DATA_HOME"):
            os.environ.pop(name, None)
        os.environ["HOME"] = "/home/p"
        os.environ["USERPROFILE"] = "C:/Users/p"

    def where(self, platform):
        with mock.patch.object(sys, "platform", platform), \
                mock.patch.object(disc.os.path, "expanduser", lambda p: os.environ["HOME"] if p == "~" else p):
            return disc.user_dir()

    def test_named_directory_wins_everywhere(self):
        os.environ["MEMORIES_USER_DIR"] = "/named"
        for platform in ("darwin", "linux", "win32"):
            self.assertEqual(self.where(platform), Path("/named"))

    def test_darwin_is_application_support(self):
        self.assertEqual(self.where("darwin"), Path("/home/p/Library/Application Support/YFM Re-Decomp"))

    def test_darwin_ignores_xdg(self):
        os.environ["XDG_DATA_HOME"] = "/xdg"
        self.assertEqual(self.where("darwin"), Path("/home/p/Library/Application Support/YFM Re-Decomp"))

    def test_linux_is_local_share_or_absolute_xdg(self):
        self.assertEqual(self.where("linux"), Path("/home/p/.local/share/YFM Re-Decomp"))
        os.environ["XDG_DATA_HOME"] = "/xdg"
        self.assertEqual(self.where("linux"), Path("/xdg/YFM Re-Decomp"))
        os.environ["XDG_DATA_HOME"] = "relative"
        self.assertEqual(self.where("linux"), Path("/home/p/.local/share/YFM Re-Decomp"))

    def test_windows_is_documents_my_games(self):
        self.assertEqual(self.where("win32").parts[-2:], ("My Games", "YFM Re-Decomp"))


if __name__ == "__main__":
    unittest.main()
