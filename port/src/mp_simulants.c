#include "mp_simulants.h"

/* The first playable slice has one Facility actor. Do not advertise more
 * slots until independent actors and navigation exist for them. */
static int s_count;
extern int gamemode;
extern int g_StageNum;
/* Stable enum values in src/bondconstants.h; port/src/input.c uses the same
 * mode value without pulling game headers into this small configuration TU. */
#define GE_GAMEMODE_MULTI 1
#define GE_LEVEL_TITLE 90

int mpSimulantsGetCount(void) { return s_count; }

void mpSimulantsSetCount(int humans, int count)
{
    s_count = humans == 1 && count > 0 ? 1 : 0;
}

int mpSimulantsIsMatch(void)
{
    return gamemode == GE_GAMEMODE_MULTI && g_StageNum != GE_LEVEL_TITLE;
}

int mpSimulantsCanStart(int humans, int inputs)
{
    return humans >= 1 && humans <= 4 && humans <= inputs &&
           humans + s_count >= 2 && humans + s_count <= 4;
}
