/**
 * @file GameWorld.c
 * @author Prof. Dr. David Buzatto
 * @brief GameWorld implementation.
 * 
 * @copyright Copyright (c) 2026
 */
#include <stdio.h>
#include <stdlib.h>

#include "raylib/raylib.h"
#include "raylib/raymath.h"

#include "GameWorld.h"
#include "ResourceManager.h"
#include "Map.h"
#include "Player.h"

void updateCamera( Camera3D *camera, Player *player );

// orbit camera state (spherical coordinates around the player)
float cameraYaw      = 90.0f;   // horizontal angle (deg)
float cameraPitch    = 30.0f;   // vertical angle (deg)
float cameraDistance = 10.0f;   // orbit radius (world units)

static const float CAMERA_YAW_SPEED    = 90.0f;   // yaw rotation speed (deg/s)
static const float CAMERA_PITCH_SPEED  = 90.0f;   // pitch rotation speed (deg/s)
static const float CAMERA_PITCH_MIN    = 5.0f;    // keep the camera above the ground
static const float CAMERA_PITCH_MAX    = 85.0f;   // keep the camera from flipping over
static const float CAMERA_ZOOM_STEP    = 1.0f;    // distance change per wheel notch
static const float CAMERA_DISTANCE_MIN = 3.0f;    // closest zoom
static const float CAMERA_DISTANCE_MAX = 40.0f;   // farthest zoom

/**
 * @brief Creates a dynamically allocated GameWorld struct instance.
 */
GameWorld *createGameWorld( void ) {

    GameWorld *gw = (GameWorld*) malloc( sizeof( GameWorld ) );

    int rows = 100;
    int cols = 100;
    int layers = 50;

    gw->map = createMap( -cols/2, 0, -rows/2, layers, rows, cols, 1 );
    gw->player = createPlayer( 0, 30, 0, 0.8f, 2.0f, BLUE );   // spawn high so it falls onto the terrain
    gw->player->map = gw->map;                        // give the player the world to collide against

    // start the model facing "forward" (away from the camera) instead of looking
    // at it: feed the camera-relative forward vector into the same atan2 the
    // player uses to face its movement.
    float a = cameraYaw * DEG2RAD;
    gw->player->facingAngle = atan2f( -cosf( a ), -sinf( a ) ) * RAD2DEG;

    gw->camera.position = (Vector3) { 0.0f, 0.0f, 0.0f };
    gw->camera.target = gw->player->pos;
    gw->camera.up = (Vector3) { 0.0f, 1.0f, 0.0f };
    gw->camera.fovy = 60.0f;
    gw->camera.projection = CAMERA_PERSPECTIVE;

    return gw;

}

/**
 * @brief Frees the game world and everything it owns (map and player).
 */
void destroyGameWorld( GameWorld *gw ) {
    destroyMap( gw->map );
    destroyPlayer( gw->player );
    free( gw );
}

/**
 * @brief Updates the world for one frame: block-edit test input, orbit camera
 *        controls, and the player's input + physics.
 *
 * @param gw     The game world.
 * @param delta  Seconds elapsed since the last frame.
 */
void updateGameWorld( GameWorld *gw, float delta ) {

    // breakBlock test (break blocks that are below the player)
    if ( IsKeyPressed( KEY_B ) ) {

        Map *map = gw->map;
        
        //int i = map->rows / 2;
        //int j = map->cols / 2;
        
        int j = (int) roundf( ( gw->player->pos.x - map->pos.x ) / map->blockSize );
        int i = (int) roundf( ( gw->player->pos.z - map->pos.z ) / map->blockSize );

        // only act if the player is actually over the map.
        if ( i >= 0 && i < map->rows && j >= 0 && j < map->cols ) {
            // scan top-down for the highest solid block in the column and break it.
            for ( int la = map->layers - 1; la >= 0; la-- ) {
                int p = la * ( map->rows * map->cols ) + i * map->cols + j;
                if ( !map->blocks[p].broken ) {
                    breakBlock( map, la, i, j );
                    break;
                }
            }
        }

    }

    // --- orbit camera controls (keyboard) ---

    // yaw: orbit left / right
    if ( IsKeyDown( KEY_RIGHT ) ) {
        cameraYaw += CAMERA_YAW_SPEED * delta;
    }
    if ( IsKeyDown( KEY_LEFT ) ) {
        cameraYaw -= CAMERA_YAW_SPEED * delta;
    }

    // pitch: orbit up / down, clamped so the camera never flips or goes underground
    if ( IsKeyDown( KEY_UP ) ) {
        cameraPitch += CAMERA_PITCH_SPEED * delta;
    }
    if ( IsKeyDown( KEY_DOWN ) ) {
        cameraPitch -= CAMERA_PITCH_SPEED * delta;
    }
    cameraPitch = Clamp( cameraPitch, CAMERA_PITCH_MIN, CAMERA_PITCH_MAX );

    // distance: zoom with the mouse wheel, clamped
    cameraDistance -= GetMouseWheelMove() * CAMERA_ZOOM_STEP;
    cameraDistance = Clamp( cameraDistance, CAMERA_DISTANCE_MIN, CAMERA_DISTANCE_MAX );

    // keep player movement relative to the camera's horizontal angle, then run
    // the player's input + physics for this frame.
    gw->player->cameraAngle = cameraYaw;
    gw->player->input( gw->player );
    gw->player->update( gw->player, delta );

    // place the camera based on the (possibly updated) player position.
    updateCamera( &gw->camera, gw->player );

}

/**
 * @brief Draws one frame: the 3D scene (map, player, grid) plus the FPS overlay.
 */
void drawGameWorld( GameWorld *gw ) {

    BeginDrawing();
    ClearBackground( SKYBLUE );

    BeginMode3D( gw->camera );
    gw->map->draw( gw->map, &gw->camera );
    gw->player->draw( gw->player );
    DrawGrid( 100, 1 );
    EndMode3D();

    DrawFPS( 10, 10 );

    EndDrawing();

}

/**
 * @brief Positions the camera on a sphere around the player from the current
 *        yaw / pitch / distance, looking back at the player.
 */
void updateCamera( Camera3D *camera, Player *player ) {

    // spherical orbit: turn yaw/pitch/distance into an offset from the player.
    float yaw   = cameraYaw * DEG2RAD;
    float pitch = cameraPitch * DEG2RAD;

    float horizontal = cameraDistance * cosf( pitch );  // radius projected on XZ
    float height     = cameraDistance * sinf( pitch );  // vertical part of the radius

    camera->position = (Vector3) { 
        player->pos.x + horizontal * cosf( yaw ), 
        player->pos.y + height, 
        player->pos.z + horizontal * sinf( yaw ), 
    };

    camera->target = player->pos;

}