#!/usr/bin/env python3
"""Summarize the first bad model-node pointer and its write stack in a GDB log.

Usage: python3 tools_pc/model_watch_summary.py model-watch.log
The original log remains authoritative; this script only selects events.
"""
import argparse
from pathlib import Path
import re

EVENT = re.compile(r"^\[MODEL-WATCH-GDB\] (ARM|CHECKPOINT|WRITE|NONZERO_HIGH|CHECKPOINT_ERROR|WATCH_ERROR)\b")
NEW = re.compile(r"\bnew=0x([0-9a-fA-F]+)")


def summarize(lines):
    events = []
    first_bad = None
    first_stack = []
    capture_stack = False
    count = 0
    for line in lines:
        match = EVENT.match(line)
        if match:
            kind = match.group(1)
            if kind in ("ARM", "CHECKPOINT", "CHECKPOINT_ERROR", "WATCH_ERROR"):
                events.append(line.rstrip())
            if kind in ("WRITE", "NONZERO_HIGH"):
                count += 1
                value = NEW.search(line)
                if first_bad is None and value and int(value.group(1), 16) >> 32:
                    first_bad = line.rstrip()
                    capture_stack = True
                elif capture_stack:
                    capture_stack = False
                continue
        if capture_stack and line.startswith("#") and len(first_stack) < 10:
            first_stack.append(line.rstrip())
        elif capture_stack and first_stack and not line.startswith("#"):
            capture_stack = False
    return events, count, first_bad, first_stack


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    args = parser.parse_args()
    events, count, first_bad, stack = summarize(args.log.read_text(errors="replace").splitlines())
    print("Checkpoints and watchpoint arms:")
    print("\n".join(events) or "none (verify GE_MODEL_WATCH and the instrumented executable)")
    print("Watchpoint writes:", count)
    print("First nonzero upper half:", first_bad or "not observed")
    if stack:
        print("Recorded stack:\n" + "\n".join(stack))
    if not any(" ARM " in event for event in events):
        raise SystemExit(2)


if __name__ == "__main__":
    main()
