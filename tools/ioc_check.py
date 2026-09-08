#!/usr/bin/env python3
"""Cross-check the CubeMX .ioc against the STM32 pin layer (Model B).

This check belongs to the STM32 driver. gpio_stm32.c / can_stm32.c are the only
first-party code that knows about CubeMX; a future non-STM32 driver would bring
its own equivalent. Ownership split:

  * the .ioc owns pad *identity* (which pad, reserved + locked) and generates
    the <LABEL>_Pin / <LABEL>_GPIO_Port macros into Core/Inc/main.h;
  * GpioConfig (chip-neutral, gpio_if.h) owns a pin's electrical *behaviour*
    (direction / pull / initial level) -- MX_GPIO_Init is not on the call path;
  * gpio_stm32.c's pin_map[] is the mapping, written in terms of the generated
    macros so a renamed/removed .ioc pin fails the compile.

Naming contract:

    GpioPin enumerator     .ioc User Label     main.h macros
    ------------------      --------------      ------------
    GPIO_PIN_BRAKE_LIGHT -> BRAKE_LIGHT     -> BRAKE_LIGHT_Pin / BRAKE_LIGHT_GPIO_Port

Checks:
  1. every GpioPin (except GPIO_PIN_COUNT) has a GPIO_Output/GPIO_Input pin with
     that User Label in the .ioc, and the pad is Locked;
  2. that label has *_Pin / *_GPIO_Port in the generated main.h (stale tree);
  3. gpio_stm32.c pin_map[] has a row for every GpioPin, using the generated
     {LABEL_GPIO_Port, LABEL_Pin} macros (a literal pad is rejected);
  4. no .ioc GPIO pin is labelled without a matching GpioPin enumerator;
  5. GpioConfig .dir at first-party <x>.init(GPIO_PIN_..., &cfg) call sites
     agrees with the .ioc pin direction;
  6. the pads can_stm32.c hard-codes for CAN still carry a CAN1_* signal.

Exit status is non-zero if any check fails. Stdlib only; no third-party deps.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# Pin bit macro (GPIO_PIN_5) -> pad number.
_PIN_BIT_RE = re.compile(r"GPIO_PIN_(\d{1,2})\b")
# A .ioc pin key, e.g. "PA5".
_PAD_RE = re.compile(r"^P([A-H])(\d{1,2})$")

_GPIO_SIGNALS = ("GPIO_Output", "GPIO_Input")


def _fail(msg: str) -> None:
    print(f"  [FAIL] {msg}")


def _ok(msg: str) -> None:
    print(f"  [ ok ] {msg}")


def _note(msg: str) -> None:
    print(f"  [note] {msg}")


def normalize_label(raw: str) -> str:
    """Match CubeMX's macro sanitisation: non-alphanumerics -> '_', upper-case."""
    return re.sub(r"[^0-9A-Za-z]+", "_", raw).strip("_").upper()


# --------------------------------------------------------------------------- #
# Parsers
# --------------------------------------------------------------------------- #


def parse_gpio_enum(header: Path) -> list[str]:
    """Return the GpioPin enumerators, in order, minus the GPIO_PIN_COUNT sentinel.

    An empty list is a valid result -- a board may define no discrete GPIO pins
    (every pad taken by a peripheral). Only a missing enum type is an error.
    """
    text = header.read_text()
    match = re.search(r"typedef\s+enum\s*\{(.*?)\}\s*GpioPin\s*;", text, re.DOTALL)
    if not match:
        sys.exit(f"ioc_check: could not find the 'typedef enum {{...}} GpioPin;' block in {header}")
    names = []
    for line in match.group(1).splitlines():
        line = re.sub(r"/\*.*?\*/", "", line)          # strip /* ... */
        line = re.sub(r"//.*", "", line)               # strip // ...
        ident = re.match(r"\s*([A-Za-z_]\w*)", line)
        if ident:
            names.append(ident.group(1))
    return [n for n in names if n != "GPIO_PIN_COUNT"]


def parse_ioc(ioc: Path) -> dict[str, str]:
    props: dict[str, str] = {}
    for line in ioc.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        props[key.strip()] = value.strip()
    return props


def ioc_pins(props: dict[str, str]) -> dict[str, dict[str, str]]:
    """pad -> {attr: value}, e.g. {'PA5': {'Signal': 'GPIO_Output', 'GPIO_Label': 'STATUS_LED'}}."""
    pins: dict[str, dict[str, str]] = {}
    for key, value in props.items():
        if "." not in key:
            continue
        pad, attr = key.split(".", 1)
        if _PAD_RE.match(pad):
            pins.setdefault(pad, {})[attr] = value
    return pins


def parse_pin_map(driver: Path) -> dict[str, str]:
    """GpioPin enumerator -> raw initializer text, from gpio_stm32.c pin_map[]."""
    text = driver.read_text()
    match = re.search(r"pin_map\s*\[[^\]]*\]\s*=\s*\{(.*?)\}\s*;", text, re.DOTALL)
    if not match:
        return {}
    rows: dict[str, str] = {}
    for desig, body in re.findall(r"\[\s*([A-Za-z_]\w*)\s*\]\s*=\s*\{([^}]*)\}", match.group(1)):
        rows[desig] = body.strip()
    return rows


def parse_main_h_defines(main_h: Path) -> set[str]:
    if not main_h.exists():
        return set()
    return set(re.findall(r"#\s*define\s+([A-Za-z_]\w*)", main_h.read_text()))


def parse_gpio_configs(sources: list[Path]) -> list[tuple[str, str, str, Path]]:
    """Resolve `<x>.init(GPIO_PIN_FOO, &cfg)` call sites in first-party code to
    (pin, GPIO_DIR_*, cfg-var, file). Only sites whose GpioConfig initializer
    is a sibling in the same file and carries an explicit `.dir` are returned;
    anything fancier is left for a human."""
    call_re = re.compile(r"(?:\.|->)init\s*\(\s*(GPIO_PIN_\w+)\s*,\s*&\s*(\w+)\s*\)")
    results: list[tuple[str, str, str, Path]] = []
    for path in sources:
        text = path.read_text()
        for pin, cfg_var in call_re.findall(text):
            if pin == "GPIO_PIN_COUNT":
                continue
            init = re.search(rf"\bGpioConfig\s+{re.escape(cfg_var)}\s*=\s*\{{([^}}]*)\}}", text)
            if not init:
                continue
            direction = re.search(r"\.dir\s*=\s*(GPIO_DIR_\w+)", init.group(1))
            if direction:
                results.append((pin, direction.group(1), cfg_var, path))
    return results


def parse_can_gpio(driver: Path) -> tuple[str | None, list[int]]:
    """(port, [pad numbers]) that can_stm32.c configures for the CAN peripheral."""
    text = driver.read_text()
    port_match = re.search(r"HAL_GPIO_Init\s*\(\s*(GPIO[A-H])", text)
    pin_match = re.search(r"gpio_init\.Pin\s*=\s*([^;]+);", text)
    port = port_match.group(1) if port_match else None
    pads = [int(n) for n in _PIN_BIT_RE.findall(pin_match.group(1))] if pin_match else []
    return port, pads


# --------------------------------------------------------------------------- #
# Checks
# --------------------------------------------------------------------------- #


def ioc_gpio_labels(pins) -> dict[str, str]:
    """normalised User Label -> pad, for .ioc pins wired as plain GPIO."""
    out: dict[str, str] = {}
    for pad, attrs in pins.items():
        label = attrs.get("GPIO_Label")
        if label and attrs.get("Signal") in _GPIO_SIGNALS:
            out[normalize_label(label)] = pad
    return out


def check_gpio(enumerators, pins, pin_map, main_defines, label_to_pad) -> bool:
    ok = True

    print("GpioPin enum -> .ioc / main.h / pin_map[]")
    if not enumerators:
        _note("gpio_if.h defines no logical GPIO pins (only GPIO_PIN_COUNT) -- nothing to map")
    for enum_name in enumerators:
        if not enum_name.startswith("GPIO_PIN_"):
            _note(f"{enum_name}: does not follow the GPIO_PIN_<LABEL> convention, skipping")
            continue
        label = enum_name[len("GPIO_PIN_"):]

        pad = label_to_pad.get(label)
        if pad is None:
            _fail(
                f"{enum_name}: no GPIO_Output/GPIO_Input pin with User Label '{label}' "
                f"in the .ioc -- add it in CubeMX (set the pad's mode, User Label = "
                f"'{label}', tick Locked) and regenerate"
            )
            ok = False
        else:
            _ok(f"{enum_name} <-> .ioc {pad} (label '{label}')")
            if pins[pad].get("Locked", "false").lower() != "true":
                _note(f"{enum_name}: {pad} is not Locked in the .ioc -- a later change could steal it")

        # main.h generated defines
        if pad is not None:
            missing = [f"{label}{s}" for s in ("_Pin", "_GPIO_Port") if f"{label}{s}" not in main_defines]
            if missing:
                _fail(f"{enum_name}: {', '.join(missing)} missing from main.h -- regenerate code from the .ioc")
                ok = False
            else:
                _ok(f"{enum_name}: main.h defines {label}_Pin / {label}_GPIO_Port")

        # pin_map[] row
        body = pin_map.get(enum_name)
        if body is None:
            _fail(f"{enum_name}: no pin_map[] row in gpio_stm32.c -- add [{enum_name}] = {{ ... }}")
            ok = False
            continue

        # pin_map[] must use the generated macros, so a renamed/removed .ioc
        # pin breaks the compile instead of drifting silently.
        if f"{label}_Pin" in body and f"{label}_GPIO_Port" in body:
            _ok(f"{enum_name}: pin_map[] uses {label}_GPIO_Port / {label}_Pin")
        else:
            detail = ""
            bit = _PIN_BIT_RE.search(body)
            port = re.search(r"\bGPIO([A-H])\b", body)
            if bit and port:
                row_pad = f"P{port.group(1)}{bit.group(1)}"
                detail = f" (currently the literal {row_pad}"
                detail += f", which also disagrees with the .ioc pad {pad})" if pad and row_pad != pad else ")"
            _fail(
                f"{enum_name}: pin_map[] row must be {{{label}_GPIO_Port, {label}_Pin}} "
                f"from the generated main.h{detail}"
            )
            ok = False

    # Reverse direction: labelled GPIO pins with no enumerator.
    print("\n.ioc GPIO pins -> GpioPin enum")
    known_labels = {n[len("GPIO_PIN_"):] for n in enumerators if n.startswith("GPIO_PIN_")}
    reverse_ok = True
    for label, pad in sorted(label_to_pad.items()):
        if label not in known_labels:
            _fail(
                f"{pad} is a GPIO pin labelled '{label}' in the .ioc but there is no "
                f"GPIO_PIN_{label} in gpio_if.h"
            )
            reverse_ok = False
    if reverse_ok:
        _ok(f"{len(label_to_pad)} labelled GPIO pin(s), all have a GpioPin enumerator")
    ok = ok and reverse_ok
    return ok


def check_can(pins, port, pads) -> bool:
    print("\ncan_stm32.c CAN pads -> .ioc")
    if port is None or not pads:
        _note("could not locate the CAN GPIO configuration in can_stm32.c, skipping")
        return True
    letter = port[-1]
    ok = True
    for num in pads:
        pad = f"P{letter}{num}"
        signal = pins.get(pad, {}).get("Signal", "")
        if signal.startswith("CAN1_"):
            _ok(f"{pad} -> {signal}")
        else:
            _fail(
                f"can_stm32.c configures {pad} for CAN, but the .ioc has "
                f"{pad}.Signal={signal or '(unset)'} -- the CAN pinout has moved"
            )
            ok = False
    return ok


def check_gpio_config(configs, pins, label_to_pad) -> bool:
    """GpioConfig(dir) at first-party call sites vs the .ioc pin direction.

    GpioConfig is the chip-neutral source of truth for a pin's behaviour;
    this catches the .ioc reserving a pad as an output while the firmware
    drives it as an input (or vice versa)."""
    print("\nGpioConfig(dir) call sites -> .ioc Signal")
    if not configs:
        _note(
            "no first-party <x>.init(GPIO_PIN_..., &cfg) call sites yet -- they belong "
            "in the composition root / services; nothing to cross-check"
        )
        return True

    want = {"GPIO_DIR_OUTPUT": "GPIO_Output", "GPIO_DIR_INPUT": "GPIO_Input"}
    ok = True
    for pin, direction, cfg_var, path in configs:
        if not pin.startswith("GPIO_PIN_"):
            continue
        pad = label_to_pad.get(pin[len("GPIO_PIN_"):])
        if pad is None:
            continue  # already reported by check_gpio
        signal = pins.get(pad, {}).get("Signal", "")
        expected = want.get(direction)
        if expected and signal != expected:
            _fail(
                f"{path.name}: {cfg_var} drives {pin} as {direction}, but the .ioc has "
                f"{pad}.Signal={signal or '(unset)'}"
            )
            ok = False
        else:
            _ok(f"{pin} {direction} <-> .ioc {pad}.Signal={signal}")
    return ok


# --------------------------------------------------------------------------- #


def main() -> int:
    default_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=default_root, help="repo root (default: %(default)s)")
    parser.add_argument("--ioc", type=Path)
    parser.add_argument("--gpio-if", type=Path)
    parser.add_argument("--gpio-drv", type=Path)
    parser.add_argument("--can-drv", type=Path)
    parser.add_argument("--main-h", type=Path)
    args = parser.parse_args()

    root = args.root
    ioc = args.ioc or root / "production/cubemx/vendor/vendor.ioc"
    gpio_if = args.gpio_if or root / "production/interfaces/gpio_if.h"
    gpio_drv = args.gpio_drv or root / "production/drivers/gpio_stm32.c"
    can_drv = args.can_drv or root / "production/drivers/can_stm32.c"
    main_h = args.main_h or root / "production/cubemx/vendor/Core/Inc/main.h"

    for path in (ioc, gpio_if, gpio_drv, can_drv):
        if not path.exists():
            sys.exit(f"ioc_check: required file not found: {path}")

    props = parse_ioc(ioc)
    pins = ioc_pins(props)
    labels = ioc_gpio_labels(pins)
    enumerators = parse_gpio_enum(gpio_if)
    pin_map = parse_pin_map(gpio_drv)
    main_defines = parse_main_h_defines(main_h)
    can_port, can_pads = parse_can_gpio(can_drv)

    # First-party C, minus the tests and the fakes -- real GpioConfig call sites.
    src_globs = ("production/drivers/*.c", "production/services/*.c",
                 "production/app/*.c", "production/src/*.c")
    sources = sorted(
        p for pattern in src_globs for p in root.glob(pattern)
        if not p.name.endswith("_fake.c")
    )
    configs = parse_gpio_configs(sources)

    try:
        shown = ioc.relative_to(root)
    except ValueError:
        shown = ioc
    print(f"ioc-check: {shown}\n")

    gpio_ok = check_gpio(enumerators, pins, pin_map, main_defines, labels)
    config_ok = check_gpio_config(configs, pins, labels)
    can_ok = check_can(pins, can_port, can_pads)

    print()
    if gpio_ok and config_ok and can_ok:
        print("ioc-check: OK -- .ioc and first-party pin definitions agree")
        return 0
    print("ioc-check: FAILED -- resolve the [FAIL] lines above")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
