/* Multiplayer player bot: GoldenEye waypoints choose steering goals; the
 * existing player simulation still performs every movement and collision. */
#include "port_math.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "botinput.h"
#include "player.h"
#include "chrai.h"
#include "boss.h"
#include "lv.h"
#include "botnav_data.h"
#include "system.h"

extern waypoint *chrlvStanPathRelated(coord3d *pos, StandTile *stan);
extern s32 waypointFindRoute(waypoint *from, waypoint *to, waypoint **arr, s32 len);

#define BOT_MAX_GRAPH 2048
#define BOT_REPLAN_POLLS 12
#define BOT_REACH 115.0f

typedef struct BotRoute {
    coord3d points[BOT_DEBUG_ROUTE_POINTS];
    int count, next, complete, target_index, timer, last_tick, stuck, escape, logged;
    int door_ticks, door_cooldown, door_attempts;
    PropRecord *door;
    float last_x, last_z;
    const struct player *target;
    const waypoint *graph;
    int stage;
    BotDebugRoute debug;
} BotRoute;

static BotRoute routes[4];

/* Only consider an activatable closed door close to the current path segment.
 * GE itself remains responsible for interaction range, keys and door motion. */
static PropRecord *botFindRouteDoor(const struct player *self,
                                    const coord3d *goal)
{
    float gx = goal->x - self->prop->pos.x;
    float gz = goal->z - self->prop->pos.z;
    float glen = sqrtf(gx * gx + gz * gz);
    if (glen < 0.001f) return NULL;
    gx /= glen;
    gz /= glen;

    PropRecord *bestDoor = NULL;
    float bestDist = 1.0e30f;
    int seen = 0;
    for (PropRecord *prop = g_ActivePropsHead; prop && seen++ < MAX_PROPS;
         prop = prop->next) {
        if (prop->type != PROP_TYPE_DOOR || !prop->door) continue;
        DoorRecord *door = prop->door;
        if ((door->flags & PROPFLAG_CANNOT_ACTIVATE) || door->maxFrac <= 0
            || door->openstate != DOORSTATE_STATIONARY
            || door->openPosition > 0.5f) continue;
        float dx = door->runtime_pos.x - self->prop->pos.x;
        float dy = door->runtime_pos.y - self->prop->pos.y;
        float dz = door->runtime_pos.z - self->prop->pos.z;
        float ahead = dx * gx + dz * gz;
        float sideways = dx * gz - dz * gx;
        float dist = dx * dx + dz * dz;
        if (ahead < 10.0f || ahead > 220.0f || fabsf(sideways) > 175.0f
            || fabsf(dy) > 180.0f || dist >= bestDist) continue;
        bestDoor = prop;
        bestDist = dist;
    }
    return bestDoor;
}

static int botNearestSoloPoint(const BotNavPoint *nodes, int count,
                               const coord3d *pos)
{
    int bestIndex = -1;
    float best = 1.0e30f;
    for (int i = 0; i < count; ++i) {
        float dx = nodes[i].x - pos->x;
        float dy = nodes[i].y - pos->y;
        float dz = nodes[i].z - pos->z;
        float score = dx * dx + dz * dz + 16.0f * dy * dy;
        if (score < best) { best = score; bestIndex = i; }
    }
    return bestIndex;
}

/* MP setups intentionally have empty waypoint tables. Use the same stage's
 * original solo graph, embedded as read-only coordinates and adjacency.
 * Bounded BFS keeps all route state in the port and never changes GE globals. */
static void botBuildSoloRoute(BotRoute *r, const struct player *self,
                              const struct player *target)
{
    r->count = r->next = r->complete = 0;
    const uint16_t *edges;
    int count;
    const BotNavPoint *nodes = botNavForStage(r->stage, &edges, &count);
    if (!r->logged) {
        sysLogPrintf(LOG_NOTE, "botnav: stage %d MP route unavailable; solo graph has %d nodes",
                     r->stage, count);
        r->logged = 1;
    }
    if (!nodes || count <= 0 || count > BOT_MAX_GRAPH) return;
    int from = botNearestSoloPoint(nodes, count, &self->prop->pos);
    int to = botNearestSoloPoint(nodes, count, &target->prop->pos);
    if (from < 0 || to < 0) return;

    int prev[BOT_MAX_GRAPH], queue[BOT_MAX_GRAPH], chain[BOT_MAX_GRAPH];
    for (int i = 0; i < count; ++i) prev[i] = -1;
    int head = 0, tail = 0;
    prev[from] = from;
    queue[tail++] = from;
    while (head < tail && prev[to] < 0) {
        int here = queue[head++];
        const BotNavPoint *point = &nodes[here];
        for (int j = 0; j < point->count; ++j) {
            int next = edges[point->first + j];
            if (next >= count || prev[next] >= 0) continue;
            prev[next] = here;
            queue[tail++] = next;
        }
    }
    if (prev[to] < 0) return;

    int length = 0;
    for (int i = to; ; i = prev[i]) {
        chain[length++] = i;
        if (i == from) break;
    }
    r->complete = length <= BOT_DEBUG_ROUTE_POINTS;
    r->count = length < BOT_DEBUG_ROUTE_POINTS ? length : BOT_DEBUG_ROUTE_POINTS;
    for (int i = 0; i < r->count; ++i) {
        const BotNavPoint *point = &nodes[chain[length - 1 - i]];
        r->points[i].x = point->x;
        r->points[i].y = point->y;
        r->points[i].z = point->z;
    }
    r->next = r->count > 1 ? 1 : 0;
}

static void botBuildRoute(BotRoute *r, const struct player *self,
                          const struct player *target)
{
    r->count = r->next = r->complete = 0;
    if (!g_CurrentSetup.pathwaypoints || !g_CurrentSetup.waypointgroups
        || !g_CurrentSetup.pads || !self->prop->stan || !target->prop->stan) {
        botBuildSoloRoute(r, self, target);
        return;
    }

    waypoint *graph = g_CurrentSetup.pathwaypoints;
    int nodes = 0;
    while (nodes < BOT_MAX_GRAPH && graph[nodes].padID >= 0) ++nodes;
    if (nodes == BOT_MAX_GRAPH || nodes == 0) {
        botBuildSoloRoute(r, self, target);
        return;
    }

    waypoint *from = chrlvStanPathRelated(&self->prop->pos, self->prop->stan);
    waypoint *to = chrlvStanPathRelated(&target->prop->pos, target->prop->stan);
    if (!from || !to) { botBuildSoloRoute(r, self, target); return; }
    uintptr_t begin = (uintptr_t)graph;
    uintptr_t end = begin + (size_t)nodes * sizeof(*graph);
    if ((uintptr_t)from < begin || (uintptr_t)from >= end
        || (uintptr_t)to < begin || (uintptr_t)to >= end) {
        botBuildSoloRoute(r, self, target);
        return;
    }

    /* The original in-group solver adds 9999 to arrlen, so a 64-entry
     * destination is unsafe. A simple route visits at most the graph's
     * nodes plus group boundary nodes; allocate twice that many slots. */
    int capacity = nodes * 2 + 8;
    waypoint **scratch = calloc((size_t)capacity, sizeof(*scratch));
    if (!scratch) { botBuildSoloRoute(r, self, target); return; }
    int written = waypointFindRoute(from, to, scratch, capacity);
    if (written > 1 && written <= capacity && scratch[written - 1] == NULL) {
        int length = written - 1;
        int full = length <= BOT_DEBUG_ROUTE_POINTS;
        if (length > BOT_DEBUG_ROUTE_POINTS) length = BOT_DEBUG_ROUTE_POINTS;
        for (int i = 0; i < length; ++i) {
            uintptr_t p = (uintptr_t)scratch[i];
            if (p < begin || p >= end || (p - begin) % sizeof(*graph)) break;
            r->points[r->count++] = g_CurrentSetup.pads[scratch[i]->padID].pos;
        }
        r->complete = full && r->count == length;
        r->next = r->count > 1 ? 1 : 0;
    }
    free(scratch);
    if (!r->count) botBuildSoloRoute(r, self, target);
}

int botInputDebugRoute(int player, BotDebugRoute *out)
{
    if (!out || player < 0 || player >= 4) return 0;
    *out = routes[player].debug;
    return out->valid;
}

#define BOT_DEGREES_PER_RADIAN 57.2957795f

/* N64 button bits: the same values used by input.c and joy.c. */
#define BOT_CONT_A     0x8000
#define BOT_CONT_B     0x4000
#define BOT_CONT_FWD   0x0008
#define BOT_CONT_RIGHT 0x0001

PlayerInput botInputForPlayer(int player)
{
    PlayerInput out = {0};
    static unsigned deadPolls[4];
    if (player < 0 || player >= 4 || player >= getPlayerCount()) return out;

    BotRoute *route = &routes[player];
    int stage = bossGetStageNum();
    if (route->stage != stage || route->graph != g_CurrentSetup.pathwaypoints
        || g_GlobalTimer < route->last_tick) {
        memset(route, 0, sizeof(*route));
        route->stage = stage;
        route->graph = g_CurrentSetup.pathwaypoints;
    }
    route->last_tick = g_GlobalTimer;
    route->debug.valid = 0;

    const struct player *self = g_playerPointers[player];
    if (!self || !self->prop) return out;

    if (self->bonddead) {
        /* MP death camera accepts a fresh A press once its animation ends.
         * Keep pulsing rather than holding it, so a new edge is available. */
        out.buttons = (++deadPolls[player] % 20 == 0) ? BOT_CONT_A : 0;
        route->timer = route->count = 0;
        route->door_ticks = route->door_attempts = 0;
        route->door = NULL;
        return out;
    }
    deadPolls[player] = 0;

    const struct player *target = NULL;
    int target_index = -1;
    float best = 1.0e30f;
    int count = getPlayerCount();
    for (int i = 0; i < count; ++i) {
        const struct player *other = g_playerPointers[i];
        if (i == player || !other || !other->prop || other->bonddead) continue;
        float dx = other->prop->pos.x - self->prop->pos.x;
        float dy = other->prop->pos.y - self->prop->pos.y;
        float dz = other->prop->pos.z - self->prop->pos.z;
        float score = dx * dx + dz * dz + 4.0f * dy * dy;
        if (score < best) { best = score; target = other; target_index = i; }
    }
    if (!target) return out;

    if (route->target != target || route->target_index != target_index) {
        route->target = target;
        route->target_index = target_index;
        route->timer = route->count = route->stuck = route->escape = 0;
        route->door_ticks = route->door_attempts = 0;
        route->door = NULL;
        route->last_x = self->prop->pos.x;
        route->last_z = self->prop->pos.z;
    }

    /* Recheck progress every 30 input polls. Rebuild on a stall and briefly
     * sidestep to leave the corner before trying the next route. */
    if (++route->stuck >= 30) {
        float mx = self->prop->pos.x - route->last_x;
        float mz = self->prop->pos.z - route->last_z;
        if ((route->next < route->count || best > 200.0f * 200.0f)
            && mx * mx + mz * mz < 35.0f * 35.0f) {
            route->timer = 0;
            const coord3d *goal = route->next < route->count
                ? &route->points[route->next] : &target->prop->pos;
            route->door = route->door_attempts < 3
                ? botFindRouteDoor(self, goal) : NULL;
            if (route->door) {
                route->door_ticks = 70;
                route->escape = 0;
            } else {
                route->door_ticks = 0;
                route->escape = 16;
            }
        } else if (mx * mx + mz * mz >= 35.0f * 35.0f) {
            route->door_attempts = 0;
        }
        route->last_x = self->prop->pos.x;
        route->last_z = self->prop->pos.z;
        route->stuck = 0;
    }

    if (route->timer <= 0 || (route->next >= route->count && !route->complete)) {
        botBuildRoute(route, self, target);
        route->timer = BOT_REPLAN_POLLS + player * 2;
    } else {
        --route->timer;
    }

    float dx = target->prop->pos.x - self->prop->pos.x;
    float dz = target->prop->pos.z - self->prop->pos.z;
    if (route->door_cooldown > 0) --route->door_cooldown;
    /* A missing graph reverts to baseline direct pursuit. Supported MP stages
     * use their corresponding solo waypoint graph instead. */
    int navigating = 1;
    if (route->count) {
        while (route->next < route->count) {
            coord3d *pos = &route->points[route->next];
            float wx = pos->x - self->prop->pos.x;
            float wz = pos->z - self->prop->pos.z;
            if (wx * wx + wz * wz > BOT_REACH * BOT_REACH
                || fabsf(pos->y - self->prop->pos.y) > 140.0f) break;
            ++route->next;
        }
        if (route->next < route->count) {
            coord3d *pos = &route->points[route->next];
            dx = pos->x - self->prop->pos.x;
            dz = pos->z - self->prop->pos.z;
        } else if (!route->complete) {
            /* The copied prefix ended before the destination. */
            route->timer = 0;
            navigating = 0;
        }
    }
    int waitingForDoor = 0;
    if (route->door_ticks > 0) {
        --route->door_ticks;
        PropRecord *doorprop = route->door;
        if (doorprop && doorprop->type == PROP_TYPE_DOOR && doorprop->door
            && doorprop->door->openstate == DOORSTATE_STATIONARY
            && doorprop->door->openPosition <= 0.5f) {
            dx = doorprop->door->runtime_pos.x - self->prop->pos.x;
            dz = doorprop->door->runtime_pos.z - self->prop->pos.z;
            waitingForDoor = 1;
        } else {
            route->door_ticks = 0;
            route->door = NULL;
        }
    }
    float distance = sqrtf(dx * dx + dz * dz);
    if (!isfinite(distance) || distance < 0.001f) return out;

    route->debug.bot_x = self->prop->pos.x;
    route->debug.bot_z = self->prop->pos.z;
    route->debug.target_x = target->prop->pos.x;
    route->debug.target_z = target->prop->pos.z;
    route->debug.count = route->count;
    route->debug.next = route->next;
    route->debug.valid = route->count > 0;
    for (int i = 0; i < route->count; ++i) {
        coord3d *pos = &route->points[i];
        route->debug.x[i] = pos->x;
        route->debug.z[i] = pos->z;
    }

    /* GE faces (-sin(theta), 0, cos(theta)); positive stick X increases
     * vv_theta. Aim using the shortest signed turn in degrees. */
    float desired = atan2f(-dx, dz) * BOT_DEGREES_PER_RADIAN;
    float error = desired - self->vv_theta;
    error = remainderf(error, 360.0f);
    float turn = error * 2.7f;
    if (turn > 70.0f) turn = 70.0f;
    if (turn < -70.0f) turn = -70.0f;
    if (fabsf(error) > 4.0f && fabsf(turn) < 22.0f)
        turn = error > 0.0f ? 22.0f : -22.0f;
    out.stick_x = (signed char)turn;

    if (waitingForDoor && distance < 200.0f && fabsf(error) < 18.0f
        && route->door_cooldown == 0 && route->door_attempts < 3) {
        /* B is reload/interact in GE; a fresh edge lets the ordinary
         * player-facing door code decide whether this door can open. */
        out.buttons |= BOT_CONT_B;
        route->door_cooldown = 24;
        ++route->door_attempts;
        sysLogPrintf(LOG_NOTE, "botnav: P%d B near door (distance %.1f, try %d)",
                     player + 1, distance, route->door_attempts);
    }

    /* The setup menu defaults to 1.2, where C-up walks forward and the
     * stick controls yaw/pitch. 1.1 instead walks on stick Y. */
    if (waitingForDoor) {
        /* Face and interact with the door; resume route on its opening. */
    } else if (route->escape > 0) {
        --route->escape;
        if (self->cur_player_control_type_0 == CONTROLLER_CONFIG_SOLITARE
            || self->cur_player_control_type_0 == CONTROLLER_CONFIG_GOODNIGHT)
            out.buttons |= BOT_CONT_RIGHT;
        else
            out.stick_y = -65;
    } else if (navigating && distance > 110.0f && fabsf(error) < 90.0f) {
        if (self->cur_player_control_type_0 == CONTROLLER_CONFIG_SOLITARE
            || self->cur_player_control_type_0 == CONTROLLER_CONFIG_GOODNIGHT)
            out.buttons |= BOT_CONT_FWD;
        else
            out.stick_y = 70;
    } else if (navigating && distance <= 110.0f && fabsf(error) < 25.0f) {
        out.buttons |= BOT_CONT_RIGHT;
    }

    return out;
}
