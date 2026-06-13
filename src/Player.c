#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "raylib/raylib.h"
#include "raylib/raymath.h"

#include "Player.h"

static void input( Player *player );
static void update( Player *player, float delta );
static void draw( Player *player );

Player *createPlayer( float x, float y, float z, float size, Color color ) {

    Player *new = (Player*) malloc( sizeof( Player ) );

    new->pos = (Vector3) { x, y, z };
    new->dim = (Vector3) { size, size, size };
    new->vel = (Vector3) { 0 };
    new->walkingSpeed = 5.0f;

    new->color = color;

    new->input = input;
    new->update = update;
    new->draw = draw;

    return new;

}

void destroyPlayer( Player *player ) {
    if ( player != NULL ) {
        free( player );
    }
}

static void input( Player *player ) {
    
    int left = IsKeyDown( KEY_LEFT ) ? -1 : 0;
    int right = IsKeyDown( KEY_RIGHT ) ? 1 : 0;
    int up = IsKeyDown( KEY_UP ) ? -1 : 0;
    int down = IsKeyDown( KEY_DOWN ) ? 1 : 0;

    player->vel.x = (left + right) * player->walkingSpeed;
    player->vel.z = (up + down) * player->walkingSpeed;

}

static void update( Player *player, float delta ) {
    
    player->pos.x += player->vel.x * delta;
    player->pos.z += player->vel.z * delta;

}

static void draw( Player *player ) {
    DrawCubeV( player->pos, player->dim, player->color );
    DrawCubeWiresV( player->pos, player->dim, BLACK );
}