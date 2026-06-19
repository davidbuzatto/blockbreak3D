#pragma once

#include "raylib/raylib.h"

#include "Map.h"

typedef struct Player Player;

struct Player {

    Vector3 pos;
    Vector3 dim;
    Vector3 vel;

    float walkingSpeed;
    float cameraAngle;

    Map *map;
    bool onGround;

    Color color;

    void (*input)( Player *player );
    void (*update)( Player *player, float delta );
    void (*draw)( Player *player );

};

Player *createPlayer( float x, float y, float z, float size, Color color );
void destroyPlayer( Player *player );