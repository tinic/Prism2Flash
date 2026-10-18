#!/usr/bin/env python3
"""Boot a private Amiga drive and run the actual native binary, without a card.

Uses locally supplied emulator/ROM assets; never accesses a real Amiga or the
network. Each invocation has a fresh drive/token and stops only its own child.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time
import uuid

VERSION = (Path(__file__).resolve().parents[1] / "VERSION").read_text().strip()

CASES = [
    ("help", "--help", 0),
    ("version", "--version", 0),
    ("no_args", "", 20),
    ("invalid_sram", "probe --sram 32", 20),
    ("missing_sram", "probe", 20),
    ("reject_flash", "flash", 20),
    ("info", "info", 20),
    ("inspect", "inspect", 20),
    ("probe8", "probe --sram 8", 20),
    ("probe16", "probe --sram 16", 20),
    # Repeated claiming/failing/releasing catches stale-handle cleanup faults.
    ("info_again", "info", 20),
    ("inspect_again", "inspect", 20),
    ("help_after_errors", "--help", 0),
]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def stop_child(proc):
    if proc.poll() is None:
        try:
            os.killpg(proc.pid, signal.SIGTERM)
        except ProcessLookupError:
            return
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            proc.wait(timeout=5)


def valid_output(name, text, suite="genesis"):
    if suite == "firmware":
        if name == "version":
            return text.strip() == f"Prism2Firmware {VERSION}"
        if name.startswith("inspect_"):
            return "Structural inspection passed" in text and "Component: id=" in text
        if name.startswith("validate_"):
            return "Offline metadata and PDA checks passed" in text
        if name == "reject_wrong_mode":
            return "Offline validation rejected" in text and "class differs" in text
        if name == "bad_checksum":
            return "Image rejected" in text and "checksum" in text
        if name == "missing_image":
            return "Cannot open image" in text
        return f"Prism2Firmware {VERSION} - offline" in text
    active = name in ("info", "inspect", "probe8", "probe16", "info_again", "inspect_again")
    if active:
        return f"Prism2Genesis {VERSION} - NATIVE AMIGA" in text and (
            "Cannot own PCMCIA card" in text or "No card.resource" in text)
    if name == "version":
        return text.strip() == f"Prism2Genesis {VERSION}"
    return f"Prism2Genesis {VERSION} - experimental" in text


def copperline_run(args):
    """Use Copperline's native --run/--exit-on-return interface directly.

    Each case is a fresh guest; Amiberry separately covers repeated commands
    in one boot. No launcher that could mask the real executable's exit code.
    """
    token = uuid.uuid4().hex
    run = (args.output / f"copperline-{args.model}-{args.cpu}-{token[:8]}").resolve()
    run.mkdir(parents=True)
    config = run / "copperline.toml"
    config.write_text(f"rom = {json.dumps(str(args.rom.resolve()))}\n"
                      f"[cpu]\nmodel = {json.dumps(args.cpu)}\n[memory]\nfast = \"4M\"\n")
    report = {"token": token, "emulator": "copperline", "suite": args.suite, "model": args.model,
              "cpu": args.cpu, "binary_sha256": digest(args.binary),
              "rom_sha256": digest(args.rom), "guest_stack_bytes": 4096,
              "real_card_tested": False, "mode": "direct executable launch per case",
              "cases": [], "failures": []}
    print(f"Booting Copperline {args.model}/{args.cpu}: {run}", flush=True)
    for name, command, expected in CASES:
        case_dir = run / name
        case_dir.mkdir()
        binary = case_dir / args.program
        shutil.copy2(args.binary, binary)
        if args.suite == "firmware":
            from firmware_fixtures import write_fixtures
            write_fixtures(case_dir)
        cmd = [str(args.emulator_path.resolve()), "--config", str(config),
               "--model", args.model, "--run", str(binary),
               "--run-args", (command + " >RunProg:stdout.txt").strip(),
               "--run-stack", "4096", "--exit-on-return", "--noaudio"]
        with (case_dir / "emulator.log").open("wb") as log:
            proc = subprocess.Popen(cmd, cwd=case_dir, stdout=log, stderr=subprocess.STDOUT,
                                    start_new_session=True)
            timed_out = False
            try:
                rc = proc.wait(timeout=args.timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                rc = 124
            finally:
                stop_child(proc)
        out = case_dir / "stdout.txt"
        text = out.read_text(errors="replace") if out.exists() else ""
        ok = not timed_out and rc == expected and valid_output(name, text, args.suite)
        report["cases"].append({"name": name, "args": command, "exit": rc,
                                "expected_exit": expected, "passed": ok,
                                "output": text, "command": cmd})
        if not ok:
            report["failures"].append(f"{name}: exit {rc}, expected {expected}; output={text[:200]!r}")
    report["passed"] = not report["failures"]
    (run / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"{'PASS' if report['passed'] else 'FAIL'}: {len(CASES)} cases; {run / 'result.json'}", flush=True)
    for failure in report["failures"]:
        print(failure, file=sys.stderr)
    return not report["passed"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", choices=("copperline", "amiberry"), required=True)
    parser.add_argument("--emulator-path", type=Path, required=True)
    parser.add_argument("--model", choices=("A600", "A1200", "A3000"), required=True)
    parser.add_argument("--cpu", choices=("68000", "68020", "68030", "68060"), required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--suite", choices=("genesis", "firmware"), default="genesis")
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--env-helper", type=Path, default=Path("build-amiga/emulator_env"))
    parser.add_argument("--output", type=Path, default=Path("build-emulator"))
    parser.add_argument("--timeout", type=float, default=45)
    args = parser.parse_args()
    args.program = "Prism2Firmware" if args.suite == "firmware" else "Prism2Genesis"
    if args.binary is None:
        args.binary = Path("build-amiga") / args.program
    if args.suite == "firmware":
        global CASES
        prefix = "RunProg:" if args.emulator == "copperline" else "DH0:"
        options = f" --card {prefix}card.txt --pda {prefix}pda.bin"
        CASES = [
            ("help", "--help", 0), ("version", "--version", 0),
            ("inspect_flash", f"inspect {prefix}station.hex", 0),
            ("inspect_ram", f"inspect {prefix}ram.hex", 0),
            ("validate_flash", f"validate {prefix}station.hex --mode flash" + options, 0),
            ("validate_ram", f"validate {prefix}ram.hex --mode ram" + options, 0),
            ("validate_pair", f"validate {prefix}station.hex --mode flash" + options + f" --primary {prefix}primary.hex", 0),
            ("reject_wrong_mode", f"validate {prefix}ram.hex --mode flash" + options, 20),
            ("bad_checksum", f"inspect {prefix}bad.hex", 20),
            ("missing_image", f"inspect {prefix}missing.hex", 20),
            ("missing_args", "validate", 20), ("reject_flash", "flash", 20),
            ("help_after_errors", "--help", 0),
        ]
    for path in (args.emulator_path, args.rom, args.binary):
        if not path.is_file():
            parser.error(f"missing file: {path}")
    if args.emulator == "amiberry" and not args.env_helper.is_file():
        parser.error(f"missing environment helper: {args.env_helper}")
    if args.timeout <= 0:
        parser.error("timeout must be positive")
    if args.model != "A600" and args.cpu == "68000":
        parser.error("68000 requires the A600 profile and its matching ROM")
    if args.emulator == "copperline":
        return copperline_run(args)
    token = uuid.uuid4().hex
    run = (args.output / f"{args.emulator}-{args.model}-{args.cpu}-{token[:8]}").resolve()
    drive = run / "drive"
    for drawer in ("c", "s", "env", "envarc", "t", "clips", "libs", "devs"):
        (drive / drawer).mkdir(parents=True, exist_ok=True)
    shutil.copy2(args.binary, drive / "c" / args.program)
    if args.suite == "firmware":
        from firmware_fixtures import write_fixtures
        write_fixtures(drive)
    shutil.copy2(args.env_helper, drive / "c/smoke-env")
    startup = ["failat 9999", "stack 4096", "DH0:c/smoke-env", 'echo "$RC" >DH0:env.rc']
    for name, command, _ in CASES:
        startup += [f"DH0:c/{args.program} {command} >DH0:{name}.txt", f'echo "$RC" >DH0:{name}.rc']
    startup += ["DH0:c/smoke-env", 'echo "$RC" >DH0:env_after.rc', f'echo "{token}" >DH0:completed']
    (drive / "s/Startup-Sequence").write_bytes(("\n".join(startup) + "\n").encode("ascii"))
    rom = args.rom.resolve()
    config = run / "smoke.uae"
    config.write_text(
            f"config_description=Prism2Flash native smoke {token}\n"
            f"use_gui=no\nheadless=true\nquickstart={args.model},0\n"
            f"kickstart_rom_file={rom}\ncpu_model={args.cpu}\n"
            "cpu_compatible=true\ncachesize=0\ncpu_speed=max\n"
            "fastmem_size=4\nchipmem_size=2\nsound_output=none\n"
            "floppy0type=-1\nfloppy1type=-1\n"
            f"filesystem2=rw,DH0:DH0:{drive},0\n"
            f"uaehf0=dir,rw,DH0:DH0:{drive},0\n"
    )
    cmd = [str(args.emulator_path.resolve()), "--log", "-f", str(config)]
    report = {
        "token": token, "emulator": args.emulator, "suite": args.suite, "model": args.model,
        "cpu": args.cpu, "binary_sha256": digest(args.binary),
        "rom_sha256": digest(rom), "command": cmd,
        "guest_stack_bytes": 4096, "real_card_tested": False,
    }
    print(f"Booting {args.emulator} {args.model}/{args.cpu}: {run}", flush=True)
    completed = False
    with (run / "emulator.log").open("wb") as log:
        proc = subprocess.Popen(cmd, cwd=run, stdout=log, stderr=subprocess.STDOUT,
                                start_new_session=True)
        deadline = time.monotonic() + args.timeout
        try:
            while time.monotonic() < deadline:
                marker = drive / "completed"
                if marker.exists() and marker.read_text().strip() == token:
                    completed = True
                    break
                if proc.poll() is not None:
                    break
                time.sleep(0.1)
        finally:
            stop_child(proc)
    failures = []
    if not completed:
        failures.append("guest did not write this run's completion token")
    report["cases"] = []
    for name, command, expected in CASES:
        rc_file = drive / f"{name}.rc"
        output = drive / f"{name}.txt"
        rc_text = rc_file.read_text().strip() if rc_file.exists() else "missing"
        text = output.read_text(errors="replace") if output.exists() else ""
        rc = int(rc_text) if rc_text.isdigit() else None
        ok = rc == expected and valid_output(name, text, args.suite)
        if not ok:
            failures.append(f"{name}: exit {rc_text}, expected {expected}; output={text[:200]!r}")
        report["cases"].append({"name": name, "args": command, "exit": rc,
                                "expected_exit": expected, "passed": ok, "output": text})
    for name in ("env", "env_after"):
        path = drive / f"{name}.rc"
        if not path.exists() or path.read_text().strip() != "0":
            failures.append(f"{name}: environment helper failed")
    report["passed"] = not failures
    report["failures"] = failures
    (run / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"{'PASS' if not failures else 'FAIL'}: {len(CASES)} cases; {run / 'result.json'}", flush=True)
    for failure in failures:
        print(f"  {failure}", file=sys.stderr)
    return bool(failures)


if __name__ == "__main__":
    sys.exit(main())
