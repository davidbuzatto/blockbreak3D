#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "raylib/raylib.h"
#include "raylib/raymath.h"

#include "Map.h"
#include "Player.h"

static const float GRAVITY = -25.0f;           // world units / s^2
static const float TERMINAL_VELOCITY = -50.0f;
static const float JUMP_SPEED = 9.0f;

static void input( Player *player );
static void update( Player *player, float delta );
static void draw( Player *player );

Player *createPlayer( float x, float y, float z, float size, Color color ) {

    Player *new = (Player*) malloc( sizeof( Player ) );

    new->pos = (Vector3) { x, y, z };
    new->dim = (Vector3) { size, size, size };
    new->vel = (Vector3) { 0 };

    new->walkingSpeed = 5.0f;
    new->cameraAngle = 0.0f;

    new->map = NULL;
    new->onGround = false;

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
    
    // raw movement intent from the keys, in camera-relative axes:
    //     strafe = sideways
    //     forward = toward where the camera looks
    //     vertical = up and down
    int strafe   = ( IsKeyDown( KEY_LEFT ) ? -1 : 0 ) + ( IsKeyDown( KEY_RIGHT ) ? 1 : 0 );
    int forward  = ( IsKeyDown( KEY_DOWN ) ? -1 : 0 ) + ( IsKeyDown( KEY_UP ) ? 1 : 0 );

    // build the camera-relative basis on the XZ plane from the camera angle.
    float a = player->cameraAngle * DEG2RAD;
    Vector3 fwd = { -cosf( a ), 0.0f, -sinf( a ) };  // toward the camera's view direction
    Vector3 right = { sinf( a ), 0.0f, -cosf( a ) }; // perpendicular, to the right

    // combine both diretions weighted by the input.
    Vector3 move = {
        fwd.x * forward + right.x * strafe,
        0.0f,
        fwd.z * forward + right.z * strafe
    };

    // normalize so moving diagonally isn't faster than straight, then scale by speed.
    if ( move.x != 0.0f || move.z != 0.0f ) {
        move = Vector3Normalize( move );
    }

    player->vel.x = move.x * player->walkingSpeed;
    player->vel.z = move.z * player->walkingSpeed;

    if ( player->onGround && IsKeyPressed( KEY_SPACE ) ) {
        player->vel.y = JUMP_SPEED;
        player->onGround = false;
    }

}

static void update( Player *player, float delta ) {

    float dx = player->vel.x * delta;
    player->pos.x += dx;
    if ( mapBoxCollides( player->map, player->pos, player->dim ) ) {
        player->pos.x -= dx;
    }

    float dz = player->vel.z * delta;
    player->pos.z += dz;
    if ( mapBoxCollides( player->map, player->pos, player->dim ) ) {
        player->pos.z -= dz;
    }

    player->vel.y += GRAVITY * delta;
    if ( player->vel.y < TERMINAL_VELOCITY ) {
        player->vel.y = TERMINAL_VELOCITY;
    }

    float dy = player->vel.y * delta;
    player->pos.y += dy;

    if ( mapBoxCollides( player->map, player->pos, player->dim ) ) {

        // falling
        if ( dy <= 0.0f ) {

            float feetY = player->pos.y - player->dim.y * 0.5f;
            int la = (int) floorf( ( feetY - player->map->pos.y ) / player->map->blockSize + 0.5f );
            float blockTop = player->map->pos.y + player->map->blockSize * la + player->map->blockSize * 0.5f;
            player->pos.y = blockTop + player->dim.y * 0.5f;
            player->onGround = true;

        } else {
            player->pos.y -= dy;
        }

        player->vel.y = 0.0f;

    } else {
        player->onGround = false;
    }

}

static void draw( Player *player ) {
    DrawCubeV( player->pos, player->dim, player->color );
    DrawCubeWiresV( player->pos, player->dim, BLACK );
}