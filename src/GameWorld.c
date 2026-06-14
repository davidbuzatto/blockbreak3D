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
float cameraOffsetY = 5.0f;
float cameraAngle = 90.0f;
float cameraDistance = 10.0f;

/**
 * @brief Creates a dinamically allocated GameWorld struct instance.
 */
GameWorld *createGameWorld( void ) {

    GameWorld *gw = (GameWorld*) malloc( sizeof( GameWorld ) );

    int rows = 60;
    int cols = 60;
    int layers = 50;

    gw->map = createMap( -cols/2, 0, -rows/2, layers, rows, cols, 1 );
    gw->player = createPlayer( 0, 10, 0, 1, BLUE );

    gw->camera.position = (Vector3) { 0.0f, 0.0f, 0.0f };
    gw->camera.target = gw->player->pos;
    gw->camera.up = (Vector3) { 0.0f, 1.0f, 0.0f };
    gw->camera.fovy = 60.0f;
    gw->camera.projection = CAMERA_PERSPECTIVE;

    return gw;

}

void destroyGameWorld( GameWorld *gw ) {
    destroyPlayer( gw->player );
    free( gw );
}

void updateGameWorld( GameWorld *gw, float delta ) {

    // breakBlock test (break blocks that are in the player column)
    if ( IsKeyDown( KEY_SPACE ) ) {

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

    if ( IsKeyDown( KEY_PAGE_UP ) ) {
        cameraAngle += 1.0f;
    }

    if ( IsKeyDown( KEY_PAGE_DOWN ) ) {
        cameraAngle -= 1.0f;
    }

    gw->player->input( gw->player );
    gw->player->update( gw->player, delta );

    float w = GetMouseWheelMove();
    if ( w < 0 ) {
        cameraDistance += 0.2f;
        cameraOffsetY += 0.2f;
    } else if ( w > 0 ) {
        cameraDistance -= 0.2f;
        cameraOffsetY -= 0.2f;
    }

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

    camera->position = (Vector3) { 
        player->pos.x + cosf( cameraAngle * DEG2RAD ) * cameraDistance, 
        player->pos.y + cameraOffsetY, 
        player->pos.z + sinf( cameraAngle * DEG2RAD ) * cameraDistance, 
    };
    camera->target = player->pos;

}