#include "mp_combat.h"
#include "mp_roster.h"
#include "bondview.h"
#include "bondinv.h"
#include "chrobjdata.h"
#include "front.h"
#include "glass.h"
#include "gun.h"
#include "lv.h"
#include "mpmenu.h"
#include "player.h"
#include "propobj.h"
#include "system.h"
#include "joy.h"

extern void increment_num_deaths(void);
extern void bondviewKillCurrentPlayer(void);

typedef struct MpCombatLife {
    MpRosterRef life;
    MpRosterRef attacker;
    int credited;
} MpCombatLife;

static MpCombatLife s_lives[MP_ROSTER_MAX];

static int mpCombatSameRef(MpRosterRef a, MpRosterRef b)
{
    return a.slot == b.slot && a.generation == b.generation &&
           a.incarnation == b.incarnation && a.generation != 0;
}

static int mpCombatValidActor(MpCombatLife *state, int slot)
{
    return mpRosterResolve(state->life) == slot &&
           mpCombatSameRef(state->life, mpRosterRefForSlot(slot));
}

void mpCombatSpawnSimulant(int slot, struct ChrRecord *chr)
{
    const MpRosterEntry *entry = mpRosterEntry(slot);
    if (!chr || !entry || entry->kind != MP_ROSTER_SIMULANT ||
        entry->chrnum != chr->chrnum) return;
    s_lives[slot].life = mpRosterRefForSlot(slot);
    s_lives[slot].attacker = (MpRosterRef){0, 0, -1};
    s_lives[slot].credited = 0;
    /* Character damage is in eighths of a player's normalized health.
     * Ignore campaign difficulty/007 modifiers in a multiplayer match. */
    chr->maxdamage = MP_COMBAT_ACTOR_HEALTH;
    chr->damage = 0.0f;
}

void mpCombatNoteSimulantHit(struct ChrRecord *chr, int human_index)
{
    int slot;
    MpCombatLife *state;
    int attacker = mpRosterSlotForHuman(human_index);
    if (!chr || (slot = mpRosterSlotForChr(chr->chrnum)) < 0) return;
    state = &s_lives[slot];
    if (!mpCombatValidActor(state, slot) || state->credited) return;
    state->attacker = attacker >= 0 ? mpRosterRefForSlot(attacker) :
                                   (MpRosterRef){0, 0, -1};
}

/* Human MP damage does not route through the guard's ACT_ARGH stagger.
 * Keep a living Simulant's current movement/attack action on weapon hits,
 * while allowing a fatal hit to reach the native character death action. */
int mpCombatSimulantKeepsMoving(struct ChrRecord *chr)
{
    int slot;
    if (!chr || (slot = mpRosterSlotForChr(chr->chrnum)) < 0) return 0;
    return mpCombatValidActor(&s_lives[slot], slot) &&
           chr->damage < chr->maxdamage;
}

int mpCombatShouldCountNativeKill(struct ChrRecord *chr)
{
    int slot;
    if (!chr || (slot = mpRosterSlotForChr(chr->chrnum)) < 0) return 1;
    return mpCombatValidActor(&s_lives[slot], slot) &&
           mpRosterResolve(s_lives[slot].attacker) >= 0;
}

void mpCombatSimulantDied(int slot, struct ChrRecord *chr)
{
    MpCombatLife *state;
    int attacker;
    const MpRosterEntry *entry = mpRosterEntry(slot);
    if (!chr || !entry || entry->kind != MP_ROSTER_SIMULANT ||
        entry->chrnum != chr->chrnum) return;
    state = &s_lives[slot];
    if (!mpCombatValidActor(state, slot) || state->credited) return;
    state->credited = 1;
    attacker = mpRosterResolve(state->attacker);
    if (attacker >= 0 && attacker != slot)
        g_playerPlayerData[attacker].kill_counts[slot]++;
}

/* The N64 record_damage_kills path treats an attacker index as a real
 * struct player and switches g_CurrentPlayer to it. A Simulant has no such
 * object. Keep the native human health, armor and death presentation, then
 * credit the stage roster without inventing a viewport or controller. */
void mpCombatDamageHuman(int attacker_slot, int victim_human, float amount,
                         float dx, float dz)
{
    int previous = get_cur_playernum();
    int victim = mpRosterSlotForHuman(victim_human);
    const MpRosterEntry *attacker = mpRosterEntry(attacker_slot);
    struct player *player;
    float damage;
    if (!attacker || attacker->kind != MP_ROSTER_SIMULANT ||
        mpRosterResolve(mpRosterRefForSlot(attacker_slot)) != attacker_slot ||
        victim < 0 || victim >= getPlayerCount() || !g_playerPointers[victim] ||
        g_stopPlayFlag || g_gameOverFlag) return;

    set_cur_player(victim_human);
    player = g_CurrentPlayer;
    if (player->watch_animation_state != WATCH_ANIMATION_0x0) {
        hudMakeDamageSegments(&player->armor_display_values[0].items[0], 0x2e, 1,
                              currentPlayerGetArmor());
        hudMakeDamageSegments(&player->health_display_values[0].items[0], 0x2e, -1,
                              currentPlayerGetHealth());
    }
    if (!player->bonddead && !player->cheatBondInvincible && !g_PlayerInvincible &&
        player->watch_animation_state != WATCH_ANIMATION_0x5 &&
        player->watch_animation_state != WATCH_ANIMATION_0xc &&
        (player->damageshowtime < 0 || player->damageshowtime == 0)) {
        damage = g_playerPlayerData[victim].handicap * amount;
        if (get_scenario() == SCENARIO_LTK)
            damage = player->bondhealth * player->actual_health +
                     player->bondarmour * player->actual_armor;
        player->oldhealth = player->bondhealth;
        player->oldarmour = player->bondarmour;
        joyRumblePakStart(victim_human, 0.25f);
        if (damage <= player->bondarmour * player->actual_armor) {
            player->bondarmour -= damage / player->actual_armor;
        } else {
            damage -= player->bondarmour / player->actual_armor;
            player->bondarmour = 0.0f;
            player->actual_armor = 1.0f;
            player->bondhealth -= damage / player->actual_health;
            if (player->bondhealth <= 0.0f) {
                drop_inventory();
                increment_num_deaths();
                g_playerPlayerData[attacker_slot].kill_counts[victim]++;
                g_playerPlayerData[attacker_slot].kill_count++;
                bondviewKillCurrentPlayer();
                sysLogPrintf(LOG_NOTE, "simulant %d eliminated player %d",
                             attacker_slot + 1, victim + 1);
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
