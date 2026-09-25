#include <stdio.h>
#include <bondtypes.h>
#include "player.h"
#include "mp_roster.h"
#include "mp_combat.h"
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "check failed: %s at %d\n", #expr, __LINE__); \
    return 1; \
} } while (0)

struct player_data g_playerPlayerData[4];

int main(void)
{
    ChrRecord bot = {0};
    bot.chrnum = 0x6fff;
    CHECK(mpRosterBegin(45, 1, 1));
    CHECK(mpRosterBindSimulant(1, bot.chrnum));
    mpCombatSpawnSimulant(1, &bot);
    CHECK(bot.maxdamage == MP_COMBAT_ACTOR_HEALTH);
    CHECK(bot.damage == 0.0f);
    CHECK(mpCombatSimulantKeepsMoving(&bot));
    bot.damage = bot.maxdamage - 0.5f;
    CHECK(mpCombatSimulantKeepsMoving(&bot));
    bot.damage = bot.maxdamage;
    CHECK(!mpCombatSimulantKeepsMoving(&bot));
    bot.damage = 0.0f;

    /* Unowned/environmental damage never becomes a human point. */
    mpCombatNoteSimulantHit(&bot, -1);
    CHECK(!mpCombatShouldCountNativeKill(&bot));
    mpCombatNoteSimulantHit(&bot, 0);
    CHECK(mpCombatShouldCountNativeKill(&bot));
    mpCombatSimulantDied(1, &bot);
    mpCombatSimulantDied(1, &bot);
    CHECK(g_playerPlayerData[0].kill_counts[1] == 1);

    mpRosterUnbindSimulant(1, bot.chrnum);
    CHECK(!mpCombatSimulantKeepsMoving(&bot));
    CHECK(mpRosterBindSimulant(1, bot.chrnum));
    CHECK(!mpCombatSimulantKeepsMoving(&bot));
    mpCombatSpawnSimulant(1, &bot);
    CHECK(mpCombatSimulantKeepsMoving(&bot));
    CHECK(!mpCombatShouldCountNativeKill(&bot));
    mpCombatNoteSimulantHit(&bot, 0);
    mpCombatNoteSimulantHit(&bot, -1);
    mpCombatSimulantDied(1, &bot);
    CHECK(g_playerPlayerData[0].kill_counts[1] == 1);

    mpRosterEnd();
    CHECK(!mpCombatSimulantKeepsMoving(&bot));
    CHECK(mpRosterBegin(45, 1, 1));
    CHECK(mpRosterBindSimulant(1, bot.chrnum));
    mpCombatSpawnSimulant(1, &bot);
    mpCombatNoteSimulantHit(&bot, 0);
    mpCombatSimulantDied(1, &bot);
    CHECK(g_playerPlayerData[0].kill_counts[1] == 2);
    puts("mp_combat: life attribution and single-credit checks passed");
    return 0;
}
