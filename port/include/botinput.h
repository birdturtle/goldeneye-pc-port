#ifndef PORT_BOTINPUT_H
#define PORT_BOTINPUT_H

#include "playerinput.h"

/* Produce ordinary N64 controller commands for an existing MP player. */
PlayerInput botInputForPlayer(int player);

/* Read-only snapshot for the F8 route display. Coordinates are world X/Z. */
#define BOT_DEBUG_ROUTE_POINTS 64
typedef struct BotDebugRoute {
    float x[BOT_DEBUG_ROUTE_POINTS], z[BOT_DEBUG_ROUTE_POINTS];
    float bot_x, bot_z, target_x, target_z;
    int count, next, valid;
} BotDebugRoute;
int botInputDebugRoute(int player, BotDebugRoute *out);

#endif
