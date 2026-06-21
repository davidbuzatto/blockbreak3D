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
#include "raylib/rlgl.h"

#include "Macros.h"
#include "Map.h"
#include "Player.h"
#include "ResourceManager.h"

static const float GRAVITY             = -25.0f;  // downward acceleration (units/s^2)
static const float TERMINAL_VELOCITY   = -50.0f;  // maximum fall speed (clamp)
static const float JUMP_SPEED          = 9.0f;    // initial upward velocity of a jump
static const float MODEL_FACING_OFFSET = 0.0f;    // corrects the model's default facing
static const float MAX_STEP_HEIGHT     = 1.0f;    // tallest ledge the player auto-steps up
static const float STICK_DEADZONE      = 0.1f;    // ignore tiny left-stick noise

static const float PICKAXE_TARGET_SCALE = 1.0f;   // desired world size (auto-scaled from the model)
static const float PICKAXE_OFFSET_X     = -0.3f;   // hand offset: right (+) / left (-)
static const float PICKAXE_OFFSET_Y     = 0.05f;   // hand offset: up (+) / down (-)
static const float PICKAXE_OFFSET_Z     = 0.45f;   // hand offset: forward (+) / back (-)
static const float PICKAXE_ROT_X        = 94.0f;   // rotation around X (deg)
static const float PICKAXE_ROT_Y        = -3.0f;   // rotation around Y (deg)
static const float PICKAXE_ROT_Z        = 61.0f;   // rotation around Z (deg)
static const float PICKAXE_GRIP_X       = -0.4f;   // swing pivot X
static const float PICKAXE_GRIP_Y       = 0.0f;    // swing pivot Y
static const float PICKAXE_GRIP_Z       = 0.4f;    // swing pivot Z

static const float SWING_DURATION  = 0.25f;  // seconds for one full swing
static const float SWING_AMPLITUDE = -70.0f;  // peek swing angle (deg)

static void input( Player *player );
static void update( Player *player, float delta );
static void draw( Player *player );
static void moveAxis( Player *player, float *coord, float amount );

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

    // scale the pickaxe so its largest dimension matches the PICKAXE_TARGET_SIZE.
    BoundingBox pbb = GetModelBoundingBox( rm.pickaxeModel );
    float pickaxeMax = fmaxf( 
        pbb.max.x - pbb.min.x,
        fmaxf(
            pbb.max.y - pbb.min.y,
            pbb.max.z - pbb.min.z
        )
    );
    new->pickaxeScale = PICKAXE_TARGET_SCALE / pickaxeMax;

    new->map = NULL;
    new->onGround = false;
    new->availableMaterials = 200;

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

void playerSwingPickaxe( Player *player ) {
    player->swingTimer = SWING_DURATION;  // restart swing
}

/**
 * @brief Reads input: sets horizontal velocity (camera-relative) and triggers a
 *        jump. Vertical velocity is otherwise controlled by gravity in update().
 */
static void input( Player *player ) {

    // pickaxe positioning
    /*if ( IsKeyPressed( KEY_U ) ) {
        PICKAXE_ROT_X += IsKeyDown( KEY_LEFT_CONTROL ) ? -1 : 1;
        trace( "x: %.2f, y: %.2f, z: %.2f", PICKAXE_ROT_X, PICKAXE_ROT_Y, PICKAXE_ROT_Z );
    }
    if ( IsKeyPressed( KEY_I ) ) {
        PICKAXE_ROT_Y += IsKeyDown( KEY_LEFT_CONTROL ) ? -1 : 1;
        trace( "x: %.2f, y: %.2f, z: %.2f", PICKAXE_ROT_X, PICKAXE_ROT_Y, PICKAXE_ROT_Z );
    }
    if ( IsKeyPressed( KEY_O ) ) {
        PICKAXE_ROT_Z += IsKeyDown( KEY_LEFT_CONTROL ) ? -1 : 1;
        trace( "x: %.2f, y: %.2f, z: %.2f", PICKAXE_ROT_X, PICKAXE_ROT_Y, PICKAXE_ROT_Z );
    }

    if ( IsKeyPressed( KEY_J ) ) {
        PICKAXE_OFFSET_X += IsKeyDown( KEY_LEFT_CONTROL ) ? -0.05 : 0.05;
        trace( "x: %.2f, y: %.2f, z: %.2f", PICKAXE_OFFSET_X, PICKAXE_OFFSET_Y, PICKAXE_OFFSET_Z );
    }
    if ( IsKeyPressed( KEY_K ) ) {
        PICKAXE_OFFSET_Y += IsKeyDown( KEY_LEFT_CONTROL ) ? -0.05 : 0.05;
        trace( "x: %.2f, y: %.2f, z: %.2f", PICKAXE_OFFSET_X, PICKAXE_OFFSET_Y, PICKAXE_OFFSET_Z );
    }
    if ( IsKeyPressed( KEY_L ) ) {
        PICKAXE_OFFSET_Z += IsKeyDown( KEY_LEFT_CONTROL ) ? -0.05 : 0.05;
        trace( "x: %.2f, y: %.2f, z: %.2f", PICKAXE_OFFSET_X, PICKAXE_OFFSET_Y, PICKAXE_OFFSET_Z );
    }

    if ( IsKeyPressed( KEY_J ) ) {
        PICKAXE_GRIP_X += IsKeyDown( KEY_LEFT_CONTROL ) ? -0.05 : 0.05;
        trace( "x: %.2f, y: %.2f, z: %.2f", PICKAXE_GRIP_X, PICKAXE_GRIP_Y, PICKAXE_GRIP_Z );
    }
    if ( IsKeyPressed( KEY_K ) ) {
        PICKAXE_GRIP_Y += IsKeyDown( KEY_LEFT_CONTROL ) ? -0.05 : 0.05;
        trace( "x: %.2f, y: %.2f, z: %.2f", PICKAXE_GRIP_X, PICKAXE_GRIP_Y, PICKAXE_GRIP_Z );
    }
    if ( IsKeyPressed( KEY_L ) ) {
        PICKAXE_GRIP_Z += IsKeyDown( KEY_LEFT_CONTROL ) ? -0.05 : 0.05;
        trace( "x: %.2f, y: %.2f, z: %.2f", PICKAXE_GRIP_X, PICKAXE_GRIP_Y, PICKAXE_GRIP_Z );
    }*/

    // movement intent on two camera-relative axes, -1..+1 each:
    //     strafe  = sideways,  forward = toward where the camera looks.
    float strafe  = ( IsKeyDown( KEY_A ) ? -1.0f : 0.0f ) + ( IsKeyDown( KEY_D ) ? 1.0f : 0.0f );
    float forward = ( IsKeyDown( KEY_S ) ? -1.0f : 0.0f ) + ( IsKeyDown( KEY_W ) ? 1.0f : 0.0f );

    // if no movement key is held, fall back to the gamepad left stick (analog).
    bool gamepadAvailable = IsGamepadAvailable( 0 );
    if ( gamepadAvailable && strafe == 0.0f && forward == 0.0f ) {

        float leftAnalogX = GetGamepadAxisMovement( 0, GAMEPAD_AXIS_LEFT_X );
        float leftAnalogY = GetGamepadAxisMovement( 0, GAMEPAD_AXIS_LEFT_Y );

        // radial deadzone (by vector length, so it doesn't bias diagonals).
        if ( leftAnalogX * leftAnalogX + leftAnalogY * leftAnalogY > STICK_DEADZONE * STICK_DEADZONE ) {
            strafe  = leftAnalogX;     // stick right = +X
            forward = -leftAnalogY;    // stick up = -Y  ->  forward
        }
    }

    // build the camera-relative basis on the XZ plane from the camera angle.
    float a = player->cameraAngle * DEG2RAD;
    Vector3 fwd = { -cosf( a ), 0.0f, -sinf( a ) };  // toward the camera's view direction
    Vector3 right = { sinf( a ), 0.0f, -cosf( a ) }; // perpendicular, to the right

    // combine both directions weighted by the input.
    Vector3 move = {
        fwd.x * forward + right.x * strafe,
        0.0f,
        fwd.z * forward + right.z * strafe
    };

    // clamp the move length to 1: keyboard diagonals (length ~1.41) normalize
    // down to full speed, while a half-pushed analog stick (length < 1) stays
    // slower. Scaling each axis by its own stick value instead would shrink
    // diagonals to ~0.7 -- that was the "diagonal is slow" bug.
    float len = Vector3Length( move );
    if ( len > 1.0f ) {
        move = Vector3Scale( move, 1.0f / len );
    }

    player->vel.x = move.x * player->walkingSpeed;
    player->vel.z = move.z * player->walkingSpeed;

    // jump: only when on the ground and only on the key press (not while held),
    // otherwise it would re-jump every frame.
    if ( player->onGround && ( IsKeyPressed( KEY_SPACE ) || ( gamepadAvailable && IsGamepadButtonPressed( 0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN ) ) ) ) {
        player->vel.y = JUMP_SPEED;
        player->onGround = false;
    }

}

/**
 * @brief Advances the player's physics for one frame: horizontal movement
 *        (wall collision + auto-step), gravity, and vertical movement
 *        (ground/ceiling collision, sub-stepped against tunneling).
 *
 * Horizontal is resolved one axis at a time so the player slides along walls,
 * and climbs a 1-block ledge when possible (see moveAxis). Vertical is split
 * into sub-steps no larger than a block, so a fast fall stops on the surface
 * instead of skipping through it; landing snaps the feet onto the block top.
 */
static void update( Player *player, float delta ) {

    // --- tick down the pickaxe swing.
    if ( player->swingTimer > 0.0f ) {
        player->swingTimer -= delta;
        if ( player->swingTimer < 0.0f ) {
            player->swingTimer = 0.0f;
        }
    }

    // --- horizontal movement, one axis at a time so we can slide along walls ---

    // movement with auto-step.
    moveAxis( player, &player->pos.x, player->vel.x * delta );
    moveAxis( player, &player->pos.z, player->vel.z * delta );

    // movement without auto-step.
    // X axis: move, then undo only X if it put us inside a block.
    /*
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
    */

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

    // --- vertical movement + ground/ceiling collision, SUB-STEPPED so a fast fall can't pass
    //     through the ground (or snap onto a buried block) ---
    float dy = player->vel.y * delta;

    // split dy into chunks no larger than (almost) one block, so the box always
    // meets a floor/ceiling before it could skip past it.
    float maxStep = player->map->blockSize * 0.9f;
    int steps = (int) ceilf( fabsf( dy ) / maxStep );
    if ( steps < 1 ) {
        steps = 1;
    }
    float stepY = dy / steps;

    // assume airborne; a downward sub-step that lands sets this back to true.
    player->onGround = false;

    for ( int s = 0; s < steps; s++ ) {

        player->pos.y += stepY;

        if ( !mapBoxCollides( player->map, player->pos, player->dim ) ) {
            continue;  // this slice is clear, keep falling/rising
        }

        if ( dy <= 0.0f ) {

            // falling: snap the feet onto the top of the block we just touched.
            float feetY = player->pos.y - player->dim.y * 0.5f;
            int la = (int) floorf( ( feetY - player->map->pos.y ) / player->map->blockSize + 0.5f );
            float blockTop = player->map->pos.y + player->map->blockSize * la + player->map->blockSize * 0.5f;
            player->pos.y = blockTop + player->dim.y * 0.5f;
            player->onGround = true;

        } else {
            // rising into a ceiling: undo just this sub-step.
            player->pos.y -= stepY;
        }

        player->vel.y = 0.0f;
        break;   // resolved this frame's vertical movement; stop slicing.

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

    float swingAngle = 0.0f;
    if ( player->swingTimer > 0.0f ) {
        float progress = 1.0f - player->swingTimer / SWING_DURATION;
        swingAngle = sinf( progress * PI ) * SWING_AMPLITUDE;
    }

    rlPushMatrix();

        rlTranslatef( player->pos.x, player->pos.y, player->pos.z );
        rlRotatef( player->facingAngle + MODEL_FACING_OFFSET, 0.0f, 1.0f, 0.0f );
        rlTranslatef( PICKAXE_OFFSET_X, PICKAXE_OFFSET_Y, PICKAXE_OFFSET_Z );
        
        rlRotatef( PICKAXE_ROT_X, 1.0f, 0.0f, 0.0f );
        rlRotatef( PICKAXE_ROT_Y, 0.0f, 1.0f, 0.0f );
        rlRotatef( PICKAXE_ROT_Z, 0.0f, 0.0f, 1.0f );

        // debug grip pivot
        //rawSphere( (Vector3) { PICKAXE_GRIP_X, PICKAXE_GRIP_Y, PICKAXE_GRIP_Z }, 0.06f, RED );

        rlTranslatef( PICKAXE_GRIP_X, PICKAXE_GRIP_Y, PICKAXE_GRIP_Z );
        rlRotatef( swingAngle, 0.0f, 1.0f, 0.0f );
        rlTranslatef( -PICKAXE_GRIP_X, -PICKAXE_GRIP_Y, -PICKAXE_GRIP_Z );
        DrawModel( rm.pickaxeModel, (Vector3) { 0 }, player->pickaxeScale, WHITE );

    rlPopMatrix();

    // debug: the collision box (player->dim) drawn around the model.
    //DrawCubeWiresV( player->pos, player->dim, BLACK );

}

/**
 * @brief Moves the player along one horizontal axis by 'amount', resolving
 *        collision and auto-stepping up a 1-block ledge.
 *
 * 'coord' points at the coordinate to change (&player->pos.x or &player->pos.z),
 * so the same logic serves both X and Z.
 */
static void moveAxis( Player *player, float *coord, float amount ) {

    float before = *coord;
    *coord += amount;

    // moved freely? nothing else to do.
    if ( !mapBoxCollides( player->map, player->pos, player->dim ) ) {
        return;
    }

    // blocked. only try to climb when on the ground (no wall-climbing mid-air).
    if ( player->onGround ) {

        // try the same move lifted by a step: if it's clear now, we climbed a
        // ledge; the gravity + ground snap later this frame settles us onto it.
        player->pos.y += MAX_STEP_HEIGHT;
        if ( !mapBoxCollides( player->map, player->pos, player->dim ) ) {
            return;   // stepped up successfully
        }
        player->pos.y -= MAX_STEP_HEIGHT;   // still blocked even when raised
    }

    // truly blocked (a real wall): undo the horizontal move.
    *coord = before;

}
