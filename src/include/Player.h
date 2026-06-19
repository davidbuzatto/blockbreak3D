/**
 * @file Player.h
 * @author Prof. Dr. David Buzatto
 * @brief Player interface: a cube controlled by the user, with camera-relative
 *        movement and physics (gravity, jump, collision) against the map.
 *
 * @copyright Copyright (c) 2026
 */
#pragma once

#include "raylib/raylib.h"

#include "Map.h"

typedef struct Player Player;

struct Player {

    Vector3 pos;            // world-space position (cube center).
    Vector3 dim;            // cube size (width, height, depth).
    Vector3 vel;            // current velocity (units/s).

    float walkingSpeed;     // horizontal movement speed (units/s).
    float cameraAngle;      // camera orbit angle (deg), set by GameWorld each frame.

    Map *map;               // world the player moves and collides against.
    bool onGround;          // true when standing on a solid block (enables jump).

    Color color;

    void (*input)( Player *player );
    void (*update)( Player *player, float delta );
    void (*draw)( Player *player );

};

/**
 * @brief Creates a player. See createPlayer in Player.c for parameter details.
 */
Player *createPlayer( float x, float y, float z, float size, Color color );

/**
 * @brief Frees a player. Safe to call with NULL.
 */
void destroyPlayer( Player *player );
