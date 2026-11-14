#!/usr/bin/env python3
"""Board test for the debug CLI (issue #51).

Talks to a flashed board over its debug UART and checks what the unit tests
cannot: every command answers as documented, the uptime clock is right, the
CLI survives an overlong line and a burst of random bytes, and the worst
command handler time is measured.

    tools/cli_board_test.py                      # /dev/ttyACM0, 115200 baud
    tools/cli_board_test.py --port /dev/ttyUSB0 --uptime-seconds 10

Close any other program holding the port (picocom, a serial monitor) first.
Needs pyserial (pip install pyserial). Exit status is non-zero if any check
fails. Record the printed values in the pull request, not only pass or fail.
"""

import argparse
import os
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is not installed: pip install pyserial")

REFUSED = "ERR refused: vehicle not in safe state"
UNKNOWN = "ERR unknown command, try help"

# Command -> accepted last line of its reply. `set` exists in Debug builds
# only, and `boot` has no report on a build without one.
EXPECTED = {
    "help": ["OK"],
    "version": ["OK"],
    "uptime": ["OK"],
    "log": ["OK"],
    "boot": ["OK", "ERR unavailable: no boot report"],
    "health": ["OK"],
    "state": ["ERR unavailable: needs #25"],
    "faults": ["ERR unavailable: needs #59"],
    "faults clear": [REFUSED],
    "cal": ["ERR unavailable: needs #61"],
    "cal set x 1": [REFUSED],
    "selftest": ["ERR unavailable: needs #50"],
    "set test 1": [REFUSED, UNKNOWN],
    "nope": [UNKNOWN],
}

failures = 0


def check(name, passed, detail=""):
    global failures
    if not passed:
        failures += 1
    print(f"{'PASS' if passed else 'FAIL'}  {name}{'  ' + detail if detail else ''}")


def wait_quiet(port, quiet_s=1.0, limit_s=60.0):
    """Read and discard until the board has sent nothing for quiet_s."""
    end = time.time() + limit_s
    last = time.time()
    while time.time() < end and time.time() - last < quiet_s:
        if port.read(256):
            last = time.time()


def sync(port):
    """Clear the board's line and everything it has already sent us."""
    wait_quiet(port)
    port.write(b"\x03\r")
    wait_quiet(port, quiet_s=0.5)


def cmd(port, line, wait_s=3.0):
    """Send one command; return its reply lines, ending in OK / ERR ... ."""
    port.write(line.encode() + b"\r")
    out = []
    end = time.time() + wait_s
    while time.time() < end:
        text = port.readline().decode(errors="replace").strip()
        if not text:
            continue
        out.append(text)
        if text == "OK" or text.startswith("ERR "):
            return out
    return out + ["TIMEOUT"]


def value(lines, key):
    """Find key=value in a reply; return the value as text, or None."""
    for text in lines:
        for word in text.split():
            if word.startswith(key + "="):
                return word[len(key) + 1:]
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--uptime-seconds", type=int, default=60)
    parser.add_argument("--noise-bytes", type=int, default=10240)
    args = parser.parse_args()

    port = serial.Serial(args.port, args.baud, timeout=0.2)
    sync(port)

    print("-- every command answers as documented")
    for line, accepted in EXPECTED.items():
        last = cmd(port, line)[-1]
        check(line, last in accepted, last)
    reply = cmd(port, "version")
    print("    " + " ".join(reply[1:-1]))

    print(f"-- uptime over {args.uptime_seconds} s")
    started = time.time()
    first = value(cmd(port, "uptime"), "uptime_ms")
    time.sleep(args.uptime_seconds)
    second = value(cmd(port, "uptime"), "uptime_ms")
    elapsed_ms = (time.time() - started) * 1000.0
    if first is None or second is None:
        check("uptime", False, "no uptime_ms in the reply")
    else:
        delta = int(second) - int(first)
        # 1 % for the board's internal oscillator, 100 ms for the serial link.
        tolerance = elapsed_ms * 0.01 + 100.0
        check("uptime", abs(delta - elapsed_ms) <= tolerance,
              f"board {delta} ms, laptop {elapsed_ms:.0f} ms")

    print("-- overlong line, then random bytes")
    last = cmd(port, "x" * 200)[-1]
    check("200-character line", last == "ERR line too long (max 79)", last)
    port.write(os.urandom(args.noise_bytes))
    port.flush()
    sync(port)
    reply = cmd(port, "help")
    check(f"help after {args.noise_bytes} random bytes", reply[-1] == "OK",
          f"echo {reply[0]!r}, last {reply[-1]!r}")

    print("-- every command 10 times")
    timeouts = 0
    wrong = 0
    for _ in range(10):
        for line, accepted in EXPECTED.items():
            reply = cmd(port, line)
            timeouts += reply[-1] == "TIMEOUT"
            if reply[-1] not in accepted:
                wrong += 1
                if wrong <= 5:
                    print(f"    sent {line!r}, got {reply!r}")
    check("140 commands", timeouts == 0 and wrong == 0,
          f"{timeouts} timeouts, {wrong} wrong replies")

    reply = cmd(port, "health")
    check("health", reply[-1] == "OK", reply[-1])
    print("-- measured values")
    for key in ("worst_handler_us", "worst_handler", "rx_dropped", "tx_dropped",
                "cli_stack_free_words", "idle_count"):
        print(f"    {key}={value(reply, key)}")

    print(f"\n{'ALL PASSED' if failures == 0 else str(failures) + ' FAILED'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
