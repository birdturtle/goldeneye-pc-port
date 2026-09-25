/* Experimental separate-character Simulant proof. Enable with
 * GE_MP_SIM_PROBE=1; no player/controller slot is consumed. */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "port_math.h"

#include "simulant_probe.h"
#include "model_life.h"
#include "player.h"
#include "chrobjdata.h"
#include "chr.h"
#include "chraction.h"
#include "front.h"
#include "lv.h"
#include "bondview.h"
#include "system.h"
#include "chrai.h"
#include "mp_weapon.h"
#include "model.h"
#include "stan.h"
#include "gun.h"
#include "mpmenu.h"
#include "propobj.h"
#include "music.h"
#include "snd.h"
#include "simulant_facility_nav.h"

extern u16 get_player_mp_char_head(s32 player);
extern u16 get_player_mp_char_body(s32 player);
extern PropRecord *chrSpawnAtCoord(s32 body, s32 head, coord3d *pos,
                                   StandTile *stan, f32 angle,
                                   AIListRecord *ailist, s32 flags);
extern waypoint *chrlvStanPathRelated(coord3d *pos, StandTile *stan);
extern PropRecord *chrGiveWeapon(ChrRecord *chr, s32 model, ITEM_IDS item, s32 flags);
extern void bondviewKillCurrentPlayer(void);
extern void increment_num_deaths(void);

/* This ID is outside the normal stage guard numbering (which starts at 5000).
 * The slot is checked against the current stage's array before each use. */
#define SIM_PROBE_CHRNUM 0x6fff
#define SIM_ROUTE_REPLAN_POLLS 90
#define SIM_ROUTE_STALL_POLLS 240
#define SIM_FIRE_INTERVAL_POLLS 24
#define SIM_FIRE_RANGE 1000.0f
#define SIM_FLASH_POLLS 3
#define SIM_RECOIL_POLLS 5

typedef struct SimProbeRoute {
    unsigned retryAfter;
    unsigned sampleAt;
    unsigned blockedUntil;
    float sampleX, sampleZ;
    int goal;
    int blockedGoal;
    int stalls;
    PropRecord *weaponTarget;
    PropRecord *blockedWeapon;
    unsigned weaponBlockedUntil;
    int noWeaponLogged;
    unsigned nextShot;
    unsigned flashUntil;
    unsigned recoilUntil;
} SimProbeRoute;

typedef struct SimProbeRuntime {
    int attempts;
    unsigned polls;
    unsigned retryAfter;
    unsigned deathPoll;
    int waitingForPadLogged;
    int lastStage;
    PadRecord *failedPads;
    SimProbeRoute route;
} SimProbeRuntime;

static SimProbeRuntime s_probe = { .lastStage = -1, .route = { .goal = -1, .blockedGoal = -1 } };
/* One byte per body/head ID: only models used by this probe are invalidated. */
static unsigned char s_botModelUsed[HEAD_END];
static int s_lastBotBody = -1;
static int s_lastBotHead = -1;
static waypoint *s_originalWaypoints;
static waygroup *s_originalGroups;
static int s_readyStage = -1;

void simulantProbeStageReady(int stage)
{
    s_readyStage = stage;
    if (getenv("GE_D86")) {
        fprintf(stderr, "[D86] sim probe stage ready stage=%d\n", stage);
        fflush(stderr);
    }
}

static void simProbeResetRoute(SimProbeRoute *route)
{
    memset(route, 0, sizeof(*route));
    route->goal = route->blockedGoal = -1;
}

static void simProbeResetRuntime(void)
{
    /* Includes route weapon/target props and the failed Facility pads.
     * Chr, held weapon, player target, STAN and waypoint pointers are
     * obtained inside each poll and never cached beyond that call. */
    memset(&s_probe, 0, sizeof(s_probe));
    s_probe.lastStage = -1;
    simProbeResetRoute(&s_probe.route);
}

static void simProbeLogModel(const char *event, int body, int head, int id)
{
    static int d86 = -1;
    if (d86 < 0) d86 = getenv("GE_D86") != NULL;
    if (d86 && id >= 0 && id < HEAD_END) {
        ModelFileHeader *header = c_item_entries[id].header;
        fprintf(stderr,
                "[D86] sim probe %s body=%d head=%d model=%d header=%p RootNode=%p Switches=%p Textures=%p\n",
                event, body, head, id, (void *)header,
                header ? (void *)header->RootNode : NULL,
                header ? (void *)header->Switches : NULL,
                header ? (void *)header->Textures : NULL);
        fflush(stderr);
    }
}

void simulantProbeStageTeardown(void)
{
    s_readyStage = -1;
    /* Restore the stage's own tables after character/object cleanup, before
     * dropping the probe's route and any STAN-linked stage references. */
    if (g_CurrentSetup.pathwaypoints == simFacilityWaypoints && s_originalWaypoints)
        g_CurrentSetup.pathwaypoints = s_originalWaypoints;
    if (g_CurrentSetup.waypointgroups == simFacilityGroups && s_originalGroups)
        g_CurrentSetup.waypointgroups = s_originalGroups;
    s_originalWaypoints = NULL;
    s_originalGroups = NULL;
    for (int id = 0; id < HEAD_END; ++id) {
        if (s_botModelUsed[id]) {
            ModelFileHeader *header = c_item_entries[id].header;
            simProbeLogModel("invalidate", s_lastBotBody, s_lastBotHead, id);
            modelLifeEvent("BOT_HEADER_INVALIDATE_BEFORE", header);
            if (header) {
                header->RootNode = NULL;
                header->Switches = NULL;
                header->Textures = NULL;
                header->numRecords = 0;
            }
            modelLifeEvent("BOT_HEADER_INVALIDATE_AFTER", header);
        }
    }
    memset(s_botModelUsed, 0, sizeof(s_botModelUsed));
    s_lastBotBody = s_lastBotHead = -1;
    simProbeResetRuntime();
}

/* The solo and MP Facility pad lists have identical positions and indexes.
 * Verify the live stage before installing the original solo path tables: the
 * MP setup's objects, spawn pads and original pads stay untouched. */
static int simProbeInstallFacilityNav(int stage)
{
    if (stage != LEVELID_FACILITY && stage != LEVELID_FACILITY_MP) return 0;
    if (g_CurrentSetup.pathwaypoints == simFacilityWaypoints &&
        g_CurrentSetup.waypointgroups == simFacilityGroups) return 1;
    if (s_probe.failedPads == g_CurrentSetup.pads && s_probe.failedPads) return 0;
    if (!g_CurrentSetup.pads || !g_CurrentSetup.pathwaypoints ||
        !g_CurrentSetup.waypointgroups ||
        g_CurrentSetup.pathwaypoints[0].padID >= 0 ||
        g_CurrentSetup.waypointgroups[0].neighbours) return 0;

    for (int i = 0; i < 311; ++i) {
        PadRecord *pad = &g_CurrentSetup.pads[i];
        if (!pad->plink || strcmp(pad->plink, simFacilityPadNames[i])) {
            sysLogPrintf(LOG_NOTE, "sim probe: Facility pad %d mismatch; navigation disabled", i);
            s_probe.failedPads = g_CurrentSetup.pads;
            return 0;
        }
    }
    if (g_CurrentSetup.pads[311].plink) {
        sysLogPrintf(LOG_NOTE, "sim probe: Facility pad count mismatch; navigation disabled");
        s_probe.failedPads = g_CurrentSetup.pads;
        return 0;
    }
    for (int i = 0; i < 157; ++i) {
        if (!g_CurrentSetup.pads[simFacilityWaypoints[i].padID].stan) {
            sysLogPrintf(LOG_NOTE, "sim probe: waypoint %d has no STAN; navigation disabled", i);
            s_probe.failedPads = g_CurrentSetup.pads;
            return 0;
        }
    }
    s_originalWaypoints = g_CurrentSetup.pathwaypoints;
    s_originalGroups = g_CurrentSetup.waypointgroups;
    g_CurrentSetup.pathwaypoints = simFacilityWaypoints;
    g_CurrentSetup.waypointgroups = simFacilityGroups;
    sysLogPrintf(LOG_NOTE, "sim probe: installed original Facility navigation (157 waypoints, 22 groups)");
    return 1;
}

/* Only stage-owned, respawning firearms from the selected MP weapon set.
 * Dropped items and special scenario objects need their own pickup rules. */
static int simProbeUsableWorldGun(PropRecord *prop)
{
    WeaponObjRecord *weapon;
    struct s_mp_weapon_set *set;
    if (!prop || !(prop->flags & PROPFLAG_ENABLED) ||
        (prop->type != PROP_TYPE_OBJ && prop->type != PROP_TYPE_WEAPON) ||
        prop->parent || prop->timetoregen > 0 || !prop->stan || !prop->obj ||
        prop->obj->type != PROPDEF_COLLECTABLE ||
        !(prop->obj->state & PROPSTATE_RESPAWN) ||
        (prop->obj->runtime_bitflags & RUNTIMEBITFLAG_00000800) ||
        (prop->obj->flags & (PROPFLAG_UNCOLLECTABLE | PROPFLAG_ASSIGNEDTOCHR | PROPFLAG_INSIDEANOTHEROBJ)))
        return 0;
    weapon = prop->weapon;
    if (weapon->prop != prop || !weapon->model ||
        weapon->weaponnum < ITEM_WPPK ||
        (weapon->weaponnum > ITEM_RUGER && weapon->weaponnum != ITEM_LASER))
        return 0;
    set = getPtrMPWeaponSetData();
    for (int i = 0; i < 8; ++i) {
        if (set[i].allowpickup && set[i].itemID == weapon->weaponnum &&
            set[i].propID == weapon->obj) return 1;
    }
    return 0;
}

/* Return true while a world gun is the navigation goal. Keep a real pickup
 * and its respawn timer; the separate held copy uses GoldenEye's chr weapon
 * allocation path. This does not fire or credit a shot to a player slot. */
static int simProbeSeekWeapon(ChrRecord *chr, unsigned polls, SimProbeRoute *route)
{
    PropRecord *nearest = NULL;
    PropRecord *locked = NULL;
    float nearestDist = 1.0e30f;
    int seen = 0;
    if (chrGetEquippedWeaponProp(chr, GUNRIGHT) ||
        chrGetEquippedWeaponProp(chr, GUNLEFT)) {
        route->weaponTarget = NULL;
        route->noWeaponLogged = 0;
        return 0;
    }

    for (PropRecord *prop = g_ActivePropsHead; prop && seen++ < MAX_PROPS;
         prop = prop->next) {
        if (!simProbeUsableWorldGun(prop) ||
            (prop == route->blockedWeapon && polls < route->weaponBlockedUntil))
            continue;
        if (prop == route->weaponTarget) locked = prop;
        float dx = prop->pos.x - chr->prop->pos.x;
        float dy = prop->pos.y - chr->prop->pos.y;
        float dz = prop->pos.z - chr->prop->pos.z;
        float dist = dx * dx + dz * dz + 16.0f * dy * dy;
        if (dist < nearestDist) { nearestDist = dist; nearest = prop; }
    }
    nearest = locked ? locked : nearest;
    if (!nearest) {
        route->weaponTarget = NULL;
        if (!route->noWeaponLogged) {
            sysLogPrintf(LOG_NOTE, "sim probe: no available world firearm; pursuing unarmed");
            route->noWeaponLogged = 1;
        }
        return 0;
    }
    route->noWeaponLogged = 0;

    float dx = nearest->pos.x - chr->prop->pos.x;
    float dy = nearest->pos.y - chr->prop->pos.y;
    float dz = nearest->pos.z - chr->prop->pos.z;
    if (dx * dx + dz * dz < 135.0f * 135.0f &&
        dy * dy < 150.0f * 150.0f) {
        WeaponObjRecord *weapon = nearest->weapon;
        int item = weapon->weaponnum;
        int pad = weapon->pad;
        if (chrGiveWeapon(chr, weapon->obj, item, 0)) {
            propExecuteTickOperation(nearest, TICKOP_FREE);
            sysLogPrintf(LOG_NOTE, "sim probe: picked up item %d at pad %d; world pickup respawning",
                         item, pad);
            route->weaponTarget = NULL;
            route->goal = -1;
            route->retryAfter = polls + 30;
            return 1;
        }
        sysLogPrintf(LOG_NOTE, "sim probe: unable to equip item %d", item);
        route->blockedWeapon = nearest;
        route->weaponBlockedUntil = polls + 360;
        route->weaponTarget = NULL;
        return 1;
    }

    if (route->weaponTarget == nearest &&
        polls - route->sampleAt >= SIM_ROUTE_STALL_POLLS) {
        float movedX = chr->prop->pos.x - route->sampleX;
        float movedZ = chr->prop->pos.z - route->sampleZ;
        if (chr->actiontype == ACT_GOPOS &&
            movedX * movedX + movedZ * movedZ < 64.0f * 64.0f &&
            dx * dx + dz * dz > 180.0f * 180.0f) {
            sysLogPrintf(LOG_NOTE, "sim probe: stalled en route to weapon at pad %d",
                         nearest->weapon->pad);
            route->blockedWeapon = nearest;
            route->weaponBlockedUntil = polls + 360;
            route->weaponTarget = NULL;
            route->retryAfter = polls + 120;
            route->sampleAt = polls;
            return 1;
        }
        route->sampleAt = polls;
        route->sampleX = chr->prop->pos.x;
        route->sampleZ = chr->prop->pos.z;
    }

    if (polls >= route->retryAfter &&
        (route->weaponTarget != nearest || chr->actiontype != ACT_GOPOS)) {
        int pad = nearest->weapon->pad;
        int ok = chrGoToPad(chr, pad, SPEED_RUN);
        sysLogPrintf(LOG_NOTE, "sim probe: route to weapon item %d at pad %d: %s",
                     nearest->weapon->weaponnum, pad, ok ? "running" : "unreachable");
        route->retryAfter = polls + SIM_ROUTE_REPLAN_POLLS;
        if (ok) {
            route->weaponTarget = nearest;
            route->goal = -1;
            route->sampleAt = polls;
            route->sampleX = chr->prop->pos.x;
            route->sampleZ = chr->prop->pos.z;
        } else {
            route->blockedWeapon = nearest;
            route->weaponBlockedUntil = polls + 360;
            route->weaponTarget = NULL;
        }
    }
    return 1;
}

/* Damage a player with a non-player attacker. The original NPC shot helper
 * supplies playerid=-1 to record_damage_kills, which indexes multiplayer
 * score arrays with that value. Keep the original health/armor/death path,
 * but attribute no shot or kill to any human player slot. */
static void simProbeDamagePlayer(int victim, float amount, float dx, float dz)
{
    static unsigned probeKills;
    int previous = get_cur_playernum();
    struct player *player;
    if (victim < 0 || victim >= getPlayerCount() || victim >= 4 ||
        !g_playerPointers[victim] || g_stopPlayFlag || g_gameOverFlag) return;

    set_cur_player(victim);
    player = g_CurrentPlayer;
    if (!player->bonddead && !player->cheatBondInvincible && !g_PlayerInvincible &&
        player->watch_animation_state != WATCH_ANIMATION_0x5 &&
        player->watch_animation_state != WATCH_ANIMATION_0xc &&
        (player->damageshowtime < 0 || player->damageshowtime == 0)) {
        float damage = amount;
        player->oldhealth = player->bondhealth;
        player->oldarmour = player->bondarmour;
        if (get_scenario() == SCENARIO_LTK) {
            damage = player->bondhealth * player->actual_health +
                     player->bondarmour * player->actual_armor;
        }
        if (damage <= player->bondarmour * player->actual_armor) {
            player->bondarmour -= damage / player->actual_armor;
        } else {
            /* Match record_damage_kills' armor depletion and player death
             * handling, without its playerid-indexed scoring writes. */
            damage -= player->bondarmour / player->actual_armor;
            player->bondarmour = 0.0f;
            player->actual_armor = 1.0f;
            player->bondhealth -= damage / player->actual_health;
            if (player->bondhealth <= 0.0f) {
                drop_inventory();
                increment_num_deaths();
                bondviewKillCurrentPlayer();
                ++probeKills;
                sysLogPrintf(LOG_NOTE, "sim probe: eliminated player %d (probe kills %u)",
                             victim + 1, probeKills);
            }
        }
        if (player->damageshowtime < 0) {
            player->bondshotspeed.x += 2.0f * dx;
            player->bondshotspeed.z += 2.0f * dz;
        }
        player->damageshowtime = 0;
        player->healthshowtime = 0;
    }
    set_cur_player(previous);
}

/* A deliberately modest first engagement: no projectile or explosion paths,
 * and a wall/closed door blocks both target acquisition and damage. */
static void simProbeCombat(ChrRecord *chr, unsigned polls, SimProbeRoute *route)
{
    PropRecord *gun = chrGetEquippedWeaponProp(chr, GUNRIGHT);
    float nearest = SIM_FIRE_RANGE * SIM_FIRE_RANGE;
    int victim = -1;
    if (!gun || !gun->weapon || !chr->model || !chr->prop || !chr->prop->stan ||
        (chr->actiontype != ACT_STAND && chr->actiontype != ACT_GOPOS)) return;
    int item = gun->weapon->weaponnum;
    if (item < ITEM_WPPK || item > ITEM_RUGER) {
        if (route->flashUntil) chrSetFiring(chr, GUNRIGHT, 0);
        route->flashUntil = route->recoilUntil = 0;
        return;
    }

    /* The old one-poll pulse was too brief to reliably appear in either
     * split-screen viewport. Keep the normal gun model's flash switch lit
     * for a few controller polls, then clear it even if sight is lost. */
    if (route->flashUntil && polls >= route->flashUntil) {
        chrSetFiring(chr, GUNRIGHT, 0);
        route->flashUntil = 0;
    }

    for (int i = 0; i < getPlayerCount() && i < 4; ++i) {
        const struct player *player = g_playerPointers[i];
        StandTile *tile = chr->prop->stan;
        if (!player || !player->prop || !player->prop->stan || player->bonddead) continue;
        float dx = player->prop->pos.x - chr->prop->pos.x;
        float dy = player->prop->pos.y - chr->prop->pos.y;
        float dz = player->prop->pos.z - chr->prop->pos.z;
        float dist = dx * dx + dz * dz + 16.0f * dy * dy;
        if (dist >= nearest) continue;
        chrSetMoving(chr, 0);
        int visible = stanTestLineUnobstructed(&tile, chr->prop->pos.x,
                           chr->prop->pos.z, player->prop->pos.x,
                           player->prop->pos.z,
                           CDTYPE_OBJS | CDTYPE_DOORS | CDTYPE_PATHBLOCKER |
                           CDTYPE_AIOPAQUE, chr->chrheight - 20.0f,
                           chr->chrheight - 20.0f, 0.0f, 1.0f);
        chrSetMoving(chr, 1);
        if (visible && tile == player->prop->stan) { nearest = dist; victim = i; }
    }
    if (victim < 0) {
        if (route->recoilUntil) {
            chr->aimendrshoulder = 0.0f;
            chr->aimendcount = 6;
            route->recoilUntil = 0;
        }
        return;
    }

    const struct player *target = g_playerPointers[victim];
    float dx = target->prop->pos.x - chr->prop->pos.x;
    float dz = target->prop->pos.z - chr->prop->pos.z;
    float dy = target->prop->pos.y - (chr->prop->pos.y + 30.0f);
    float elevation = atan2f(dy, sqrtf(dx * dx + dz * dz));
    if (elevation > 0.45f) elevation = 0.45f;
    if (elevation < -0.45f) elevation = -0.45f;
    /* chrUpdateAimProperties interpolates this target into the right-arm
     * rotation used by chrRender. It does not change ACT_GOPOS or its route. */
    chr->aimendrshoulder = elevation +
                            (polls < route->recoilUntil ? 0.18f : 0.0f);
    chr->aimendcount = 6;
    float desired = atan2f(dx, dz);
    float heading = getsubroty(chr->model);
    float delta = atan2f(sinf(desired - heading), cosf(desired - heading));
    float step = 0.075f;
    if (delta > step) delta = step;
    if (delta < -step) delta = -step;
    setsubroty(chr->model, heading + delta);
    if (fabsf(atan2f(sinf(desired - getsubroty(chr->model)),
                     cosf(desired - getsubroty(chr->model)))) > 0.12f ||
        polls < route->nextShot) return;

    route->nextShot = polls + SIM_FIRE_INTERVAL_POLLS;
    route->flashUntil = polls + SIM_FLASH_POLLS;
    route->recoilUntil = polls + SIM_RECOIL_POLLS;
    chr->aimendrshoulder = elevation + 0.18f;
    chr->aimendcount = 2;
    chrSetFiring(chr, GUNRIGHT, 1);
    /* Use the same per-weapon sound ID and positional audio hook as guards.
     * The original NPC firing routine also applies unsafe MP shot ownership,
     * so only its feedback path is reproduced here. */
    u16 sound = bondwalkItemGetSound(item);
    if (sound) {
        ALSoundState *playing = sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr,
                                           sound, NULL);
        if (playing) chrobjSndCreatePostEventDefault(playing, &chr->prop->pos);
    }
    simProbeDamagePlayer(victim, 0.125f * gunItemGetDestructionAmount(item),
                         dx / sqrtf(dx * dx + dz * dz + 1.0f),
                         dz / sqrtf(dx * dx + dz * dz + 1.0f));
}

/* GoldenEye owns route traversal, collision and doors. Periodically update
 * the destination when the closest living player changes position. */
static void simProbeWalk(ChrRecord *chr, unsigned polls, SimProbeRoute *route)
{
    int goal = -1;
    float best = 1.0e30f;
    float targetDist = 1.0e30f;
    const struct player *target = NULL;
    waypoint *start;
    if (!chr->prop || !chr->prop->stan ||
        (chr->actiontype != ACT_GOPOS && chr->actiontype != ACT_STAND)) return;

    if (simProbeSeekWeapon(chr, polls, route)) return;

    if (!route->sampleAt) {
        route->sampleAt = polls;
        route->sampleX = chr->prop->pos.x;
        route->sampleZ = chr->prop->pos.z;
    }
    if (polls - route->sampleAt >= SIM_ROUTE_STALL_POLLS) {
        float dx = chr->prop->pos.x - route->sampleX;
        float dz = chr->prop->pos.z - route->sampleZ;
        if (chr->actiontype == ACT_GOPOS && route->goal >= 0) {
            const PadRecord *pad = &g_CurrentSetup.pads[simFacilityWaypoints[route->goal].padID];
            float gx = chr->prop->pos.x - pad->pos.x;
            float gz = chr->prop->pos.z - pad->pos.z;
            if (dx * dx + dz * dz < 64.0f * 64.0f &&
                gx * gx + gz * gz > 180.0f * 180.0f) {
                ++route->stalls;
                sysLogPrintf(LOG_NOTE, "sim probe: stalled on waypoint %d (%d); replanning",
                             route->goal, route->stalls);
                if (route->stalls >= 2) {
                    route->blockedGoal = route->goal;
                    route->blockedUntil = polls + 360;
                    route->retryAfter = polls + 120;
                } else {
                    route->retryAfter = polls;
                }
                route->goal = -1;
            } else if (dx * dx + dz * dz >= 64.0f * 64.0f) {
                route->stalls = 0;
            }
        }
        route->sampleAt = polls;
        route->sampleX = chr->prop->pos.x;
        route->sampleZ = chr->prop->pos.z;
    }
    if (polls < route->retryAfter) return;
    start = chrlvStanPathRelated(&chr->prop->pos, chr->prop->stan);
    if (!start) {
        route->retryAfter = polls + SIM_ROUTE_REPLAN_POLLS;
        sysLogPrintf(LOG_NOTE, "sim probe: no waypoint near character STAN");
        return;
    }
    route->retryAfter = polls + SIM_ROUTE_REPLAN_POLLS;
    for (int p = 0; p < getPlayerCount() && p < 4; ++p) {
        const struct player *player = g_playerPointers[p];
        if (!player || !player->prop || player->bonddead) continue;
        float dx = chr->prop->pos.x - player->prop->pos.x;
        float dy = chr->prop->pos.y - player->prop->pos.y;
        float dz = chr->prop->pos.z - player->prop->pos.z;
        float dist = dx * dx + dz * dz + 16.0f * dy * dy;
        if (dist < targetDist) { targetDist = dist; target = player; }
    }
    if (!target) return;
    {
        float dx = chr->prop->pos.x - target->prop->pos.x;
        float dz = chr->prop->pos.z - target->prop->pos.z;
        /* No attack behavior yet. Don't circle an adjacent player just to
         * reach the next waypoint while the player is standing still. */
        if (dx * dx + dz * dz < 200.0f * 200.0f) return;
    }
    for (int i = 0; i < 157; ++i) {
        if (&simFacilityWaypoints[i] == start ||
            (i == route->blockedGoal && polls < route->blockedUntil)) continue;
        PadRecord *pad = &g_CurrentSetup.pads[simFacilityWaypoints[i].padID];
        float dx = pad->pos.x - target->prop->pos.x;
        float dz = pad->pos.z - target->prop->pos.z;
        float dy = pad->pos.y - target->prop->pos.y;
        float dist = dx * dx + dz * dz + 16.0f * dy * dy;
        if (dist < best) { best = dist; goal = i; }
    }
    if (chr->actiontype == ACT_STAND && route->goal >= 0) {
        const PadRecord *pad = &g_CurrentSetup.pads[simFacilityWaypoints[route->goal].padID];
        float dx = chr->prop->pos.x - pad->pos.x;
        float dz = chr->prop->pos.z - pad->pos.z;
        if (dx * dx + dz * dz > 180.0f * 180.0f) route->goal = -1;
    }
    if (goal >= 0 && goal != route->goal) {
        int padid = simFacilityWaypoints[goal].padID;
        int ok = chrGoToPad(chr, padid, SPEED_RUN);
        sysLogPrintf(LOG_NOTE, "sim probe: route to waypoint %d (pad %d): %s",
                     goal, padid, ok ? "running" : "unreachable");
        if (ok) route->goal = goal;
    }
}

void simulantProbePoll(void)
{
    static int enabled = -1;
    SimProbeRuntime *probe = &s_probe;
    SimProbeRoute *route = &probe->route;
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
    /* inputUpdate can run during lvlStageLoad, before init_guards and
     * bodiesReset replace the previous match's stage-owned data. */
    if (stage != s_readyStage) return;
    if (current_menu != MENU_RUN_STAGE || gamemode != GAMEMODE_MULTI
        || getPlayerCount() < 2 || stage <= 0) {
        simProbeResetRuntime();
        return;
    }
    if (stage != probe->lastStage) {
        simProbeResetRuntime();
        probe->lastStage = stage;
    }
    ++probe->polls;
    if (!g_ChrSlots || g_NumChrSlots < 3) return;

    /* Stage setup and its STAN links have been initialized by this point. */
    int navReady = simProbeInstallFacilityNav(stage);

    /* Keep the actor through its full death animation. GoldenEye removes a
     * dead chr from its character slot after the corpse finishes fading. */
    for (int i = 0; i < g_NumChrSlots; ++i) {
        if (g_ChrSlots[i].model && g_ChrSlots[i].chrnum == SIM_PROBE_CHRNUM) {
            ChrRecord *chr = &g_ChrSlots[i];
            if (chr->actiontype == ACT_DIE || chr->actiontype == ACT_DEAD) {
                if (!probe->deathPoll) {
                    probe->deathPoll = probe->polls;
                    sysLogPrintf(LOG_NOTE, "sim probe: character died; waiting for corpse removal");
                }
            }
            if (navReady && chr->actiontype != ACT_DIE && chr->actiontype != ACT_DEAD) {
                simProbeWalk(chr, probe->polls, route);
                simProbeCombat(chr, probe->polls, route);
            }
            return;
        }
    }
    if (probe->deathPoll && probe->polls - probe->deathPoll < 180) return;
    if (probe->deathPoll) {
        probe->deathPoll = 0;
        probe->attempts = 0;
        probe->retryAfter = 0;
        simProbeResetRoute(route);
        sysLogPrintf(LOG_NOTE, "sim probe: respawning character");
    }
    if (probe->attempts >= 5 || probe->polls < probe->retryAfter) return;

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
        if (!probe->waitingForPadLogged) {
            sysLogPrintf(LOG_NOTE, "sim probe: waiting for an unoccupied multiplayer spawn pad");
            probe->waitingForPadLogged = 1;
        }
        return;
    }
    probe->waitingForPadLogged = 0;

    ++probe->attempts;
    probe->retryAfter = probe->polls + 120;
    /* The pad supplies floor height and stan; a player prop is at eye height.
     * chrSpawnAtCoord performs its own final collision/volume check. */
    int body = get_player_mp_char_body(0);
    int head = get_player_mp_char_head(0);
    modelLifeBot(body, head);
    if (body >= 0 && body < BODIES_MAX) {
        /* Remember each definition for stage teardown, not for every bot
         * respawn. Multiple bots in one stage share the loaded headers. */
        s_botModelUsed[body] = 1;
        if (!c_item_entries[body].hasHead && head >= 0 && head < HEAD_END)
            s_botModelUsed[head] = 1;
        s_lastBotBody = body;
        s_lastBotHead = head;
        simProbeLogModel("spawn before body", body, head, body);
        if (!c_item_entries[body].hasHead)
            simProbeLogModel("spawn before head", body, head, head);
    }
    PropRecord *spawned = chrSpawnAtCoord(body, head,
                                          &bestPad->pos, bestPad->stan,
                                          0.0f, NULL, 0x10);
    simProbeLogModel("spawn after body", body, head, body);
    if (spawned && spawned->chr && body >= 0 && body < BODIES_MAX &&
        !c_item_entries[body].hasHead) {
        /* chrSpawnAtCoord can resolve a random-head sentinel itself. Record
         * the actual attached model when it returns a live character. */
        int actualHead = spawned->chr->headnum;
        if (actualHead >= 0 && actualHead < HEAD_END) {
            s_botModelUsed[actualHead] = 1;
            s_lastBotHead = actualHead;
            simProbeLogModel("spawn after head", body, actualHead, actualHead);
        }
    }
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
    probe->attempts = 0;
    simProbeResetRoute(route);
    route->retryAfter = probe->polls + 30;
    sysLogPrintf(LOG_NOTE, "sim probe: spawned character %d in stage %d at pad %d (%.0f %.0f %.0f)",
                 SIM_PROBE_CHRNUM, stage, bestPadIndex, (double)spawned->pos.x,
                 (double)spawned->pos.y, (double)spawned->pos.z);
}
