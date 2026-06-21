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
static const int BUILD_COST = 1;           // materials spent to place on block

static const float MOUSE_SENSITIVITY        = 0.1f;   // degrees of look per pixel of mouse movement
static const float FP_PITCH_LIMIT           = 89.0f;  // first-person up/down look limit (deg)
static const float EYE_OFFSET_FACTOR        = 0.4f;   // eye height above the player center (× dim.y)
static const bool  GAMEPAD_INVERT_CAMERA_Y  = true;   // invert the gamepad camera Y (right stick)

// orbit camera state (spherical coordinates around the player)
static float cameraYaw      = 90.0f;   // horizontal angle (deg)
static float cameraPitch    = 30.0f;   // vertical angle (deg)
static float cameraDistance = 10.0f;   // orbit radius (world units)
static bool firstPerson     = false;   // camera mode: true = first person, false = third person

static void updateCamera( Camera3D *camera, Player *player );
static void drawHud( GameWorld *gw );
static void drawTargetBlockHighlighting( GameWorld *gw );
static void drawCrosshair( void );

/**
 * @brief Creates a dynamically allocated GameWorld struct instance.
 */
GameWorld *createGameWorld( void ) {

    GameWorld *gw = (GameWorld*) malloc( sizeof( GameWorld ) );

    int rows = 60;
    int cols = 60;
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

    // start in first person, so lock the cursor right away for mouse-look.
    if ( firstPerson ) {
        DisableCursor();
    }

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

    // toggle first / third person (F5 or gamepad start).
    // third person is for debug.
    if ( IsKeyPressed( KEY_F5 ) || ( IsGamepadAvailable( 0 ) && IsGamepadButtonPressed( 0, GAMEPAD_BUTTON_MIDDLE_RIGHT ) ) ) {
        firstPerson = !firstPerson;
        if ( firstPerson ) {
            DisableCursor();   // lock cursor for mouse-look
        } else {
            EnableCursor();    // free cursor in debug view
        }
    }
    
    // --- camera look: arrows / right stick / mouse all adjust yaw & pitch ---
    // pitch convention: larger pitch = looking more downward.
    bool rightDown = IsKeyDown( KEY_RIGHT );
    bool leftDown  = IsKeyDown( KEY_LEFT );
    bool upDown    = IsKeyDown( KEY_UP );
    bool downDown  = IsKeyDown( KEY_DOWN );

    // yaw: turn right / left
    if ( rightDown ) {
        cameraYaw += CAMERA_YAW_SPEED * delta;
    }
    if ( leftDown ) {
        cameraYaw -= CAMERA_YAW_SPEED * delta;
    }

    // pitch: up arrow looks up (pitch decreases), down arrow looks down.
    if ( upDown ) {
        cameraPitch -= CAMERA_PITCH_SPEED * delta;
    }
    if ( downDown ) {
        cameraPitch += CAMERA_PITCH_SPEED * delta;
    }

    // gamepad right stick (only when no arrow is held)
    if ( IsGamepadAvailable( 0 ) && !rightDown && !leftDown && !upDown && !downDown ) {
        float rightAnalogX = GetGamepadAxisMovement( 0, GAMEPAD_AXIS_RIGHT_X );
        float rightAnalogY = GetGamepadAxisMovement( 0, GAMEPAD_AXIS_RIGHT_Y );
        if ( rightAnalogX < -CAMERA_STICK_DEADZONE || rightAnalogX > CAMERA_STICK_DEADZONE ) {
            cameraYaw += CAMERA_YAW_SPEED * delta * rightAnalogX;
        }
        if ( rightAnalogY < -CAMERA_STICK_DEADZONE || rightAnalogY > CAMERA_STICK_DEADZONE ) {
            // by default stick down (+Y) = look down; GAMEPAD_INVERT_CAMERA_Y flips it.
            float invertY = GAMEPAD_INVERT_CAMERA_Y ? -1.0f : 1.0f;
            cameraPitch += CAMERA_PITCH_SPEED * delta * rightAnalogY * invertY;
        }

    }

    // mouse-look (first person only; the cursor is locked)
    if ( firstPerson ) {
        Vector2 mouseDelta = GetMouseDelta();
        cameraYaw   += mouseDelta.x * MOUSE_SENSITIVITY;
        cameraPitch += mouseDelta.y * MOUSE_SENSITIVITY; // mouse down (+Y) = look down
    }

    // clamp pitch: wide range in first person, "above ground" range in third.
    if ( firstPerson ) {
        cameraPitch = Clamp( cameraPitch, -FP_PITCH_LIMIT, FP_PITCH_LIMIT );
    } else {
        cameraPitch = Clamp( cameraPitch, CAMERA_PITCH_MIN, CAMERA_PITCH_MAX );
    }

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
    // 64 = how far the crosshair "sees"; the player's reach is applied below.
    Vector2 screenCenter = { GetScreenWidth() / 2, GetScreenHeight() / 2 };
    Ray ray = GetScreenToWorldRay( screenCenter, gw->camera );
    gw->targetBlock = mapRaycast( gw->map, ray, 64.0f );

    // drop the target if it's beyond the player's reach, so the highlight and
    // breaking stay consistent (you can only target what you can reach).
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

    // break the aimed block instantly (left mouse / B key / gamepad right trigger).
    if ( gw->targetBlock.hit &&
        ( IsMouseButtonPressed( MOUSE_BUTTON_LEFT ) || IsKeyPressed( KEY_B ) ||
          ( IsGamepadAvailable( 0 ) && IsGamepadButtonPressed( 0, GAMEPAD_BUTTON_RIGHT_TRIGGER_2 ) ) ) ) {
        gw->player->availableMaterials += breakBlock( gw->map, gw->targetBlock.la, gw->targetBlock.i, gw->targetBlock.j );
    }

    // place a block on the empty cell next to the aimed block (right mouse / C /
    // gamepad left trigger), if we have material and the spot isn't the player.
    if ( gw->targetBlock.hit && gw->player->availableMaterials >= BUILD_COST &&
        ( IsMouseButtonPressed( MOUSE_BUTTON_RIGHT ) || IsKeyPressed( KEY_C ) ||
          ( IsGamepadAvailable( 0 ) && IsGamepadButtonPressed( 0, GAMEPAD_BUTTON_LEFT_TRIGGER_2 ) ) ) ) {
        
        int pla = gw->targetBlock.pla;
        int pi  = gw->targetBlock.pi;
        int pj  = gw->targetBlock.pj;

        // world center of the cell we'd fill.
        Vector3 bc = {
            gw->map->pos.x + gw->map->blockSize * pj,
            gw->map->pos.y + gw->map->blockSize * pla,
            gw->map->pos.z + gw->map->blockSize * pi
        };

        // AABB overlap test: don't place a block inside the player.
        Vector3 d = gw->player->dim;
        float bs = gw->map->blockSize;
        bool overlapsPlayer =
            fabsf( bc.x - gw->player->pos.x ) < ( bs + d.x ) * 0.5f &&
            fabsf( bc.y - gw->player->pos.y ) < ( bs + d.y ) * 0.5f &&
            fabsf( bc.z - gw->player->pos.z ) < ( bs + d.z ) * 0.5f;

        if ( !overlapsPlayer && placeBlock( gw->map, pla, pi, pj, DARKBROWN ) ) {
            gw->player->availableMaterials -= BUILD_COST;
        }

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
    if ( !firstPerson ) {
        gw->player->draw( gw->player );   // hidden in first person (camera is inside it)
    }
    drawTargetBlockHighlighting( gw );
    DrawGrid( 100, 1 );

    EndMode3D();

    drawCrosshair();
    drawHud( gw );

    EndDrawing();

}

/**
 * @brief Positions the camera from the current yaw/pitch (a shared forward look
 *        direction). First person: at the player's eyes, looking forward. Third
 *        person: orbiting behind/above at cameraDistance, looking at the player.
 */
void updateCamera( Camera3D *camera, Player *player ) {

    float yaw   = cameraYaw * DEG2RAD;
    float pitch = cameraPitch * DEG2RAD;

    // forward look direction, shared by both modes (larger pitch = more downward).
    Vector3 forward = {
        -cosf( pitch ) * cosf( yaw ),
        -sinf( pitch ),
        -cosf( pitch ) * sinf( yaw )
    };

    if ( firstPerson ) {

        // camera at the player's eyes, looking forward.
        Vector3 eye = {
            player->pos.x,
            player->pos.y + player->dim.y * EYE_OFFSET_FACTOR,
            player->pos.z
        };
        camera->position = eye;
        camera->target = Vector3Add( eye, forward );

    } else {

        // third person: orbit behind/above, looking at the player.
        float horizontal = cameraDistance * cosf( pitch );
        float height     = cameraDistance * sinf( pitch );
        camera->position = (Vector3) {
            player->pos.x + horizontal * cosf( yaw ),
            player->pos.y + height,
            player->pos.z + horizontal * sinf( yaw )
        };
        camera->target = player->pos;

    }

}

/**
 * @brief Draws the 2D HUD: the available-materials counter (on a translucent
 *        background) plus the FPS.
 */
static void drawHud( GameWorld *gw ) {

    const char *text = TextFormat( "Available Materials: %d", gw->player->availableMaterials );
    int w = MeasureText( text, 20 );
    DrawRectangle( 10, 10, w + 10, 30, Fade( WHITE, 0.5f ) );
    DrawText( text, 15, 15, 20, BLACK );

    DrawFPS( 10, GetScreenHeight() - 25 );

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