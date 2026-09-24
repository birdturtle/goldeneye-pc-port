#!/usr/bin/env python3
"""Emit Facility's original solo waypoint tables for the PC Simulant probe.

The MP setup uses identical pads, but has empty waypoint/group tables.
Only the navigation tables are copied; no solo props or scripts are loaded.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent
SETUP = ROOT / "assets/obseg/setup"
OUT = ROOT / "port/src/simulant_facility_nav.h"


def body(source, declaration):
    start = source.index(declaration) + len(declaration)
    return source[start:source.index("};", start)]


def arrays(source, prefix):
    return {int(index): [int(n) for n in values.split(",")]
            for index, values in re.findall(
                rf"^s32 {prefix}_(\d+)\[\] = \{{([^}}]+)\}};", source, re.M)}


def pads(source):
    return re.findall(r'^\s*\{\s*\{([^}]+)\}.*?"([^"]+)"',
                      body(source, "PadRecord padlist[] = {"), re.M)


def generate():
    solo = (SETUP / "UsetuparkZ.c").read_text()
    mp = (SETUP / "Ump_setuparkZ.c").read_text()
    assert pads(solo) == pads(mp) and len(pads(mp)) == 311, "Facility pads diverged"
    points = re.findall(r"\{\s*(0x[0-9a-fA-F]+),\s*&path_table_(\d+),\s*"
                        r"(0x[0-9a-fA-F]+),\s*0x[0-9a-fA-F]+\s*\}",
                        body(solo, "waypoint pathwaypoints[] = {"))
    groups = re.findall(r"\{\s*&path_neighbors_(\d+),\s*&path_indeces_(\d+),\s*0\s*\}",
                        body(solo, "waygroup pathsets[] = {"))
    tables = {name: arrays(solo, name) for name in
              ("path_table", "path_neighbors", "path_indeces")}
    assert len(points) == len(tables["path_table"]) == 157
    assert len(groups) == len(tables["path_neighbors"]) == len(tables["path_indeces"]) == 22
    for i, (pad, link, group) in enumerate(points):
        assert int(link) == i and int(pad, 16) < 311 and int(group, 16) < len(groups)
    for i, (neighbors, indices) in enumerate(groups):
        assert int(neighbors) == int(indices) == i
        assert all(int(points[n][2], 16) == i for n in tables["path_indeces"][i][:-1])
    for name, entries in tables.items():
        for values in entries.values():
            assert values[-1] == -1
            bound = len(points) if name != "path_neighbors" else len(groups)
            assert all(0 <= n < bound for n in values[:-1])

    lines = ["/* Generated from assets/obseg/setup/UsetuparkZ.c by",
             " * tools_pc/gen_simulant_nav.py. Facility MP pads have matching IDs. */",
             "#ifndef PORT_SIMULANT_FACILITY_NAV_H",
             "#define PORT_SIMULANT_FACILITY_NAV_H", ""]
    for name, entries in tables.items():
        for i, values in sorted(entries.items()):
            lines.append(f"static s32 sim_{name}_{i}[] = {{ " +
                         ", ".join(map(str, values)) + " };")
    lines += ["", "static waypoint simFacilityWaypoints[] = {"]
    for pad, link, group in points:
        lines.append(f"    {{ {int(pad,16)}, sim_path_table_{link}, {int(group,16)}, 0 }},")
    lines += ["    { -1, NULL, 0, 0 }", "};", "",
              "static waygroup simFacilityGroups[] = {"]
    for neighbors, indices in groups:
        lines.append(f"    {{ sim_path_neighbors_{neighbors}, sim_path_indeces_{indices}, 0 }},")
    lines += ["    { NULL, NULL, 0 }", "};", "#endif", ""]
    lines[-2:-2] = ["static const char *const simFacilityPadNames[] = {"] + [
        f'    "{name}",' for _, name in pads(mp)] + ["};", ""]
    OUT.write_text("\n".join(lines))
    print(f"Facility: {len(points)} waypoints, {len(groups)} groups, {len(pads(mp))} shared pads")


if __name__ == "__main__":
    generate()
