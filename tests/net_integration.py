#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path


RUNNER = r"""
import os
import subprocess
import sys
import time
from pathlib import Path

main_binary = Path(os.environ["CNEGATIVE_NET_MAIN_BINARY"])
server_binary = Path(os.environ["CNEGATIVE_NET_SERVER_BINARY"])
client_binary = Path(os.environ["CNEGATIVE_NET_CLIENT_BINARY"])
repetitions = int(os.environ["CNEGATIVE_NET_REPETITIONS"])
settle_seconds = float(os.environ["CNEGATIVE_NET_SETTLE_SECONDS"])
timeout_seconds = float(os.environ["CNEGATIVE_NET_TIMEOUT_SECONDS"])
base_port = int(os.environ["CNEGATIVE_NET_BASE_PORT"])
port_attempts = int(os.environ["CNEGATIVE_NET_PORT_ATTEMPTS"])

main_result = subprocess.run(
    [str(main_binary)],
    text=True,
    capture_output=True,
    check=False,
)
print(f"compile_only_exit={main_result.returncode}")
if main_result.returncode != 0:
    if main_result.stdout:
        sys.stderr.write(main_result.stdout)
    if main_result.stderr:
        sys.stderr.write(main_result.stderr)
    raise SystemExit(main_result.returncode)

for run_index in range(repetitions):
    # FIX: wait between repetitions so the OS can reclaim ports from
    # TIME_WAIT before we try to bind the next one.
    if run_index > 0:
        time.sleep(2.0)

    run_ok = False
    for attempt_index in range(port_attempts):
        port = base_port + (run_index * port_attempts) + attempt_index
        process_env = dict(os.environ)
        process_env["CNEGATIVE_NET_TEST_PORT"] = str(port)

        server_log = Path(f"/tmp/cnegative-net-server-{run_index}-{attempt_index}.log")
        client_log = Path(f"/tmp/cnegative-net-client-{run_index}-{attempt_index}.log")
        with server_log.open("wb") as server_out, client_log.open("wb") as client_out:
            server_proc = subprocess.Popen(
                [str(server_binary)],
                stdout=server_out,
                stderr=subprocess.STDOUT,
                env=process_env,
            )
            time.sleep(settle_seconds)
            client_proc = subprocess.Popen(
                [str(client_binary)],
                stdout=client_out,
                stderr=subprocess.STDOUT,
                env=process_env,
            )

            # FIX: wait for client and server independently, each with the
            # full timeout. Previously, server_proc.wait() was called after
            # client_proc.wait() already consumed most of the timeout window,
            # leaving the server almost no time before being killed.
            client_exit = None
            server_exit = None
            deadline = time.monotonic() + timeout_seconds
            try:
                client_exit = client_proc.wait(timeout=timeout_seconds)
            except subprocess.TimeoutExpired:
                client_proc.kill()
                client_proc.wait()
                client_exit = 124

            remaining = max(0.0, deadline - time.monotonic())
            try:
                server_exit = server_proc.wait(timeout=remaining if remaining > 0 else timeout_seconds)
            except subprocess.TimeoutExpired:
                server_proc.kill()
                server_proc.wait()
                server_exit = 124

        print(f"run={run_index + 1} port={port} client_exit={client_exit} server_exit={server_exit}")
        if client_exit == 0 and server_exit == 0:
            run_ok = True
            break

        client_text = client_log.read_text(encoding="utf-8")
        server_text = server_log.read_text(encoding="utf-8")
        if client_text:
            print("client_log:")
            print(client_text, end="")
        if server_text:
            print("server_log:")
            print(server_text, end="")
        time.sleep(1.0)

    if not run_ok:
        raise SystemExit(1)
"""


def main() -> int:
    parser = argparse.ArgumentParser(description="Optional Python-based blocking network integration test for cnegative.")
    parser.add_argument("--main-binary", default="build/net-main-python", help="Path to the already-built compile-only networking binary.")
    parser.add_argument("--server-binary", default="build/net-server-python", help="Path to the already-built networking server binary.")
    parser.add_argument("--client-binary", default="build/net-client-python", help="Path to the already-built networking client binary.")
    parser.add_argument("--repetitions", type=int, default=5, help="How many server/client runs to perform.")
    parser.add_argument("--settle-seconds", type=float, default=5.0, help="Seconds to wait after starting the server.")
    parser.add_argument("--timeout-seconds", type=float, default=15.0, help="Timeout for each client/server process.")
    parser.add_argument("--base-port", type=int, default=34567, help="First loopback port to use for the integration runs.")
    parser.add_argument("--port-attempts", type=int, default=5, help="How many sequential ports to try per repetition before failing.")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    os.chdir(repo_root)

    for binary_path in (Path(args.main_binary), Path(args.server_binary), Path(args.client_binary)):
        if not binary_path.exists():
            raise SystemExit(f"expected built binary before running net integration: {binary_path}")

    runner_env = dict(os.environ)
    runner_env["CNEGATIVE_NET_MAIN_BINARY"] = args.main_binary
    runner_env["CNEGATIVE_NET_SERVER_BINARY"] = args.server_binary
    runner_env["CNEGATIVE_NET_CLIENT_BINARY"] = args.client_binary
    runner_env["CNEGATIVE_NET_REPETITIONS"] = str(args.repetitions)
    runner_env["CNEGATIVE_NET_SETTLE_SECONDS"] = str(args.settle_seconds)
    runner_env["CNEGATIVE_NET_TIMEOUT_SECONDS"] = str(args.timeout_seconds)
    runner_env["CNEGATIVE_NET_BASE_PORT"] = str(args.base_port)
    runner_env["CNEGATIVE_NET_PORT_ATTEMPTS"] = str(args.port_attempts)

    completed = subprocess.run([sys.executable, "-c", RUNNER], env=runner_env, check=False)
    return int(completed.returncode)


if __name__ == "__main__":
    raise SystemExit(main())
