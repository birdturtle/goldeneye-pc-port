/* Experimental separate-character Simulant proof. Enable with
 * GE_MP_SIM_PROBE=1; no player/controller slot is consumed. */
#include <stdlib.h>
#include <string.h>

#include "simulant_probe.h"
#include "player.h"
#include "chr.h"
#include "chraction.h"
#include "front.h"
#include "lv.h"
#include "bondview.h"
#include "system.h"
#include "simulant_facility_nav.h"

extern u16 get_player_mp_char_head(s32 player);
extern u16 get_player_mp_char_body(s32 player);
extern PropRecord *chrSpawnAtCoord(s32 body, s32 head, coord3d *pos,
                                   StandTile *stan, f32 angle,
                                   AIListRecord *ailist, s32 flags);
extern waypoint *chrlvStanPathRelated(coord3d *pos, StandTile *stan);

/* This ID is outside the normal stage guard numbering (which starts at 5000).
 * The slot is checked against the current stage's array before each use. */
#define SIM_PROBE_CHRNUM 0x6fff

/* The solo and MP Facility pad lists have identical positions and indexes.
 * Verify the live stage before installing the original solo path tables: the
 * MP setup's objects, spawn pads and original pads stay untouched. */
static int simProbeInstallFacilityNav(int stage)
{
    static PadRecord *failedPads;
    if (stage != LEVELID_FACILITY && stage != LEVELID_FACILITY_MP) return 0;
    if (g_CurrentSetup.pathwaypoints == simFacilityWaypoints &&
        g_CurrentSetup.waypointgroups == simFacilityGroups) return 1;
    if (failedPads == g_CurrentSetup.pads && failedPads) return 0;
    if (!g_CurrentSetup.pads || !g_CurrentSetup.pathwaypoints ||
        !g_CurrentSetup.waypointgroups ||
        g_CurrentSetup.pathwaypoints[0].padID >= 0 ||
        g_CurrentSetup.waypointgroups[0].neighbours) return 0;

    for (int i = 0; i < 311; ++i) {
        PadRecord *pad = &g_CurrentSetup.pads[i];
        if (!pad->plink || strcmp(pad->plink, simFacilityPadNames[i])) {
            sysLogPrintf(LOG_NOTE, "sim probe: Facility pad %d mismatch; navigation disabled", i);
            failedPads = g_CurrentSetup.pads;
            return 0;
        }
    }
    if (g_CurrentSetup.pads[311].plink) {
        sysLogPrintf(LOG_NOTE, "sim probe: Facility pad count mismatch; navigation disabled");
        failedPads = g_CurrentSetup.pads;
        return 0;
    }
    for (int i = 0; i < 157; ++i) {
        if (!g_CurrentSetup.pads[simFacilityWaypoints[i].padID].stan) {
            sysLogPrintf(LOG_NOTE, "sim probe: waypoint %d has no STAN; navigation disabled", i);
            failedPads = g_CurrentSetup.pads;
            return 0;
        }
    }
    g_CurrentSetup.pathwaypoints = simFacilityWaypoints;
    g_CurrentSetup.waypointgroups = simFacilityGroups;
    sysLogPrintf(LOG_NOTE, "sim probe: installed original Facility navigation (157 waypoints, 22 groups)");
    return 1;
}

/* One deliberately simple goal: walk to a waypoint near a living player.
 * The engine computes the path, handles STAN collision and activates doors. */
static void simProbeWalk(ChrRecord *chr, unsigned polls, unsigned *retryAfter,
                         int *lastGoal)
{
    int goal = -1;
    float best = 1.0e30f;
    waypoint *start;
    if (chr->actiontype == ACT_DIE || chr->actiontype == ACT_DEAD ||
        chr->actiontype == ACT_GOPOS || polls < *retryAfter) return;
    start = chrlvStanPathRelated(&chr->prop->pos, chr->prop->stan);
    if (!start) {
        *retryAfter = polls + 90;
        sysLogPrintf(LOG_NOTE, "sim probe: no waypoint near character STAN");
        return;
    }
    for (int p = 0; p < getPlayerCount() && p < 4; ++p) {
        const struct player *player = g_playerPointers[p];
        if (!player || !player->prop || player->bonddead) continue;
        for (int i = 0; i < 157; ++i) {
            if (&simFacilityWaypoints[i] == start) continue;
            PadRecord *pad = &g_CurrentSetup.pads[simFacilityWaypoints[i].padID];
            float dx = pad->pos.x - player->prop->pos.x;
            float dz = pad->pos.z - player->prop->pos.z;
            float dy = pad->pos.y - player->prop->pos.y;
            float dist = dx * dx + dz * dz + 16.0f * dy * dy;
            if (dist < best) { best = dist; goal = i; }
        }
    }
    *retryAfter = polls + 90;
    if (goal >= 0 && goal != *lastGoal) {
        int padid = simFacilityWaypoints[goal].padID;
        int ok = chrGoToPad(chr, padid, SPEED_WALK);
        sysLogPrintf(LOG_NOTE, "sim probe: route to waypoint %d (pad %d): %s",
                     goal, padid, ok ? "walking" : "unreachable");
        if (ok) *lastGoal = goal;
    }
}

void simulantProbePoll(void)
{
    static int enabled = -1;
    static int attempts;
    static unsigned polls;
    static unsigned retryAfter;
    static unsigned deathPoll;
    static int waitingForPadLogged;
    static int lastStage = -1;
    static unsigned routeRetryAfter;
    static int lastGoal = -1;
    int stage;
    PadRecord *bestPad = NULL;
    int bestPadIndex = -1;
    float bestDistance = -1.0f;

    if (enabled < 0) {
        const char *value = getenv("GE_MP_SIM_PROBE");
        enabled = value && value[0] == '1' && value[1] == '\0';
    }
    if (!enabled) return;

    stage = lvlGetCurrentStageToLoad();
    if (current_menu != MENU_RUN_STAGE || gamemode != GAMEMODE_MULTI
        || getPlayerCount() < 2 || stage <= 0) {
        attempts = 0;
        polls = retryAfter = 0;
        deathPoll = 0;
        waitingForPadLogged = 0;
        lastStage = -1;
        routeRetryAfter = 0;
        lastGoal = -1;
        return;
    }
    if (stage != lastStage) {
        attempts = 0;
        polls = retryAfter = 0;
        deathPoll = 0;
        waitingForPadLogged = 0;
        lastStage = stage;
        routeRetryAfter = 0;
        lastGoal = -1;
    }
    ++polls;
    if (!g_ChrSlots || g_NumChrSlots < 3) return;

    /* Stage setup and its STAN links have been initialized by this point. */
    int navReady = simProbeInstallFacilityNav(stage);

    /* Keep the actor through its full death animation. GoldenEye removes a
     * dead chr from its character slot after the corpse finishes fading. */
    for (int i = 0; i < g_NumChrSlots; ++i) {
        if (g_ChrSlots[i].model && g_ChrSlots[i].chrnum == SIM_PROBE_CHRNUM) {
            ChrRecord *chr = &g_ChrSlots[i];
            if (chr->actiontype == ACT_DIE || chr->actiontype == ACT_DEAD) {
                if (!deathPoll) {
                    deathPoll = polls;
                    sysLogPrintf(LOG_NOTE, "sim probe: character died; waiting for corpse removal");
                }
            }
            if (navReady) simProbeWalk(chr, polls, &routeRetryAfter, &lastGoal);
            return;
        }
    }
    if (deathPoll && polls - deathPoll < 180) return;
    if (deathPoll) {
        deathPoll = 0;
        attempts = 0;
        retryAfter = 0;
        routeRetryAfter = 0;
        lastGoal = -1;
        sysLogPrintf(LOG_NOTE, "sim probe: respawning character");
    }
    if (attempts >= 5 || polls < retryAfter) return;

    /* GE's player start pads are valid in MP setups. The character spawn
     * helper can accept an occupied VIEWER position, so explicitly require
     * clearance from every living player before calling it. */
    for (int padIndex = 0; padIndex < startpadcount && padIndex < 16; ++padIndex) {
        PadRecord *pad = g_Startpad[padIndex];
        float nearest = 1.0e30f;
        if (!pad || !pad->stan) continue;
        for (int p = 0; p < getPlayerCount() && p < 4; ++p) {
            const struct player *player = g_playerPointers[p];
            if (!player || !player->prop || player->bonddead) continue;
            float dx = pad->pos.x - player->prop->pos.x;
            float dz = pad->pos.z - player->prop->pos.z;
            /* Player props are at eye height, pads at floor height. Use X/Z
             * clearance so an occupied pad cannot pass the distance test. */
            float distance = dx * dx + dz * dz;
            if (distance < nearest) nearest = distance;
        }
        if (nearest > bestDistance) {
            bestDistance = nearest;
            bestPad = pad;
            bestPadIndex = padIndex;
        }
    }
    if (!bestPad || bestDistance < 250.0f * 250.0f) {
        if (!waitingForPadLogged) {
            sysLogPrintf(LOG_NOTE, "sim probe: waiting for an unoccupied multiplayer spawn pad");
            waitingForPadLogged = 1;
        }
        return;
    }
    waitingForPadLogged = 0;

    ++attempts;
    retryAfter = polls + 120;
    /* The pad supplies floor height and stan; a player prop is at eye height.
     * chrSpawnAtCoord performs its own final collision/volume check. */
    PropRecord *spawned = chrSpawnAtCoord(get_player_mp_char_body(0),
                                          get_player_mp_char_head(0),
                                          &bestPad->pos, bestPad->stan,
                                          0.0f, NULL, 0x10);
    if (!spawned || !spawned->chr) {
        sysLogPrintf(LOG_NOTE, "sim probe: character spawn failed in stage %d", stage);
        return;
    }
    /* chrAllocate sets ACT_STAND but does not initialize its action union;
     * stage AI scripts normally do that on their first tick. This character
     * intentionally has no stage AI script. */
    memset(&spawned->chr->act_stand, 0, sizeof(spawned->chr->act_stand));
    chrlvMergeKneelToStand(spawned->chr, 16.0f);
    spawned->chr->chrnum = SIM_PROBE_CHRNUM;
    attempts = 0;
    routeRetryAfter = polls + 30;
    lastGoal = -1;
    sysLogPrintf(LOG_NOTE, "sim probe: spawned character %d in stage %d at pad %d (%.0f %.0f %.0f)",
                 SIM_PROBE_CHRNUM, stage, bestPadIndex, (double)spawned->pos.x,
                 (double)spawned->pos.y, (double)spawned->pos.z);
}
