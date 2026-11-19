"""Runs the Archipelago loop test bench (loop_test.exe) on a new seed.

    python mod/tests/run_loop_test.py [Archipelago folder]

The Archipelago folder is either an installed Archipelago (ArchipelagoGenerate.exe, default
C:\\ProgramData\\Archipelago) or a source checkout (Generate.py, used by the CI). The installed
bioshock_infinite world is used: install it again after changing it (build.ps1 -Install).

1. generates a seed with tests/players/*.yaml;
2. starts a test Archipelago server on port 38282;
3. connects a fake DeathLink player (slot Finder): it writes down each death of Booker received
   and answers with its own;
4. runs loop_test.exe (built by build.ps1, or: cmake --build mod/build --config Release --target loop_test);
5. stops everything. Exit code: the test's.

Needs the "websockets" Python package (pip install websockets).
"""

import asyncio
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time

import websockets

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
# Absolute: the Archipelago tools run with it as working directory (the CI passes a relative path).
ARCHIPELAGO = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else r"C:\ProgramData\Archipelago")
PORT = 38282
TEST_EXE = os.path.join(ROOT, "mod", "bin", "tests", "Release", "loop_test.exe")
MACHINES = os.path.join(ROOT, "apworld", "bioshock_infinite", "machines.py")


def archipelago_command(tool: str) -> list[str]:
    """Command line of an Archipelago tool (Generate, Server): installed exe or source script."""
    exe = os.path.join(ARCHIPELAGO, f"Archipelago{tool}.exe")
    if os.path.exists(exe):
        return [exe]
    script = {"Generate": "Generate.py", "Server": "MultiServer.py"}[tool]
    return [sys.executable, os.path.join(ARCHIPELAGO, script)]


async def deathlink_player(log_path: str, stop: threading.Event) -> None:
    """The Finder slot, subscribed to DeathLink: writes down Booker's deaths and sends its own back."""
    async with websockets.connect(f"ws://localhost:{PORT}", max_size=None) as ws:
        await ws.recv()  # RoomInfo
        await ws.send(
            json.dumps(
                [
                    {
                        "cmd": "Connect",
                        "game": "ChecksFinder",
                        "name": "Finder",
                        "password": "",
                        "uuid": "looptest-finder",
                        "items_handling": 0,
                        "tags": ["DeathLink"],
                        "version": {"major": 0, "minor": 6, "build": 7, "class": "Version"},
                        "slot_data": False,
                    }
                ]
            )
        )
        while not stop.is_set():
            try:
                packets = json.loads(await asyncio.wait_for(ws.recv(), 0.5))
            except TimeoutError:
                continue
            for p in packets:
                data = p.get("data", {})
                if p.get("cmd") == "Bounced" and "DeathLink" in p.get("tags", []) and data.get("source") == "Booker":
                    with open(log_path, "a", encoding="utf-8") as f:
                        f.write(data.get("cause", "") + "\n")
                    await asyncio.sleep(1)
                    await ws.send(
                        json.dumps(
                            [
                                {
                                    "cmd": "Bounce",
                                    "tags": ["DeathLink"],
                                    "data": {
                                        "time": time.time(),
                                        "source": "Finder",
                                        "cause": "Finder clicked a bomb.",
                                    },
                                }
                            ]
                        )
                    )


def main() -> int:
    if not os.path.exists(TEST_EXE):
        print(f"{TEST_EXE} not found: build the loop_test target first.")
        return 2
    work = tempfile.mkdtemp(prefix="bsap_looptest_")
    code = run(work)
    if code == 0:
        shutil.rmtree(work, ignore_errors=True)
    else:
        print(f"The seed and the server log are kept in {work}")
    return code


def run(work: str) -> int:
    """Generates the seed in work, starts the server and the fake player, runs the test."""
    generate = subprocess.run(
        archipelago_command("Generate")
        + [f"--player_files_path={os.path.join(HERE, 'players')}", f"--outputpath={work}"],
        cwd=ARCHIPELAGO,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
    )
    if generate.returncode != 0:
        print(generate.stdout)
        print(f"The seed generation failed (exit code {generate.returncode}).")
        return 2
    multidata = glob.glob(os.path.join(work, "AP_*.zip"))[0]
    server_log_path = os.path.join(work, "server.txt")
    with open(server_log_path, "w", encoding="utf-8", errors="replace") as server_log:
        server = subprocess.Popen(
            archipelago_command("Server") + ["--port", str(PORT), multidata],
            cwd=ARCHIPELAGO,
            stdin=subprocess.PIPE,
            stdout=server_log,
            stderr=subprocess.STDOUT,
        )
        try:
            for _ in range(240):  # the server loads every world before listening
                time.sleep(0.5)
                with open(server_log_path, encoding="utf-8", errors="replace") as f:
                    if "server listening" in f.read():
                        break
                if server.poll() is not None:
                    break
            if server.poll() is not None:  # e.g. port already taken: we would test another server
                print(f"The test server didn't start (port {PORT} already taken?). See {server_log_path}")
                return 2
            death_log = os.path.join(work, "deathlink.txt")
            open(death_log, "w").close()
            # Lines the test appends to this file: commands for the server console.
            server_cmds = os.path.join(work, "server_cmds.txt")
            open(server_cmds, "w").close()

            def forward_commands() -> None:
                done = 0
                while server.poll() is None:
                    with open(server_cmds, encoding="utf-8") as f:
                        lines = f.read().splitlines()
                    for line in lines[done:]:
                        server.stdin.write((line + "\n").encode())
                        server.stdin.flush()
                    done = len(lines)
                    time.sleep(0.3)

            threading.Thread(target=forward_commands, daemon=True).start()
            stop = threading.Event()
            player = threading.Thread(target=lambda: asyncio.run(deathlink_player(death_log, stop)), daemon=True)
            player.start()
            try:
                return subprocess.run(
                    [TEST_EXE, f"Booker@localhost:{PORT}", MACHINES, server_log_path, death_log, server_cmds]
                ).returncode
            finally:
                stop.set()
                player.join(5)
        finally:
            server.kill()


if __name__ == "__main__":
    sys.exit(main())
