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

float cameraYaw = 90.0f;        // horizontal angle
float cameraPitch = 30.0f;      // vertical angle
float cameraDistance = 10.0f;   // orbit radius

static const float CAMERA_YAW_SPEED = 90.0f;
static const float CAMERA_PITCH_SPEED = 90.0f;
static const float CAMERA_PITCH_MIN = 5.0f;
static const float CAMERA_PITCH_MAX = 85.0f;
static const float CAMERA_ZOOM_STEP = 1.0f;
static const float CAMERA_DISTANCE_MIN = 3.0f;
static const float CAMERA_DISTANCE_MAX = 40.0f;

/**
 * @brief Creates a dinamically allocated GameWorld struct instance.
 */
GameWorld *createGameWorld( void ) {

    GameWorld *gw = (GameWorld*) malloc( sizeof( GameWorld ) );

    int rows = 60;
    int cols = 60;
    int layers = 50;

    gw->map = createMap( -cols/2, 0, -rows/2, layers, rows, cols, 1 );
    gw->player = createPlayer( 0, 30, 0, 1, BLUE );
    gw->player->map = gw->map;

    gw->camera.position = (Vector3) { 0.0f, 0.0f, 0.0f };
    gw->camera.target = gw->player->pos;
    gw->camera.up = (Vector3) { 0.0f, 1.0f, 0.0f };
    gw->camera.fovy = 60.0f;
    gw->camera.projection = CAMERA_PERSPECTIVE;

    return gw;

}

void destroyGameWorld( GameWorld *gw ) {
    destroyMap( gw->map );
    destroyPlayer( gw->player );
    free( gw );
}

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

    if ( IsKeyDown( KEY_A ) ) {
        cameraYaw += CAMERA_YAW_SPEED * delta;
    }
    if ( IsKeyDown( KEY_D ) ) {
        cameraYaw -= CAMERA_YAW_SPEED * delta;
    }

    if ( IsKeyDown( KEY_W ) ) {
        cameraPitch += CAMERA_PITCH_SPEED * delta;
    }
    if ( IsKeyDown( KEY_S ) ) {
        cameraPitch -= CAMERA_PITCH_SPEED * delta;
    }
    cameraPitch = Clamp( cameraPitch, CAMERA_PITCH_MIN, CAMERA_PITCH_MAX );

    cameraDistance -= GetMouseWheelMove() * CAMERA_ZOOM_STEP;
    cameraDistance = Clamp( cameraDistance, CAMERA_DISTANCE_MIN, CAMERA_DISTANCE_MAX );

    gw->player->cameraAngle = cameraYaw;
    gw->player->input( gw->player );
    gw->player->update( gw->player, delta );

    updateCamera( &gw->camera, gw->player );

}

void drawGameWorld( GameWorld *gw ) {

    BeginDrawing();
    ClearBackground( WHITE );

    BeginMode3D( gw->camera );
    gw->map->draw( gw->map, &gw->camera );
    gw->player->draw( gw->player );
    DrawGrid( 100, 1 );
    EndMode3D();

    DrawFPS( 10, 10 );

    EndDrawing();

}

void updateCamera( Camera3D *camera, Player *player ) {

    // spherical orbit
    float yaw = cameraYaw * DEG2RAD;
    float pitch = cameraPitch * DEG2RAD;

    float horizontal = cameraDistance * cosf( pitch );
    float height = cameraDistance * sinf( pitch );

    camera->position = (Vector3) { 
        player->pos.x + horizontal * cosf( yaw ), 
        player->pos.y + height, 
        player->pos.z + horizontal * sinf( yaw ), 
    };

    camera->target = player->pos;

}