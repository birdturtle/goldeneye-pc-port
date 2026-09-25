# Simulants: match contract and implementation order

Decision (2026-09-25): every GoldenEye multiplayer map permits four total
combatants. Humans claim the first slots; Simulants fill remaining slots. The
original per-map two/three-human restrictions do not constrain the PC
Simulant roster. The first release targets one to four humans and zero to
three Simulants, with at least two combatants to start a competitive match.

## The two counts

`human_count` controls controller sources, `struct player` allocation,
viewports and human HUD. `participant_count = human_count + simulant_count`
controls score, teams, match rules, radar targets, combat attribution and
winning. Both counts are at most four for this implementation. A Simulant
has a `ChrRecord` and a `PropRecord`, but never gets a fake human player,
controller or viewport.

`port/include/mp_roster.h` introduces a stage-scoped, human-first roster.
The roster stores a character number and match/respawn generations instead
of a stage-owned pointer. The first playable integration uses one actor on
Facility with one human, without an environment flag. The development
`GE_MP_SIM_PROBE=1` override is retained for existing two-human probe tests.

## UX: Perfect Dark's selection model in GoldenEye's presentation

Perfect Dark separates human player setup from its eight Simulant slots and
offers distinct Simulant skill/type choices. Adopt that clarity of setup,
then use GoldenEye's existing multiplayer page, cursor, font, selection
highlight, sounds and character presentation. The setup should show a
four-place roster with humans first, empty places available for Simulants,
and the resulting combatant total. Do not route normal match setup through
the F10 diagnostics overlay or expose environment flags as menu settings.

One connected controller must admit the multiplayer menu. The menu may
select one to four human places independently of the number of connected
controllers; starting requires a distinct assigned input source for each
human. This first phase does not add mid-match human joining. Keyboard and
mouse count as one source. A bot never reserves a source.

## Ordered gates

1. **Roster ownership.** Reserve up to four human-first identities. Begin it
   only after the stage is ready; clear it at teardown. A spawn binds one
   Simulant actor, and removal/respawn invalidates old actor references.
   Verify all count combinations and repeated match generations.
2. **One-human multiplayer.** Untie front-end entry and match setup from
   `joyGetControllerCount() >= 2` and `selected_num_players >= 2` on PC.
   Start one human plus one Simulant with one viewport, correct MP object
   setup and no second controller. The game must not enter solo mission
   rules simply because `getPlayerCount() == 1`.
3. **Shared score/rules.** Route kills, suicides, deaths, point limits,
   YOLT lives/placements, Golden Gun, Flag Tag and team membership through
   participant identities. Preserve human-only display and input loops.
   A human killing a bot and a bot killing a human must both count; a point
   limit must end a one-human match when either reaches it.
4. **Health/combat.** Define a multiplayer health/armor conversion for a
   `ChrRecord`, honor handicap and License to Kill, and use one attacker-
   aware damage adapter. Retire the probe's local kill counter and copied
   player damage routine once scoring owns both sides.
5. **Radar/results.** Show all living opponents and scenario carriers on
   the human radar, including with one viewport. Give bots GoldenEye-style
   names, portraits/colors and score rows without assigning viewports.
6. **Menu and scale.** Add the native four-place setup and selection of
   Simulant character and difficulty. Move actor scheduling into the game
   tick, support up to three Simulants, and test every map with four total
   combatants. Navigation, pickups, doors, death, respawn and cleanup must
   remain valid through ten consecutive match transitions.

The PC-only gameplay feature necessarily routes several `src/game` decisions
through an explicit multiplayer predicate. These changes are guarded by
`PORT`; the N64 path retains its original conditions. This is the new
Simulant feature authorized by the user, not a fidelity bug fix.

## First playable slice (2026-09-25)

With one input, select Multiplayer on the mode sheet, then Players on the
multiplayer options sheet. The Players dossier page shows the four places.
Choose one human and one Simulant, return with B, select Normal, Facility,
and a five-point or timed match, then press Start. The Facility stage is
selectable for this slice even if its campaign unlock is pending. With one
human and zero Simulants the menu shows that two combatants are required.
Other maps allow four human combatants, but bot setup currently selects
Facility because the actor navigation graph is Facility-specific.

The Game Length row defaults to **10 minutes**, which has no kill limit.
Select **5 POINTS** or **10 POINTS** in that row to test a point-limit ending.

The AI actor now ticks with the game simulation, occupies
the second score identity, appears on radar, and credits kills both ways in
Normal. The bot is named `SIM` on the watch score sheet. The point limit
checks both identities and match end still controls the single human
viewport. The probe's environmental override remains diagnostic.

**One-human MP presentation follow-up:** A Windows playtest found that the
first slice entered directly with a solo camera and bypassed the MP watch
tick and bottom-message queue. The one-human match now starts the native MP
third-person swirl, loads the multiplayer player body and starting weapon,
opens the native score/watch menu with Start, and shows health/armor gauges
through that watch or after damage. GoldenEye's character death action
already increments the native kill message and tally; the probe only writes
the bot identity into the shared score matrix. Bot-to-human deaths update
the native death message and the bot's score row. The original MP HUD does
not keep the gauges or a kill counter permanently visible during normal
play; use Start to inspect health, armor and scores. The one-human camera,
watch, single-credit tally, selected point-limit ending and varied safe
respawns were confirmed in the subsequent Windows playtests.

**Combat foundation follow-up:** `port/src/mp_combat.c` owns the bot's
stage-life attacker reference, bot-to-human damage and score writes. The
bot's full health is eight actor damage units, corresponding to a normal
multiplayer player's one normalized health unit for regular weapon hits.
The campaign one-human AI health multiplier is bypassed in a multiplayer
match. Bot shots use the human victim's selected Health handicap, and
normal player damage still depletes armor before health. Human shots record
the real attacker on the bot's current incarnation; the character death
action supplies the native kill message, then the combat adapter records
one score matrix entry. Damage without a known human attacker gives no
human point. Actor negative damage retains GoldenEye's armor capacity,
but the probe does not seek armor pickups yet. Explosion ownership and
special modes are later gates; Normal weapon hits are this phase's target.
Regional syntax and life-attribution checks pass; Windows combat feel and
health equivalence still require playtesting.

**Hit reaction follow-up:** Windows playtesting confirmed that the combat
slice works, but the bot visibly recoiled like a campaign guard on each
nonfatal hit. Native MP players continue their current action when shot.
The Simulant now retains its movement and firing action after nonfatal
weapon hits, while keeping hit sounds, health damage and the native death
animation on a fatal hit. The reaction check uses the bound stage life so
it cannot affect campaign guards or stale actors after a respawn. Check
that sustained hits no longer interrupt the bot's route, and that its final
hit still shows a death, one kill credit and a later respawn. Windows
verification of this follow-up is pending.

For that playtest, use Normal, Facility, Pistols, normal Health and 5 POINTS.
Count body hits needed to kill the bot, let it shoot you while watching the
health gauge, then test a higher/lower human Health handicap. Verify that
each death produces one score point and that a new match resets attribution.

**Current limits:** only one Simulant beside one human is offered. Other
scenarios, multiple bots, non-Facility AI routes, bot armor pickup/handicap,
and detailed awards for bots remain later gates. Do not mistake this slice
for the final all-map Simulant feature.

**Windows playtest:** build with `./build-pc.sh ntsc-final`; use the normal
game executable without `GE_MP_SIM_PROBE`. Enter with one controller or
keyboard/mouse, start one human plus one Simulant on Facility, confirm one
viewport, the MP spinning entrance and a yellow radar opponent. Press Start
for the native watch with health/armor and two score rows. Kill the bot and
check the kill message plus score; let it kill you and check the death
message plus losses row. Respawn with a button, then reach the five-point
limit and check the match-end watch. Across several deaths, verify the bot
uses varied unoccupied multiplayer start pads instead of always appearing
farthest from the human. Quit and repeat in the same process. Also
try four humans on Egypt/Bunker to confirm their old map caps no longer hide
those maps. Report the first incorrect screen or behavior and its log.

### Local roster check

```sh
cc -std=c11 -Wall -Wextra -Werror -Iport/include \
  port/src/mp_roster.c tools_pc/test_mp_roster.c -o /tmp/test_mp_roster
/tmp/test_mp_roster
```

### Windows runtime check for this first gate

The prototype still requires at least two human player slots. This check
verifies roster identity and stage cleanup; it does not exercise the future
one-human menu or scoreboard.

```sh
./build-pc.sh ntsc-final
GE_MP_SIM_PROBE=1 GE_MP_ROSTER_TRACE=1 GE_MP_TEST_PLAYERS=2 \
  ./build-pc/ge007.x86_64.exe 2>&1 | tee roster-test.log
```

Run a two-human Facility match with Pistols, kill the probe and wait for its
respawn, then quit to the menu and launch a second match in the same process.
The log should show `humans=2 simulants=1 total=3`, `bound slot 2 ... life=1`,
`unbound slot 2`, `bound slot 2 ... life=2`, and `end stage` between matches.
If a four-human match is configured, the roster should instead log
`humans=4 simulants=0 total=4` and the probe must not spawn a fifth actor.
