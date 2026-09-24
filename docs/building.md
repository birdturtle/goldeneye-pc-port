---
title: Building
description: Full build and asset-extraction guide for the GoldenEye 007 PC port, covering Windows (MSYS2) and Linux.
---

## Building the PC port

Stages:

1. **Extract assets from your ROM** (§2): a one-time step using the
   decompilation's own toolchain to pull levels, models, textures, fonts and
   music into `assets/`. *(Only needed to regenerate the committed data files;
   a plain `git clone` already has what the PC build compiles.)*
2. **Build the port** (§3): a CMake build compiling the game sources plus the
   `port/` layer into a native executable. Needs no ROM.
3. **Generate the PC asset sidecars** (§4): two pure-Python converters turn
   ROM model / stage data into the PC-layout `data/pcmodels-*` / `data/pccg-*`
   files the port loads at runtime. **Required to run.**

You need a GoldenEye 007 N64 ROM you legally own (`.z64`, big-endian). See the
[Requirements table in the README](https://github.com/jkdansereau/goldeneye-pc-port#requirements)
for accepted versions and hashes.

---

## 1. Dependencies

### Port build

> **The Windows (MSYS2 MINGW64) path is the primary one.** The Linux build is
> compiled by CI on every push and ships in the release bundle (also
> playtested on Steam Deck hardware); the macOS column is best-effort
> guidance and has never been built or run there. Expect to fix build breaks
> yourself on untested platforms.

| Need | Windows (MSYS2 MINGW64) | Debian/Ubuntu | macOS (Homebrew) |
|------|------------------------|---------------|------------------|
| toolchain | `mingw-w64-x86_64-toolchain` | `build-essential` | Xcode CLT / `gcc` |
| CMake | `mingw-w64-x86_64-cmake` | `cmake` | `cmake` |
| SDL2 | `mingw-w64-x86_64-SDL2` | `libsdl2-dev` | `sdl2` |
| zlib | `mingw-w64-x86_64-zlib` | `zlib1g-dev` | `zlib` |
| OpenGL | (in the toolchain) | `libgl1-mesa-dev` | (system) |
| Python 3 | `mingw-w64-x86_64-python` | `python3` | `python3` |

### Asset extraction (decompilation toolchain)

The extraction scripts need `binutils-mips-linux-gnu` (or an equivalent MIPS
binutils), `make`, `git`, and `python3`. They build a small host-compiled
`tools/extractor` and slice blobs straight out of the ROM; **no IDO / IRIX
toolchain is involved in extraction or in the PC build.** (The IDO toolchain is
only needed to build the N64 ROM itself, and its proprietary SGI binaries are
not distributed here; see [`SetupGuide.md`](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/SetupGuide.md) "Recompile IDO".)
On Windows this is easiest under WSL or a Linux VM. Full details and
alternatives (Docker) are in [`SetupGuide.md`](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/SetupGuide.md).

---

## 2. Extract assets

Put your **US** ROM at the repository root as `baserom.u.z64` (this name is
required by the extraction scripts; it is git-ignored and never committed),
then:

```sh
./scripts/extract_baserom.u.sh
```

For PAL or JP, additionally place `baserom.e.z64` / `baserom.j.z64` at the root
and run:

```sh
./scripts/extract_baserom.u.sh && ./scripts/extract_diff.e.sh   # PAL
./scripts/extract_baserom.u.sh && ./scripts/extract_diff.j.sh   # JP
```

(US extraction is a prerequisite for the others.)

This populates `assets/` with the generated `.bin` blobs the build needs. See
[`SetupGuide.md`](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/SetupGuide.md) for the in-depth build/asset pipeline.

---

## 3. Build

```sh
./build-pc.sh ntsc-final        # or: pal-final / jpn-final
```

which is equivalent to:

```sh
cmake -S . -B build-pc -DROMID=ntsc-final
cmake --build build-pc -j
```

For PAL/JP you must first generate that region's ROM-asset symbol file
(the US one is committed):

```sh
python3 scripts/gen_romassets.py e     # PAL   -> port/src/romassets_e.s
python3 scripts/gen_romassets.py j     # JP    -> port/src/romassets_j.s
```

The executable lands at `build-pc/ge007.x86_64` (`.exe` on Windows). PAL/JP
builds are named `ge007.pal-final.x86_64` / `ge007.jpn-final.x86_64`.

---

## 4. Generate the PC asset sidecars (required to run)

The port does **not** read model geometry, stage bg/stan data, or per-level
setup data from the raw ROM at runtime; it reads them from PC-layout *sidecar*
files under `data/`, produced offline by three converters. **Without them the
game shows the intro logos and then crashes** in
`modelPromoteNodeOffsetsToPointers` (finding D179) or on the first level load
(missing stage setup).

Put your ROM in `data/` first (same file the game runs from):

```sh
mkdir -p data
cp /path/to/your/rom.z64 data/ge007.ntsc-final.z64     # or pal-final / jpn-final
```

Then run all three emit passes for that region, **in this order** (d88 appends
to d69's output):

```sh
python3 tools_pc/d43_emit.py ntsc-final          # -> data/pcmodels-ntsc-final/{pcmodels.bin,manifest.csv}  (~1.3 MB)
python3 tools_pc/d69_emit.py ntsc-final          # -> data/pccg-ntsc-final/{pccg.bin,manifest.csv}          (bg + stan)
python3 tools_pc/d88_emit.py ntsc-final --regen  #    appends solo and multiplayer stage-setup files
```

**PAL / JP note:** sidecar generation for these regions is currently broken at
the source-data level (finding D258); use an NTSC-U ROM until issue #85 lands.

These are **pure-stdlib Python 3** (no MIPS toolchain, independent of the
step-2 asset extraction) and read only the ROM plus files already committed to
the repo (`scripts/filelist.u.csv`, `assets/obseg/file_resource_table.inc.c`,
`assets/**/ModelFileHeader.inc.c`, the bg/stan `.inc.c`). Output is a
deterministic function of the ROM. Re-run after any change to `d43_emit.py` /
`d69_emit.py` / `d88_emit.py` or the model/bg converters (`d43_*`, `d69_*`,
`d88_propdefs.py`).

For local multiplayer, regenerate the sidecar with `d88_emit.py --regen` even
if you already generated it for solo play. Earlier versions omitted the
`Ump_setup*Z` files; the Temple match needs `Ump_setuparchZ` in
`data/pccg-ntsc-final/manifest.csv`. Rebuilding the executable alone does not
regenerate ROM-derived sidecars.

Local multiplayer devices are assigned in GoldenEye's **Control Style** menu.
The PC port reports keyboard/mouse plus connected SDL gamepads as available
player slots (up to four). The menu defaults Player 1 to keyboard/mouse and
Players 2–4 to gamepads in detection order. On a player's panel, press Up or
Down on that player's current device to cycle to another device. Choosing a
device used by another player swaps their assignments, so one device cannot
control two players. Each player can also select an original GoldenEye control
style there; local multiplayer defaults to 1.2 (Solitaire) for analog look.
Connect the devices before opening the multiplayer setup screen. No launch
flag is needed, and solo play still merges keyboard/mouse with the first pad.

To exercise three or four local players with fewer devices, launch with
`GE_MP_TEST_PLAYERS=4 ./build-pc/ge007.x86_64.exe` in MSYS2 MINGW64. The
missing controller slots appear as **TEST PAD** in the Control Style menu
and send neutral input until selected. On the Character, Handicap, and Control
Style screens, press **F8** to lend keyboard/mouse to the next TEST PAD player;
confirm each player's selection normally (A, Z, or Start). A `*` marks the
test player currently receiving keyboard/mouse on the Control Style screen.
F8 also cycles through test players in a running match, then back to the
keyboard player's slot. Only one
player receives keyboard/mouse input at a time; connected gamepads continue
to control their assigned players. TEST PAD assignments are fixed, while real
keyboard and gamepad assignments can still be swapped in the menu. Set
`GE_MP_TEST_PLAYERS=3` for a three-player test, or omit it to show only real
devices. The original Players option
in the multiplayer menu still chooses the actual match size.

To try the first input-driven bot, use the same four-player test launch. On
the **Control Style** screen, press **F8** until a TEST PAD has the `*`, then
press **F9** to switch that slot to **BOT**. Confirm its 1.2 control style
with Enter. Repeat for a second bot if desired, and confirm the other players
as usual. In the match, F8 temporarily lends your keyboard/mouse to a bot
slot, letting you take it over; cycle back to resume its bot input. This first
bot pursues the nearest living opponent and fires when aligned on roughly the
same height. It does not yet navigate around walls, seek items, or account for
teams. Start with a free-for-all match to test it.

> The release bundle ships the same converter frozen as
> `prepare-assets/ge007-convert`; the game spawns it on first launch when the
> sidecars are missing, so end users never run it by hand. See the bundled
> `README.md`.

> `data/pcmodels-*/` and `data/pccg-*/` are gitignored ROM-derived game data;
> never commit or redistribute them.

---

## 5. Run

```sh
./build-pc/ge007.x86_64          # run from the repo root
```

The ROM in `data/` (from step 4) and the sidecars are both required at
runtime. `ge007.ini` is written under `data/` on first launch.

### Useful flags / env

| | |
|---|---|
| `-level_NN` | boot straight into a solo level (e.g. `-level_09` = Bunker 1) |
| `GE_PCDUMP="first-last:step"` | dump rendered frames as PPM (debugging) |

More diagnostic switches are cataloged in
[`dev/GE-ENV-PROBES.md`](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/dev/GE-ENV-PROBES.md).
