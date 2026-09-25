# Separate Simulant: first engine integration probe

`GE_MP_SIM_PROBE=1` enables a temporary, PC-only test character in local
multiplayer. It consumes a GoldenEye character slot, not a human player slot,
controller, or viewport. It uses the game's `chrSpawnAtCoord` collision-safe
spawn and ordinary `PROP_TYPE_CHR` damage/death paths. In Facility it attaches
the original solo route tables to the identical multiplayer pads and asks
GoldenEye's `chrGoToPad` to run toward a world firearm before pursuing a
living player. At pickup range, it creates a normal held character weapon
and starts the world pickup's multiplayer respawn timer. With an ordinary
firearm it turns toward a visible player and fires at a deliberately slow
rate. Player health, armor, inventory drop and death use GoldenEye's existing
state/functions; the probe keeps its kill tally in the log without writing
to the four human score slots. It does not add radar, bot profiles or
multiplayer menu controls. It respawns a fresh
probe character after GoldenEye removes the dead one.

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

### Second-match model lifetime check

Run the game with the temporary model trace enabled, then start Facility,
quit to the menu and start Facility again **without closing the executable**:

```sh
GE_D86=1 GE_MP_SIM_PROBE=1 GE_MP_TEST_PLAYERS=2 ./build-pc/ge007.x86_64.exe > second-match-trace.log 2>&1
grep -nE 'sim probe (stage ready|spawn before|spawn after|invalidate)|load_object_fill_header name=C(djbond|headbrosnan)Z' second-match-trace.log
```

At the end of match #1 the probe logs `invalidate` for only its used body
and head model IDs, with their former `RootNode`, `Switches` and `Textures`.
On the first bot spawn in match #2, `spawn before` should show a NULL
`RootNode` for each header that nothing else loaded yet. Each corresponding
`load_object_fill_header` log must appear after invalidation and before the
first match #2 bot spawn completes. `stage ready` must precede every bot
`spawn before` in that match: the input callback also runs during stage
initialization, when character slots from the previous stage may still be
visible and `bodiesReset` has not yet cleared model roots. If the player loads
the same model first,
its `load_object_fill_header` log may precede `spawn before` instead. A bot
respawn **within** a match should log `spawn before` with
non-NULL pointers and should share the existing definition without reloading.
The teardown callback runs after the original guard/object cleanup, before
the stage bank is reused. It also clears the route's cached world weapon
props and the failed-pad reference and restores the stage's original waypoint
tables; character, target, STAN and waypoint
pointers are local to a poll and are never retained across stage loads.

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
22 groups)` and then `route to waypoint ...: running`. Follow it through a
corridor and toward a closed door. Note whether the door opens, whether the
actor reaches its goal, and where it gets stuck. The route goal is checked
every 90 controller polls, including while the character is moving, and can
change when its nearest living player moves. If it advances less than 64
world units in 240 polls while still over 180 units from its goal, it logs a
stall and replans. A second stall temporarily avoids that goal. Watch for
`sim probe: stalled on waypoint ...` if it stops moving. Other multiplayer stages still
have a stationary probe. If Facility's pads or STAN links differ, navigation
is disabled with a log message; the character can still spawn.

For the weapon pickup test, select **Pistols** (or another set with guns) in
the multiplayer setup. The unarmed probe should log `route to weapon item ...`
and run to a map weapon. Only when it reaches the gun should it log `picked up
item ...`; it should then visibly hold the gun and return to chasing players.
The world gun should reappear on its normal multiplayer timer after players
move away. Shoot the probe and check that a new unarmed character again looks
for a world gun. With **Slappers Only**, expect `no available world firearm`
and unarmed pursuit. Weapon targeting is currently limited to ordinary
firearms (and the laser); thrown weapons, explosives and special scenario
items need their own rules.

For the first combat test, let the armed probe approach you in an open room.
It should turn toward you, raise its weapon arm, play the gun's positional
sound, flash at the muzzle and give a short arm recoil on each shot. The
flash lasts three controller polls so it can be seen in split-screen. It
should reduce armor/health and eventually
kill you. Respawn and check that your death count increased and neither
human player's kill/suicide total increased from the probe's shot. The log
should show `sim probe: eliminated player ... (probe kills ...)`. Stand behind
a wall or closed door: the probe may pursue but must not damage you until it
has line of sight. Try the same in a 4-player match and check that all human
scores remain intact. Test killing the probe during and after gunfire; it
should stop firing, die and eventually respawn unarmed. Watch and listen from
both split-screen viewpoints while the probe is running, standing, pursuing
through doors and shooting. Its route and door opening should still work.

This is a limited firearm proof: no ammunition, reloads, player hit sparks,
friendly fire, projectiles or multiplayer scoreboard entry
for the Simulant. Only ordinary hitscan firearms are enabled; the laser,
thrown weapons, explosives and Slappers Only are excluded. The probe's damage
is based on GoldenEye's firearm destruction value and uses its player
health/armor/death functions, with explicit non-player ownership. The original
`chrlvUpdateShotbondsum` passes `playerid=-1` to `record_damage_kills`, which
accesses `g_playerPlayerData[playerid]` in MP; using that path for the probe
would corrupt player statistics or crash. The port-side damage adapter should
eventually be replaced by a shared attacker-aware combat bridge when the bot
score/radar model is implemented.
The arm recoil uses the character renderer's right-shoulder aim controls and
does not enter `ACT_ATTACK`, since that would replace the navigation action.

## Next proof

The original multiplayer setups have no waypoint network. Facility's probe
now attaches its solo graph to the matching multiplayer pads. Run, goal
updates, stall retry, world firearm pickup, first visible-target gunfire and
shot feedback are in the probe; manually test shot audio/visuals, damage,
scores and doorway occlusion. Radar,
proper Simulant score display and full weapon mechanics are still outstanding.
Keep the existing synthetic-controller bot available until those milestones
have been tested.
