"""WSL launcher integration check using disposable data and no browser windows."""
import hashlib
import json
from pathlib import Path
import secrets
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
HTTP = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    with tempfile.TemporaryDirectory(prefix="club-launcher-") as temporary:
        data = Path(temporary)
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        command = [sys.executable, str(ROOT / "scripts/launch.py"),
                   "--data-dir", str(data), "--port", str(port), "--no-browser"]

        def launch():
            log = (data / "launch.log").open("w+")
            child = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=log,
                                     stderr=subprocess.STDOUT, text=True)
            return child, log

        def await_ready(child, log):
            deadline = time.monotonic() + 60
            while time.monotonic() < deadline:
                if child.poll() is not None:
                    log.seek(0)
                    raise AssertionError("Launcher stopped: " + log.read())
                try:
                    with HTTP.open(f"http://127.0.0.1:{port}/api/auth/status", timeout=0.5) as response:
                        state = json.load(response)
                    if state.get("ok") and state.get("data", {}).get("initialized"):
                        return
                except (OSError, ValueError):
                    pass
                time.sleep(0.1)
            raise AssertionError("Launcher did not become ready")

        def stop(child, log):
            if child.poll() is None:
                child.terminate()
                child.wait(timeout=10)
            if child.stdin:
                child.stdin.close()
            log.close()

        child, log = launch()
        try:
            password = secrets.token_urlsafe(24)
            child.stdin.write(password + "\n" + password + "\n")
            child.stdin.close()
            child.stdin = None
            await_ready(child, log)
            accounts = data / "club_accounts.txt"
            before = hashlib.sha256(accounts.read_bytes()).digest()
            check(password.encode() not in accounts.read_bytes(), "Plaintext password saved")
            record = json.loads((data / ".club-launcher.lock").read_text())
            check(Path(f"/proc/{record['pid']}/cwd").resolve() == data, "Wrong data directory")
            repeated = subprocess.run(command, capture_output=True, text=True, timeout=10)
            check(repeated.returncode == 0 and "已经在运行" in repeated.stdout, "Repeated launch failed")
            check(json.loads((data / ".club-launcher.lock").read_text())["pid"] == record["pid"],
                  "Repeated launch replaced server")
        finally:
            stop(child, log)
        check(child.returncode == 0, "Termination was not clean")
        check(not Path(f"/proc/{record['pid']}").exists(), "Server leaked after launcher stopped")
        check((data / ".club-launcher.lock").read_text() == "", "Stale runtime metadata")

        child, log = launch()
        try:
            child.stdin.close()
            child.stdin = None
            await_ready(child, log)
            check(hashlib.sha256(accounts.read_bytes()).digest() == before, "Restart changed account")
        finally:
            stop(child, log)

        with socket.socket() as occupied:
            occupied.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            occupied.bind(("127.0.0.1", port))
            occupied.listen()
            result = subprocess.run(command, capture_output=True, text=True, timeout=10)
            check(result.returncode == 1 and "已被占用" in result.stderr, "Occupied port not rejected")

        stop_command = command + ["--stop"]

        def wait_stopped(pid):
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                if not Path(f"/proc/{pid}").exists() and (data / ".club-launcher.lock").read_text() == "":
                    return
                time.sleep(0.1)
            raise AssertionError("Background service or runtime record leaked")

        for _ in range(2):
            result = subprocess.run(command + ["--background"], input="", capture_output=True,
                                    text=True, timeout=30)
            check(result.returncode == 0 and "转入后台" in result.stdout, "Background launch did not return")
            background_record = json.loads((data / ".club-launcher.lock").read_text())
            try:
                with HTTP.open(f"http://127.0.0.1:{port}/api/auth/status", timeout=2) as response:
                    check(json.load(response)["data"]["initialized"], "Service died with starter")
                duplicate = subprocess.run(command + ["--background"], capture_output=True, text=True, timeout=10)
                check(duplicate.returncode == 0 and "已经在运行" in duplicate.stdout, "Background reuse failed")
                check(json.loads((data / ".club-launcher.lock").read_text())["pid"] == background_record["pid"],
                      "Repeated background launch replaced server")
                check(hashlib.sha256(accounts.read_bytes()).digest() == before, "Background start changed account")
                check((data / ".club-service.log").stat().st_size < 16384, "Unexpected log growth")
            finally:
                stopped = subprocess.run(stop_command, capture_output=True, text=True, timeout=15)
                check(stopped.returncode == 0 and "已停止" in stopped.stdout, "Stop command failed")
            wait_stopped(background_record["pid"])
        stopped = subprocess.run(stop_command, capture_output=True, text=True, timeout=10)
        check(stopped.returncode == 0 and "未运行" in stopped.stdout, "Repeated stop failed")

        unrelated = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)"], cwd=data)
        try:
            stat = Path(f"/proc/{unrelated.pid}/stat").read_text().rsplit(")", 1)[1].split()[19]
            (data / ".club-launcher.lock").write_text(json.dumps({"pid": unrelated.pid, "start": stat,
                "exe": record["exe"], "port": port}))
            stale_stop = subprocess.run(stop_command, capture_output=True, text=True, timeout=10)
            check(stale_stop.returncode == 0 and unrelated.poll() is None, "Stopped an unrelated process")
        finally:
            unrelated.terminate()
            unrelated.wait(timeout=5)

        accounts.write_text("damaged account fixture\n")
        result = subprocess.run(command, input="", capture_output=True, text=True, timeout=30)
        check(result.returncode == 1, "Damaged accounts did not stop startup")
        check(accounts.read_text() == "damaged account fixture\n", "Damaged accounts overwritten")
        result = subprocess.run(command + ["--background"], input="", capture_output=True, text=True, timeout=30)
        check(result.returncode == 1 and "后台启动失败" in result.stderr, "Background failure not reported")
        check(accounts.read_text() == "damaged account fixture\n", "Background overwrote damaged accounts")
    print("LAUNCHER CHECKS PASSED: setup, reuse, shutdown, restart, occupied port, corrupt data, "
          "background lifetime, repeat stop, process identity, background failure")


if __name__ == "__main__":
    main()
