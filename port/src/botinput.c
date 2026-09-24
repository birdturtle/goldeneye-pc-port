/* First multiplayer bot: nearest living opponent, simple pursuit and fire.
 * This module reads game state but changes it only through controller input.
 * Navigation, visibility, and item-seeking will be added after the input seam
 * and basic local play are verified. */
#include "port_math.h"

#include "botinput.h"
#include "player.h"

#define BOT_DEGREES_PER_RADIAN 57.2957795f
#define BOT_FIST_REACH         145.0f
#define BOT_GUN_REACH          1500.0f

/* N64 button bits: the same values used by input.c and joy.c. */
#define BOT_CONT_A     0x8000
#define BOT_CONT_FIRE  0x2000
#define BOT_CONT_FWD   0x0008
#define BOT_CONT_RIGHT 0x0001

PlayerInput botInputForPlayer(int player)
{
    PlayerInput out = {0};
    static unsigned deadPolls[4];
    if (player < 0 || player >= 4 || player >= getPlayerCount()) return out;

    const struct player *self = g_playerPointers[player];
    if (!self || !self->prop) return out;

    if (self->bonddead) {
        /* MP death camera accepts a fresh A press once its animation ends.
         * Keep pulsing rather than holding it, so a new edge is available. */
        out.buttons = (++deadPolls[player] % 20 == 0) ? BOT_CONT_A : 0;
        return out;
    }
    deadPolls[player] = 0;

    const struct player *target = NULL;
    float best = 1.0e30f;
    int count = getPlayerCount();
    for (int i = 0; i < count; ++i) {
        const struct player *other = g_playerPointers[i];
        if (i == player || !other || !other->prop || other->bonddead) continue;
        float dx = other->prop->pos.x - self->prop->pos.x;
        float dy = other->prop->pos.y - self->prop->pos.y;
        float dz = other->prop->pos.z - self->prop->pos.z;
        float score = dx * dx + dz * dz + 4.0f * dy * dy;
        if (score < best) { best = score; target = other; }
    }
    if (!target) return out;

    float dx = target->prop->pos.x - self->prop->pos.x;
    float dy = target->prop->pos.y - self->prop->pos.y;
    float dz = target->prop->pos.z - self->prop->pos.z;
    float distance = sqrtf(dx * dx + dz * dz);
    if (!isfinite(distance) || distance < 0.001f) return out;

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

    /* The setup menu defaults to 1.2, where C-up walks forward and the
     * stick controls yaw/pitch. 1.1 instead walks on stick Y. */
    if (distance > 110.0f && fabsf(error) < 90.0f) {
        if (self->cur_player_control_type_0 == CONTROLLER_CONFIG_SOLITARE
            || self->cur_player_control_type_0 == CONTROLLER_CONFIG_GOODNIGHT)
            out.buttons |= BOT_CONT_FWD;
        else
            out.stick_y = 70;
    } else if (distance <= 110.0f && fabsf(error) < 25.0f) {
        out.buttons |= BOT_CONT_RIGHT;
    }

    int fist = self->hands[GUNRIGHT].weaponnum == ITEM_FIST
        || self->hands[GUNRIGHT].weaponnum == ITEM_UNARMED;
    float reach = fist ? BOT_FIST_REACH : BOT_GUN_REACH;
    if (distance < reach && fabsf(error) < (fist ? 18.0f : 8.0f)
        && fabsf(dy) < 125.0f) {
        /* In 1.3/1.4 the game's own input mapping swaps A and Z. */
        int style = self->cur_player_control_type_0;
        out.buttons |= (style == CONTROLLER_CONFIG_KISSY
                        || style == CONTROLLER_CONFIG_GOODNIGHT)
                           ? BOT_CONT_A : BOT_CONT_FIRE;
    }

    return out;
}
