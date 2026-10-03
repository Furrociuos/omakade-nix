"""Exercise real startup/IPC ownership without touching desktop services."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest


BINARY = str(Path(sys.argv.pop(1)).resolve())


class GameModeStartupTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="omakade-gm-startup-")
        root = Path(self.directory.name)
        self.state = root / "state/omakade/game-mode.json"
        self.env = os.environ.copy()
        self.env.update({
            "HOME": str(root),
            "TMPDIR": str(root),
            "XDG_CONFIG_HOME": str(root / "config"),
            "XDG_DATA_HOME": str(root / "data"),
            "XDG_STATE_HOME": str(root / "state"),
            "XDG_CACHE_HOME": str(root / "cache"),
            "XDG_RUNTIME_DIR": str(root / "runtime"),
            "QT_QPA_PLATFORM": "offscreen",
            "QT_QPA_PLATFORMTHEME": "",
            "QT_STYLE_OVERRIDE": "Fusion",
            "QT_QUICK_BACKEND": "software",
            "HYPRLAND_INSTANCE_SIGNATURE": "",
            "DBUS_SESSION_BUS_ADDRESS": "unix:path=" + str(root / "no-bus"),
            "NO_AT_BRIDGE": "1",
            "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT": "0xffff/0xffff",
        })
        (root / "runtime").mkdir(mode=0o700)
        tools = root / "tools"
        tools.mkdir()
        for name in ("hyprctl", "pactl", "omarchy-shell"):
            stub = tools / name
            stub.write_text("#!/bin/sh\nexit 1\n")
            stub.chmod(0o755)
        self.env["PATH"] = str(tools) + os.pathsep + self.env["PATH"]
        config = root / "config/omakade"
        config.mkdir(parents=True)
        (config / "game-mode.json").write_text(json.dumps({"silence_notifications": False}))
        self.log = open(root / "app.log", "w+")
        self.primary = None

    def tearDown(self):
        if self.primary is not None and self.primary.poll() is None:
            self.primary.terminate()
            try:
                self.primary.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.primary.kill()
                self.primary.wait(timeout=5)
        self.log.close()
        self.directory.cleanup()

    def wait_for(self, predicate, message):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            if predicate():
                return
            if self.primary.poll() is not None:
                break
            time.sleep(0.02)
        self.log.flush()
        self.log.seek(0)
        self.fail(message + "\n" + self.log.read()[-4000:])

    def launch(self, *arguments):
        self.primary = subprocess.Popen(
            [BINARY, "--demo", *arguments], env=self.env,
            stdout=self.log, stderr=subprocess.STDOUT,
        )

    def command(self, argument):
        result = subprocess.run([BINARY, argument], env=self.env,
                                capture_output=True, text=True, timeout=8)
        self.assertEqual(result.returncode, 0, result.stderr)

    def entered(self):
        def owned():
            try:
                return json.loads(self.state.read_text())["owner_pid"] == self.primary.pid
            except (OSError, ValueError, KeyError):
                return False
        self.wait_for(owned, "Game Mode did not start in the primary instance")

    def assert_temporary_launch_closes(self, start, leave):
        self.launch(start)
        self.entered()
        self.command(leave)
        try:
            code = self.primary.wait(timeout=8)
        except subprocess.TimeoutExpired:
            self.fail("Leaving Game Mode kept its temporary Omakade instance open")
        self.assertEqual(code, 0)
        self.assertFalse(self.state.exists(), "Desktop recovery state was not cleared")

    def test_two_toggles_close_a_cold_launch(self):
        self.assert_temporary_launch_closes("--game-mode-toggle", "--game-mode-toggle")

    def test_explicit_game_mode_launch_closes_on_exit(self):
        self.assert_temporary_launch_closes("--game-mode", "--game-mode-exit")

    def assert_existing_instance_stays_open(self, *arguments):
        self.launch(*arguments)
        socket = Path(self.env["TMPDIR"]) / f"omakade-{os.getuid()}"
        self.wait_for(socket.exists, "Primary instance did not claim its IPC socket")
        for _ in range(2):
            self.command("--game-mode-toggle")
            self.entered()
            self.command("--game-mode-toggle")
            self.wait_for(lambda: not self.state.exists(), "Game Mode did not leave")
            self.assertIsNone(self.primary.poll(), "Existing Omakade instance was closed")
        self.command("--quit")
        self.assertEqual(self.primary.wait(timeout=8), 0)

    def test_existing_desktop_instance_stays_open(self):
        self.assert_existing_instance_stays_open()

    def test_existing_couch_instance_stays_open(self):
        self.assert_existing_instance_stays_open("--couch")


if __name__ == "__main__":
    unittest.main()
