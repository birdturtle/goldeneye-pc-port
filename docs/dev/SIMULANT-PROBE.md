# Separate Simulant: first engine integration probe

`GE_MP_SIM_PROBE=1` enables a temporary, PC-only test character in local
multiplayer. It consumes a GoldenEye character slot, not a human player slot,
controller, or viewport. It uses the game's `chrSpawnAtCoord` collision-safe
spawn and ordinary `PROP_TYPE_CHR` damage/death paths. In Facility it attaches
the original solo route tables to the identical multiplayer pads and asks
GoldenEye's `chrGoToPad` to walk toward a living player. It does **not** add
weapons, pickups, scoring, bot profiles, or multiplayer menu controls. It respawns a fresh probe
character after GoldenEye removes the dead one.

The trigger is `port/src/input.c`'s controller snapshot, after stage loading
has created a multiplayer player body and character slots. The implementation
is `port/src/simulant_probe.c`. There are no changes to `src/game` or the N64
build. `GE_MP_SIM_PROBE` is off by default.

## Windows / MSYS2 MINGW64 check

From the project root:

```sh
./build-pc.sh ntsc-final
GE_MP_SIM_PROBE=1 GE_MP_TEST_PLAYERS=2 ./build-pc/ge007.x86_64.exe
```

Start a local 2-player match, preferably Facility. A separate character
using Player 1's selected multiplayer body/head should appear at a multiplayer
spawn pad at least 250 units from every living player. The log should contain
`sim probe: spawned character 28671` and the pad number. Find and shoot it;
it should react, die, and return at a spawn pad after its corpse disappears
(at least 180 controller polls after death began). Neither human player's
movement or split-screen view should be affected. Also run without
`GE_MP_SIM_PROBE` to confirm the normal match has no extra character.

If all multiplayer pads are within 250 units of living players, the probe
waits for a pad to become free. If the final safe-spawn search cannot find a
position, the log reports `character spawn failed`. It retries up to five
times, 120 controller polls apart. Report a
crash with the GDB backtrace and the last `sim probe` log entry.

Facility should log `installed original Facility navigation (157 waypoints,
22 groups)` and then `route to waypoint ...: walking`. Follow it through a
corridor and toward a closed door. Note whether the door opens, whether the
actor reaches its goal, and where it gets stuck. This first navigation proof
chooses a waypoint near a player and only chooses a new destination when
GoldenEye ends its current walking action. Other multiplayer stages still
have a stationary probe. If Facility's pads or STAN links differ, navigation
is disabled with a log message; the character can still spawn.

## Next proof

The original multiplayer setups have no waypoint network. Facility's probe
now attaches its solo graph to the matching multiplayer pads. Prove one
corridor and door traversal using character locomotion, then implement
goal updates and stalled-route recovery. Next, join the character to multiplayer score, death, and respawn
rules. Keep the existing synthetic-controller bot available until those
milestones have been tested.
