#ifndef PORT_PLAYERINPUT_H
#define PORT_PLAYERINPUT_H

/* The N64 controller state consumed by a GoldenEye player, regardless of
 * whether the source is local, a bot, or eventually a remote client. */
typedef struct PlayerInput {
    unsigned buttons;
    signed char stick_x;
    signed char stick_y;
} PlayerInput;

#endif
