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
#include "Macros.h"
#include "Map.h"
#include "Player.h"
#include "ResourceManager.h"

static const float CAMERA_YAW_SPEED      = 90.0f;   // yaw rotation speed (deg/s)
static const float CAMERA_PITCH_SPEED    = 90.0f;   // pitch rotation speed (deg/s)
static const float CAMERA_PITCH_MIN      = 5.0f;    // keep the camera above the ground
static const float CAMERA_PITCH_MAX      = 85.0f;   // keep the camera from flipping over
static const float CAMERA_ZOOM_STEP      = 1.0f;    // distance change per wheel notch
static const float CAMERA_DISTANCE_MIN   = 3.0f;    // closest zoom
static const float CAMERA_DISTANCE_MAX   = 40.0f;   // farthest zoom
static const float CAMERA_STICK_DEADZONE = 0.1f;    // ignore tiny left-stick noise

static const float PLAYER_REACH = 8.0f;    // how far player can target/break blocks

// orbit camera state (spherical coordinates around the player)
float cameraYaw      = 90.0f;   // horizontal angle (deg)
float cameraPitch    = 30.0f;   // vertical angle (deg)
float cameraDistance = 10.0f;   // orbit radius (world units)

static void updateCamera( Camera3D *camera, Player *player );
static void drawTargetBlockHighlighting( GameWorld *gw );
static void drawCrosshair( void );

/**
 * @brief Creates a dynamically allocated GameWorld struct instance.
 */
GameWorld *createGameWorld( void ) {

    GameWorld *gw = (GameWorld*) malloc( sizeof( GameWorld ) );

    int rows = 100;
    int cols = 100;
    int layers = 50;

    gw->map = createMap( -cols/2, 0, -rows/2, layers, rows, cols, 1 );
    gw->player = createPlayer( 0, 18, 0, 0.8f, 2.0f, BLUE );   // spawn high so it falls onto the terrain
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

    // --- orbit camera controls (keyboard) ---
    bool rightDown = IsKeyDown( KEY_RIGHT );
    bool leftDown = IsKeyDown( KEY_LEFT );
    bool upDown = IsKeyDown( KEY_UP );
    bool downDown = IsKeyDown( KEY_DOWN );

    // yaw: orbit left / right
    if ( rightDown ) {
        cameraYaw += CAMERA_YAW_SPEED * delta;
    }
    if ( leftDown ) {
        cameraYaw -= CAMERA_YAW_SPEED * delta;
    }

    // pitch: orbit up / down, clamped so the camera never flips or goes underground
    if ( upDown ) {
        cameraPitch += CAMERA_PITCH_SPEED * delta;
    }
    if ( downDown ) {
        cameraPitch -= CAMERA_PITCH_SPEED * delta;
    }

    // use gamepad only if none of the keys are down
    if ( IsGamepadAvailable( 0 ) && !rightDown && !leftDown && !upDown && !downDown ) {
        float rightAnalogX = GetGamepadAxisMovement( 0, GAMEPAD_AXIS_RIGHT_X );
        float rightAnalogY = GetGamepadAxisMovement( 0, GAMEPAD_AXIS_RIGHT_Y );
        if ( rightAnalogX < -CAMERA_STICK_DEADZONE || rightAnalogX > CAMERA_STICK_DEADZONE ) {
            cameraYaw += CAMERA_YAW_SPEED * delta * rightAnalogX;
        }
        if ( rightAnalogY < -CAMERA_STICK_DEADZONE || rightAnalogY > CAMERA_STICK_DEADZONE ) {
            cameraPitch -= CAMERA_PITCH_SPEED * delta * rightAnalogY; // invert y axis
        }
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

    // raycast from the screen center (crosshair) to find the aimed block.
    // 64 = how far the crosshair "sees"; a player-distance reach comes in 2.2.
    Vector2 screenCenter = { GetScreenWidth() / 2, GetScreenHeight() / 2 };
    Ray ray = GetScreenToWorldRay( screenCenter, gw->camera );
    gw->targetBlock = mapRaycast( gw->map, ray, 64.0f );

    if ( gw->targetBlock.hit ) {

        Vector3 blockCenter = {
            gw->map->pos.x + gw->map->blockSize * gw->targetBlock.j,
            gw->map->pos.y + gw->map->blockSize * gw->targetBlock.la,
            gw->map->pos.z + gw->map->blockSize * gw->targetBlock.i
        };

        if ( Vector3Distance( gw->player->pos, blockCenter ) > PLAYER_REACH ) {
            gw->targetBlock.hit = false;
        }

    }

    if ( gw->targetBlock.hit && 
        ( IsMouseButtonPressed( MOUSE_BUTTON_LEFT ) || IsKeyPressed( KEY_B ) ||
          ( IsGamepadAvailable( 0 ) && IsGamepadButtonPressed( 0, GAMEPAD_BUTTON_RIGHT_TRIGGER_2 ) ) ) ) {
        breakBlock( gw->map, gw->targetBlock.la, gw->targetBlock.i, gw->targetBlock.j );
    }

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
    drawTargetBlockHighlighting( gw );
    DrawGrid( 100, 1 );

    EndMode3D();

    drawCrosshair();
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

/**
 * @brief Draws a wireframe box around the block currently under the crosshair.
 */
static void drawTargetBlockHighlighting( GameWorld *gw ) {

    if ( gw->targetBlock.hit ) {

        // world center of the hit block (grid coords -> world).
        Vector3 blockCenter = {
            gw->map->pos.x + gw->map->blockSize * gw->targetBlock.j,
            gw->map->pos.y + gw->map->blockSize * gw->targetBlock.la,
            gw->map->pos.z + gw->map->blockSize * gw->targetBlock.i
        };

        // slightly enlarged so the wires don't z-fight with the block's faces.
        float s = gw->map->blockSize * 1.02f;
        DrawCubeWires( blockCenter, s, s, s, YELLOW );
    }

}

/**
 * @brief Draws a small 2D crosshair at the center of the screen.
 */
static void drawCrosshair( void ) {

    int cx = GetScreenWidth() / 2;
    int cy = GetScreenHeight() / 2;

    DrawLine( cx - 8, cy, cx + 8, cy, BLACK );
    DrawLine( cx, cy - 8, cx, cy + 8, BLACK );

}