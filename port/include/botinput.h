#ifndef PORT_BOTINPUT_H
#define PORT_BOTINPUT_H

#include "playerinput.h"

/* Produce ordinary N64 controller commands for an existing MP player. */
PlayerInput botInputForPlayer(int player);

#endif
