#include <assert.h>
#include <stdio.h>
#include "mp_roster.h"

int main(void)
{
    MpRosterRef first, second, human;

    /* All legal human-first rosters, including the future one-human menu. */
    for (int humans = 1; humans <= MP_ROSTER_MAX; ++humans) {
        for (int bots = 0; bots <= MP_ROSTER_MAX - humans; ++bots) {
            assert(mpRosterBegin(9, humans, bots));
            assert(mpRosterCount() == humans + bots);
            assert(mpRosterHumanCount() == humans);
            for (int i = 0; i < humans + bots; ++i) {
                const MpRosterEntry *entry = mpRosterEntry(i);
                assert(entry);
                assert(entry->kind == (i < humans ? MP_ROSTER_HUMAN : MP_ROSTER_SIMULANT));
                assert(entry->human_index == (i < humans ? i : -1));
            }
            assert(mpRosterEntry(humans + bots) == 0);
            mpRosterEnd();
        }
    }

    assert(!mpRosterBegin(9, 4, 1)); /* Never create a fifth combatant. */
    assert(mpRosterCount() == 0);
    assert(!mpRosterBegin(9, 0, 1));
    assert(!mpRosterBegin(0, 1, 1));

    assert(mpRosterBegin(9, 2, 2));
    human = mpRosterRefForSlot(0);
    assert(mpRosterResolve(human) == 0);
    assert(mpRosterSlotForHuman(0) == 0);
    assert(mpRosterSlotForHuman(2) == -1);
    assert(mpRosterRefForSlot(2).slot == -1); /* Reserved, not spawned. */
    assert(!mpRosterBindSimulant(0, 100));
    assert(mpRosterBindSimulant(2, 100));
    assert(!mpRosterBindSimulant(3, 100)); /* Actor cannot own two slots. */
    assert(mpRosterSlotForChr(100) == 2);
    first = mpRosterRefForSlot(2);
    assert(mpRosterResolve(first) == 2);
    mpRosterUnbindSimulant(2, 101); /* Old/incorrect removal is ignored. */
    assert(mpRosterResolve(first) == 2);
    mpRosterUnbindSimulant(2, 100);
    assert(mpRosterResolve(first) == -1);
    assert(mpRosterBindSimulant(2, 100)); /* Same chrnum, new life. */
    second = mpRosterRefForSlot(2);
    assert(mpRosterResolve(first) == -1);
    assert(mpRosterResolve(second) == 2);
    mpRosterEnd();
    assert(mpRosterResolve(second) == -1);
    assert(mpRosterResolve(human) == -1);
    assert(mpRosterBegin(9, 1, 3));
    assert(mpRosterResolve(second) == -1); /* Same stage number, new match. */
    mpRosterEnd();

    puts("mp_roster: all roster and lifetime checks passed");
    return 0;
}
