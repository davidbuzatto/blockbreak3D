/**
 * @file Player.c
 * @author Prof. Dr. David Buzatto
 * @brief Player implementation: camera-relative movement, gravity, jumping and
 *        AABB collision against the voxel map.
 *
 * @copyright Copyright (c) 2026
 */
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "raylib/raylib.h"
#include "raylib/raymath.h"

#include "Map.h"
#include "Player.h"
#include "ResourceManager.h"

static const float GRAVITY = -25.0f;            // downward acceleration (units/s^2)
static const float TERMINAL_VELOCITY = -50.0f;  // maximum fall speed (clamp)
static const float JUMP_SPEED = 9.0f;           // initial upward velocity of a jump
static const float MODEL_FACING_OFFSET = 0.0f;  // corrects the model's default facing

static void input( Player *player );
static void update( Player *player, float delta );
static void draw( Player *player );

/**
 * @brief Creates a dynamically allocated Player.
 *
 * @param x,y,z    Initial world-space position (collision box center).
 * @param width    Collision box width and depth (X/Z footprint).
 * @param height   Collision box height (Y).
 * @param color    Debug color (used only by the optional debug cube).
 * @return Player* The new player. Its map pointer starts NULL and must be set by
 *                 the caller (GameWorld) before update() runs.
 */
Player *createPlayer( float x, float y, float z, float width, float height, Color color ) {

    Player *new = (Player*) malloc( sizeof( Player ) );

    new->pos = (Vector3) { x, y, z };
    new->dim = (Vector3) { width, height, width };   // collision box: square footprint, given height
    new->vel = (Vector3) { 0 };

    new->walkingSpeed = 5.0f;
    new->cameraAngle = 0.0f;
    new->facingAngle = 0.0f;

    // scale the model uniformly so its height matches the collision box height.
    // GetModelBoundingBox gives the model's real size in its own units; dividing
    // the wanted height by it gives the factor (so changing dim.y rescales the model).
    BoundingBox bb = GetModelBoundingBox( rm.playerModel );
    float modelHeight = bb.max.y - bb.min.y;
    new->modelScale = new->dim.y / modelHeight;

    new->map = NULL;
    new->onGround = false;

    new->color = color;

    new->input = input;
    new->update = update;
    new->draw = draw;

    return new;

}

/**
 * @brief Frees a Player. Safe to call with NULL.
 */
void destroyPlayer( Player *player ) {
    if ( player != NULL ) {
        free( player );
    }
}

/**
 * @brief Reads input: sets horizontal velocity (camera-relative) and triggers a
 *        jump. Vertical velocity is otherwise controlled by gravity in update().
 */
static void input( Player *player ) {
    
    // raw movement intent from the keys, in camera-relative axes:
    //     strafe  = sideways
    //     forward = toward where the camera looks
    int strafe   = ( IsKeyDown( KEY_A ) ? -1 : 0 ) + ( IsKeyDown( KEY_D ) ? 1 : 0 );
    int forward  = ( IsKeyDown( KEY_S ) ? -1 : 0 ) + ( IsKeyDown( KEY_W ) ? 1 : 0 );

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

    // jump: only when on the ground and only on the key press (not while held),
    // otherwise it would re-jump every frame.
    if ( player->onGround && IsKeyPressed( KEY_SPACE ) ) {
        player->vel.y = JUMP_SPEED;
        player->onGround = false;
    }

}

/**
 * @brief Advances the player's physics for one frame: horizontal movement with
 *        wall collision, gravity, and vertical movement with ground/ceiling
 *        collision.
 *
 * Movement is resolved one axis at a time (move, then undo that axis if it
 * caused an overlap), which lets the player slide along walls instead of
 * sticking to them.
 */
static void update( Player *player, float delta ) {

    // --- horizontal movement, one axis at a time so we can slide along walls ---

    // X axis: move, then undo only X if it put us inside a block.
    float dx = player->vel.x * delta;
    player->pos.x += dx;
    if ( mapBoxCollides( player->map, player->pos, player->dim ) ) {
        player->pos.x -= dx;
    }

    // Z axis: same idea, independent of X.
    float dz = player->vel.z * delta;
    player->pos.z += dz;
    if ( mapBoxCollides( player->map, player->pos, player->dim ) ) {
        player->pos.z -= dz;
    }

    // face the direction of horizontal movement (keep the last facing when idle).
    // atan2(x, z) measures the angle from +Z, matching the Y rotation used to draw.
    if ( player->vel.x != 0.0f || player->vel.z != 0.0f ) {
        player->facingAngle = atan2f( player->vel.x, player->vel.z ) * RAD2DEG;
    }

    // --- gravity: accelerate the fall each frame, capped at terminal velocity ---
    player->vel.y += GRAVITY * delta;
    if ( player->vel.y < TERMINAL_VELOCITY ) {
        player->vel.y = TERMINAL_VELOCITY;
    }

    // --- vertical movement + ground/ceiling collision ---
    float dy = player->vel.y * delta;
    player->pos.y += dy;

    if ( mapBoxCollides( player->map, player->pos, player->dim ) ) {

        if ( dy <= 0.0f ) {

            // falling: snap the feet onto the top of the block below, so the
            // player rests exactly on the surface (no hovering/jitter) and
            // onGround stays stable (which the jump relies on).
            float feetY = player->pos.y - player->dim.y * 0.5f;
            int la = (int) floorf( ( feetY - player->map->pos.y ) / player->map->blockSize + 0.5f );
            float blockTop = player->map->pos.y + player->map->blockSize * la + player->map->blockSize * 0.5f;
            player->pos.y = blockTop + player->dim.y * 0.5f;
            player->onGround = true;

        } else {
            // rising into a ceiling: just cancel the upward step.
            player->pos.y -= dy;
        }

        player->vel.y = 0.0f;

    } else {
        // nothing under (or above) us: airborne.
        player->onGround = false;
    }

}

/**
 * @brief Draws the player's 3D model at its feet, rotated to face its movement
 *        direction. The wireframe box shows the collision AABB (debug).
 */
static void draw( Player *player ) {

    // the model's origin is at its feet, but player->pos is the box CENTER,
    // so lower the draw position by half the height.
    Vector3 feet = {
        player->pos.x,
        player->pos.y - player->dim.y * 0.5f,
        player->pos.z
    };

    DrawModelEx(
        rm.playerModel,
        feet,
        (Vector3) { 0.0f, 1.0f, 0.0f },                 // rotate around the Y axis
        player->facingAngle + MODEL_FACING_OFFSET,      // face the movement direction
        (Vector3) {                                     // scale
            player->modelScale, 
            player->modelScale, 
            player->modelScale
        },
        WHITE                                           // tint (WHITE keeps texture colors)
    );

    // debug: the collision box (player->dim) drawn around the model.
    DrawCubeWiresV( player->pos, player->dim, BLACK );

}