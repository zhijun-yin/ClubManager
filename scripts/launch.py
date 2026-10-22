#!/usr/bin/env python3
"""Double-click entry: build, initialize locally, open Windows browser, serve."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import select
import shutil
import signal
import socket
import subprocess
import sys
import time
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
HTTP = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def ready(port):
    try:
        with HTTP.open(f"http://127.0.0.1:{port}/api/auth/status", timeout=1) as response:
            result = json.load(response)
        return result.get("ok") is True and result.get("data", {}).get("initialized") is True
    except (OSError, ValueError, AttributeError):
        return False


def process_start(pid):
    # Field 22, with the parenthesized process name removed first.
    return Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()[19]


def active_record(lock, data_dir):
    try:
        lock.seek(0)
        record = json.load(lock)
        pid = int(record["pid"])
        if (process_start(pid) != record["start"]
                or Path(f"/proc/{pid}/cwd").resolve() != data_dir
                or str(Path(f"/proc/{pid}/exe").resolve()).removesuffix(" (deleted)") != record["exe"]
                or not 1 <= int(record["port"]) <= 65535):
            return None
        return record
    except (OSError, ValueError, KeyError, TypeError, IndexError):
        return None


def open_page(port, no_browser):
    url = f"http://127.0.0.1:{port}/"
    print(f"\n网页地址：{url}", flush=True)
    if no_browser:
        return
    powershell = shutil.which("powershell.exe")
    if not powershell:
        print("无法自动调用 Windows 浏览器，请手动打开上面的地址。", flush=True)
        return
    try:
        # The URL contains only our fixed localhost prefix and a validated integer.
        subprocess.run([powershell, "-NoLogo", "-NoProfile", "-NonInteractive",
                        "-Command", f"Start-Process '{url}'"],
                       check=True, timeout=15, stdin=subprocess.DEVNULL)
    except (OSError, subprocess.SubprocessError):
        print("浏览器未能自动打开，服务仍在运行；请手动打开上面的地址。", flush=True)


def interrupted(_signum, _frame):
    raise KeyboardInterrupt


def stop_service(lock, data_dir):
    record = active_record(lock, data_dir)
    if not record:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError("项目正在初始化或启动，尚未生成有效服务记录；请在启动窗口按 Ctrl+C 取消。")
        lock.seek(0)
        lock.truncate()
        print("服务未运行，无需停止。", flush=True)
        return 0
    # Pin the process before rechecking its identity. A recycled PID must never
    # let the stop shortcut signal an unrelated application.
    try:
        process_fd = os.pidfd_open(record["pid"])
    except ProcessLookupError:
        print("服务已停止。", flush=True)
        return 0
    try:
        current = active_record(lock, data_dir)
        if current != record:
            raise RuntimeError("服务状态发生变化，本次没有停止任何进程；请重试。")
        signal.pidfd_send_signal(process_fd, signal.SIGTERM)
        if not select.select([process_fd], [], [], 10)[0]:
            raise RuntimeError("服务未在 10 秒内退出；请检查运行日志。本次未强制结束其他进程。")
    finally:
        os.close(process_fd)
    print("社团管理系统已停止，账号和业务数据已保留。", flush=True)
    return 0


def serve(executable, data_dir, port, lock, on_ready):
    server = None
    try:
        env = os.environ.copy()
        for name in ("CLUB_LAN", "CLUB_SECURE_COOKIE"):
            env.pop(name, None)
        env.update(CLUB_NO_BROWSER="1", CLUB_PORT=str(port))
        server = subprocess.Popen([str(executable), "gui"], cwd=data_dir,
                                  env=env, stdin=subprocess.DEVNULL)
        lock.seek(0)
        lock.truncate()
        json.dump({"pid": server.pid, "start": process_start(server.pid),
                   "exe": str(executable), "port": port}, lock)
        lock.flush()
        deadline = time.monotonic() + 30
        while True:
            if server.poll() is not None:
                raise RuntimeError("服务启动失败；原数据和账号未重置。")
            if ready(port):
                break
            if time.monotonic() >= deadline:
                raise RuntimeError("等待服务启动超时。")
            time.sleep(0.2)
        on_ready()
        if server.wait() not in (0, -signal.SIGTERM, -signal.SIGINT):
            raise RuntimeError("服务已异常退出，请查看运行日志。")
        return 0
    finally:
        if server is not None and server.poll() is None:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()
        lock.seek(0)
        lock.truncate()
        lock.flush()


def serve_background(executable, data_dir, args, lock):
    # The child inherits the existing flock. It owns and reaps the service, while
    # the Windows console can exit after receiving a real readiness notification.
    reader, writer = os.pipe()
    sys.stdout.flush()
    sys.stderr.flush()
    if os.fork():
        os.close(writer)
        with os.fdopen(reader, "r", encoding="utf-8") as status:
            message = status.readline().strip()
        if message != "READY":
            raise RuntimeError(message or "后台启动失败，请查看 .club-service.log。")
        open_page(args.port, args.no_browser)
        print("服务已转入后台，可以关闭启动窗口。双击“停止社团管理系统”结束服务。", flush=True)
        return 0

    os.close(reader)
    exit_code = 1
    notified = False

    def notify_ready():
        nonlocal notified
        os.write(writer, b"READY\n")
        os.close(writer)
        notified = True

    try:
        os.setsid()
        os.chdir(data_dir)
        # Overwrite one small log per start, rather than accumulating dated logs.
        with open(os.devnull, "rb") as null, (data_dir / ".club-service.log").open("wb") as log:
            os.dup2(null.fileno(), 0)
            os.dup2(log.fileno(), 1)
            os.dup2(log.fileno(), 2)
        exit_code = serve(executable, data_dir, args.port, lock, notify_ready)
    except KeyboardInterrupt:
        exit_code = 0
    except BaseException as error:
        print(f"后台服务退出：{error}", file=sys.stderr, flush=True)
    finally:
        if not notified:
            os.write(writer, "后台启动失败，请查看项目目录中的 .club-service.log。\n".encode())
            os.close(writer)
        sys.stdout.flush()
        sys.stderr.flush()
        os._exit(exit_code)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-dir", type=Path, default=ROOT)
    parser.add_argument("--port", type=int, default=8090)
    parser.add_argument("--no-browser", action="store_true", help="For terminal/testing use")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--background", action="store_true", help="Detach after initialization and readiness")
    mode.add_argument("--stop", action="store_true", help="Stop only the verified service for this data directory")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("port must be between 1 and 65535")
    data_dir = args.data_dir.resolve(strict=True)
    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGHUP, interrupted)
    # Keep the inode, including after exit, so simultaneous launchers cannot lock
    # different files. Only this small, non-secret runtime record is retained.
    with (data_dir / ".club-launcher.lock").open("a+", encoding="utf-8") as lock:
        if args.stop:
            return stop_service(lock, data_dir)
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            record = active_record(lock, data_dir)
            if record and ready(record["port"]):
                print("项目已经在运行，打开现有网页。", flush=True)
                open_page(record["port"], args.no_browser)
            else:
                print("项目正在另一个启动窗口中准备，请回到那个窗口完成初始化或等待启动。", flush=True)
            return 0

        # Clear metadata from a previous, already stopped run before preparing.
        lock.seek(0)
        lock.truncate()
        lock.flush()
        # Fail before initialization if another service owns the desired port.
        with socket.socket() as probe:
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                probe.bind(("127.0.0.1", args.port))
            except OSError as error:
                raise RuntimeError(f"端口 {args.port} 已被占用。请先关闭原服务窗口，再双击启动。") from error

        print("正在准备社团管理系统（WSL）……", flush=True)
        subprocess.run(["bash", str(ROOT / "scripts/run-linux.sh"), "build"],
                       cwd=ROOT, check=True)
        build = Path((ROOT / "build/wsl-build-path.txt").read_text().strip())
        executable = (build / "club").resolve(strict=True)
        if not (data_dir / "club_accounts.txt").exists():
            print("\n首次使用：管理员账号为 admin。请设置密码并再输入一次（至少 10 字节，输入时不显示）。",
                  flush=True)
            subprocess.run([str(executable), "auth", "init", "admin"],
                           cwd=data_dir, check=True)

        if args.background:
            return serve_background(executable, data_dir, args, lock)

        def foreground_ready():
            open_page(args.port, args.no_browser)
            print("\n启动成功。使用期间请保留这个窗口；按 Ctrl+C 停止服务。", flush=True)

        return serve(executable, data_dir, args.port, lock, foreground_ready)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n服务已停止。", flush=True)
        sys.exit(0)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"\n启动失败：{error}", file=sys.stderr, flush=True)
        sys.exit(1)
