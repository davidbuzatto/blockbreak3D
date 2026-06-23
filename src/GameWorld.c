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
#include "raylib/rlgl.h"

#include "GameWorld.h"
#include "Macros.h"
#include "Map.h"
#include "Player.h"
#include "ResourceManager.h"
#include "SoundPool.h"

static const float CAMERA_YAW_SPEED      = 90.0f;   // yaw rotation speed (deg/s)
static const float CAMERA_PITCH_SPEED    = 90.0f;   // pitch rotation speed (deg/s)
static const float CAMERA_PITCH_MIN      = 5.0f;    // keep the camera above the ground
static const float CAMERA_PITCH_MAX      = 85.0f;   // keep the camera from flipping over
static const float CAMERA_ZOOM_STEP      = 1.0f;    // distance change per wheel notch
static const float CAMERA_DISTANCE_MIN   = 3.0f;    // closest zoom
static const float CAMERA_DISTANCE_MAX   = 40.0f;   // farthest zoom
static const float CAMERA_STICK_DEADZONE = 0.1f;    // ignore tiny left-stick noise

static const float PLAYER_REACH          = 8.0f;    // how far player can target/break blocks
static const int BUILD_COST              = 1;       // materials spent to place on block

static const float MOUSE_SENSITIVITY        = 0.1f;   // degrees of look per pixel of mouse movement
static const float FP_PITCH_LIMIT           = 89.0f;  // first-person up/down look limit (deg)
static const float EYE_OFFSET_FACTOR        = 0.4f;   // eye height above the player center (× dim.y)
static const bool  GAMEPAD_INVERT_CAMERA_Y  = true;   // invert the gamepad camera Y (right stick)

// orbit camera state (spherical coordinates around the player)
static float cameraYaw      = 90.0f;   // horizontal angle (deg)
static float cameraPitch    = 30.0f;   // vertical angle (deg)
static float cameraDistance = 10.0f;   // orbit radius (world units)
static bool firstPerson     = true;    // camera mode: true = first person, false = third person

static BuildType buildTypes[40];
static int buildTypeCount;
static int currentBuildType;

static void updateCamera( Camera3D *camera, Player *player );
static void drawHud( GameWorld *gw );
static void drawTargetBlockHighlighting( GameWorld *gw );
static void drawCrosshair( void );

static TextureCubemap genTextureCubemap( Shader shader, Texture2D panorama, int size, int format );

/**
 * @brief Creates a dynamically allocated GameWorld struct instance.
 */
GameWorld *createGameWorld( void ) {

    GameWorld *gw = (GameWorld*) malloc( sizeof( GameWorld ) );

    int rows = 30;
    int cols = 30;
    int layers = 50;

    gw->map = createMap( -cols/2, 0, -rows/2, layers, rows, cols, 1 );
    gw->player = createPlayer( 0, 18, 0, 0.8f, 2.0f, BLUE );   // spawn high so it falls onto the terrain
    gw->player->map = gw->map;                        // give the player the world to collide against

    // skybox
    Mesh cube = GenMeshCube( 1.0f, 1.0f, 1.0f );
    gw->skybox = LoadModelFromMesh( cube );
    gw->skybox.materials[0].shader = rm.skyboxShader;

    // shader config
    SetShaderValue( 
        rm.skyboxShader,
        GetShaderLocation( rm.skyboxShader, "environmentMap" ),
        (int[1]){ MATERIAL_MAP_CUBEMAP },
        SHADER_UNIFORM_INT
    );
    // doGamma = 0: the panorama is a plain (sRGB) PNG, not HDR, so we must NOT
    // apply the HDR tonemap/gamma in the shader (it would wash the sky out).
    SetShaderValue(
        rm.skyboxShader,
        GetShaderLocation( rm.skyboxShader, "doGamma" ),
        (int[1]){ 0 },
        SHADER_UNIFORM_INT
    );
    // vflipped = 1: matches the face orientation genTextureCubemap produces.
    // (if the sky comes out upside-down, change this to 0.)
    SetShaderValue(
        rm.skyboxShader,
        GetShaderLocation( rm.skyboxShader, "vflipped" ),
        (int[1]){ 1 },
        SHADER_UNIFORM_INT
    );

    SetShaderValue(
        rm.skyboxCubemapShader,
        GetShaderLocation( rm.skyboxCubemapShader, "equirectangularMap" ),
        (int[1]){ 0 },
        SHADER_UNIFORM_INT
    );

    // bake the equirectangular panorama into the cubemap stored in the model's
    // CUBEMAP slot. Pass the CONVERSION shader (skyboxCubemapShader), not the draw
    // shader. (LoadTextureCubemap can't handle a panorama layout, which is exactly
    // why the sky was black before.)
    gw->skybox.materials[0].maps[MATERIAL_MAP_CUBEMAP].texture = genTextureCubemap(
        rm.skyboxCubemapShader,
        rm.skyPanorama,
        1024,
        PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
    );

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

    // build
    buildTypeCount = 0;
    buildTypes[BLOCK_GRASS]       = (BuildType) { GREEN,      0, BLOCK_GRASS,       SOUND_TYPE_GRASS  }; buildTypeCount++;
    buildTypes[BLOCK_DIRT]        = (BuildType) { BROWN,      2, BLOCK_DIRT,        SOUND_TYPE_GRAVEL }; buildTypeCount++;
    buildTypes[BLOCK_STONE]       = (BuildType) { GRAY,       3, BLOCK_STONE,       SOUND_TYPE_STONE  }; buildTypeCount++;
    buildTypes[BLOCK_OAK_PLANKS]  = (BuildType) { DARKBROWN,  4, BLOCK_OAK_PLANKS,  SOUND_TYPE_WOOD   }; buildTypeCount++;
    buildTypes[BLOCK_OAK_LOG]     = (BuildType) { DARKBROWN,  5, BLOCK_OAK_LOG,     SOUND_TYPE_WOOD   }; buildTypeCount++;
    buildTypes[BLOCK_OAK_LEAVES]  = (BuildType) { DARKGREEN,  7, BLOCK_OAK_LEAVES,  SOUND_TYPE_GRASS  }; buildTypeCount++;
    buildTypes[BLOCK_WATER]       = (BuildType) { BLUE,       8, BLOCK_WATER,       SOUND_TYPE_SNOW   }; buildTypeCount++;
    buildTypes[BLOCK_SNOW]        = (BuildType) { WHITE,      9, BLOCK_SNOW,        SOUND_TYPE_SNOW   }; buildTypeCount++;
    buildTypes[BLOCK_GLASS]       = (BuildType) { SKYBLUE,   10, BLOCK_GLASS,       SOUND_TYPE_SNOW   }; buildTypeCount++;
    buildTypes[BLOCK_ICE]         = (BuildType) { RAYWHITE,  11, BLOCK_ICE,         SOUND_TYPE_SNOW   }; buildTypeCount++;
    buildTypes[BLOCK_IRON_BLOCK]  = (BuildType) { LIGHTGRAY, 12, BLOCK_IRON_BLOCK,  SOUND_TYPE_STONE  }; buildTypeCount++;
    buildTypes[BLOCK_SAND]        = (BuildType) { BEIGE,     13, BLOCK_SAND,        SOUND_TYPE_SAND   }; buildTypeCount++;
    buildTypes[BLOCK_LAVA]        = (BuildType) { RED,       14, BLOCK_LAVA,        SOUND_TYPE_STONE  }; buildTypeCount++;
    buildTypes[BLOCK_SLATE]       = (BuildType) { DARKGRAY,  15, BLOCK_SLATE,       SOUND_TYPE_STONE  }; buildTypeCount++;
    buildTypes[BLOCK_OBSIDIAN]    = (BuildType) { BLACK,     16, BLOCK_OBSIDIAN,    SOUND_TYPE_STONE  }; buildTypeCount++;
    buildTypes[BLOCK_BRICKS]      = (BuildType) { MAROON,    17, BLOCK_BRICKS,      SOUND_TYPE_STONE  }; buildTypeCount++;
    buildTypes[BLOCK_MOSSY_STONE] = (BuildType) { GREEN,     18, BLOCK_MOSSY_STONE, SOUND_TYPE_STONE  }; buildTypeCount++;
    buildTypes[BLOCK_GRAVEL]      = (BuildType) { BROWN,     19, BLOCK_GRAVEL,      SOUND_TYPE_GRAVEL }; buildTypeCount++;
    currentBuildType = 3;

    return gw;

}

/**
 * @brief Frees the game world and everything it owns (map and player).
 */
void destroyGameWorld( GameWorld *gw ) {
    // the cubemap baked at creation lives in the model's material; unload it and
    // the cube model here (the skybox shaders are owned by the ResourceManager).
    UnloadTexture( gw->skybox.materials[0].maps[MATERIAL_MAP_CUBEMAP].texture );
    UnloadModel( gw->skybox );
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

    // on a "break" input (left mouse / B / gamepad right trigger), always swing
    // the pickaxe; break the aimed block too if there is one.
    if ( IsMouseButtonPressed( MOUSE_BUTTON_LEFT ) || IsKeyPressed( KEY_B ) ||
          ( IsGamepadAvailable( 0 ) && IsGamepadButtonPressed( 0, GAMEPAD_BUTTON_RIGHT_TRIGGER_2 ) ) ) {

        playerSwingPickaxe( gw->player );

        if ( gw->targetBlock.hit ) {
            gw->player->availableMaterials += breakBlock( gw->map, gw->targetBlock.la, gw->targetBlock.i, gw->targetBlock.j );
        }

    }

    // on a "place" input (right mouse / C / gamepad left trigger), always swing;
    // if aiming at a block and we have material, place one on the adjacent empty
    // cell (unless that cell would be inside the player).
    if ( IsMouseButtonPressed( MOUSE_BUTTON_RIGHT ) || IsKeyPressed( KEY_C ) ||
          ( IsGamepadAvailable( 0 ) && IsGamepadButtonPressed( 0, GAMEPAD_BUTTON_LEFT_TRIGGER_2 ) ) ) {
        
        playerSwingPickaxe( gw->player );

        if ( gw->targetBlock.hit && gw->player->availableMaterials >= BUILD_COST ) {

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

            if ( !overlapsPlayer && placeBlock( gw->map, pla, pi, pj, &buildTypes[currentBuildType] ) ) {
                gw->player->availableMaterials -= BUILD_COST;
            }

        }

    }

    // change build type
    if ( IsKeyPressed( KEY_ONE ) || ( IsGamepadAvailable( 0 ) && IsGamepadButtonPressed( 0, GAMEPAD_BUTTON_LEFT_TRIGGER_1 ) ) ) {
        currentBuildType--;
        if ( currentBuildType < 0 ) {
            currentBuildType = buildTypeCount - 1;
        }
    } else if ( IsKeyPressed( KEY_TWO ) || ( IsGamepadAvailable( 0 ) && IsGamepadButtonPressed( 0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1 ) ) ) {
        currentBuildType = ( currentBuildType + 1 ) % buildTypeCount;
    }

}

/**
 * @brief Draws one frame: the 3D scene (map, player, grid) plus the FPS overlay.
 */
void drawGameWorld( GameWorld *gw ) {

    BeginDrawing();
    ClearBackground( SKYBLUE );

    BeginMode3D( gw->camera );

    rlDisableBackfaceCulling();
    rlDisableDepthMask();
        DrawModel( gw->skybox, (Vector3){0, 0, 0}, 1.0f, WHITE );
    rlEnableBackfaceCulling();
    rlEnableDepthMask();

    gw->map->draw( gw->map, &gw->camera );

    // target highlight before the player/pickaxe, so the first-person viewmodel
    // (drawn on top of the scene) is never covered by the highlight outline.
    drawTargetBlockHighlighting( gw );

    if ( !firstPerson ) {
        gw->player->draw( gw->player );   // hidden in first person (camera is inside it)
    } else {
        drawPlayerPickaxeViewmodel( gw->player, &gw->camera );   // draws only the pickaxe
    }

    //DrawGrid( 100, 1 );

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

    int marginX = 10;
    int marginY = 10;
    int xStart = 10;
    int yStart = 10;
    int tileW = 32;
    int sourceW = 128;
    int hSpace = 10;
    int fontSize = 20;
    int blockTypeMenuW = buildTypeCount * ( tileW + hSpace ) - hSpace + marginX * 2;
    int xIni = GetScreenWidth() / 2 - blockTypeMenuW / 2 - 2;

    DrawRectangleRounded(
        (Rectangle) { xIni, marginY, blockTypeMenuW, 52 },
        0.5f,
        10,
        Fade( WHITE, 0.5f )
    );

    for ( int i = 0; i < buildTypeCount; i++ ) {

        BuildType *bt = &buildTypes[i];

        int col = ( bt->atlasPos ) % ATLAS_COLS;
        int row = ( bt->atlasPos ) / ATLAS_COLS;

        DrawTexturePro(
            rm.blockTypeAtlas,
            (Rectangle) { sourceW * col, sourceW * row, sourceW, sourceW },
            (Rectangle) { 
                xIni + xStart + ( tileW + hSpace ) * i,
                marginY + yStart,
                tileW, tileW
            },
            (Vector2) { 0 },
            0.0f,
            WHITE
        );

        /*const char *text = TextFormat( "%d", i + 1 );
        int textW = MeasureText( text, fontSize );
        DrawText( 
            TextFormat( "%d", i + 1 ),
            xIni + xStart + ( tileW + hSpace ) * i + tileW / 2 - textW / 2,
            marginY + yStart + fontSize / 2 - 2,
            fontSize,
            WHITE
        );*/

    }

    DrawRectangleLinesEx( 
        (Rectangle) { 
            xIni + xStart + ( tileW + hSpace ) * currentBuildType,
            marginY + yStart,
            tileW, tileW
        },
        3,
        YELLOW
    );

    const char *textAM = TextFormat( "Available Materials: %d", gw->player->availableMaterials );
    int amW = MeasureText( textAM, fontSize );
    DrawRectangleRounded(
        (Rectangle) { 
            marginX, 
            GetScreenHeight() - marginY - 30,
            amW + 10,
            30
        },
        0.5f,
        10,
        Fade( WHITE, 0.5f )
    );
    DrawText( textAM, marginX + 5, GetScreenHeight() - marginY - 25, fontSize, BLACK );

    int fps = GetFPS();
    const char *textFPS = TextFormat( "%d FPS", fps );
    int fpsW = MeasureText( textFPS, fontSize );
    DrawFPS( GetScreenWidth() - fpsW - marginX, GetScreenHeight() - marginY - 20 );

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

/**
 * @brief Generate cubemap (6 faces) from equirectangular (panorama) texture.
 *        Extracted from Raylib examples.
 */
static TextureCubemap genTextureCubemap( Shader shader, Texture2D panorama, int size, int format ) {

    TextureCubemap cubemap = { 0 };

    rlDisableBackfaceCulling();     // Disable backface culling to render inside the cube

    // STEP 1: Setup framebuffer
    //------------------------------------------------------------------------------------------
    unsigned int rbo = rlLoadTextureDepth(size, size, true);
    cubemap.id = rlLoadTextureCubemap(0, size, format, 1);

    unsigned int fbo = rlLoadFramebuffer();
    rlFramebufferAttach(fbo, rbo, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
    rlFramebufferAttach(fbo, cubemap.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_CUBEMAP_POSITIVE_X, 0);

    // Check if framebuffer is complete with attachments (valid)
    if (rlFramebufferComplete(fbo)) TraceLog(LOG_INFO, "FBO: [ID %i] Framebuffer object created successfully", fbo);
    //------------------------------------------------------------------------------------------

    // STEP 2: Draw to framebuffer
    //------------------------------------------------------------------------------------------
    // NOTE: the conversion shader renders the equirectangular panorama onto the 6 cubemap faces
    rlEnableShader(shader.id);

    // Define projection matrix and send it to shader
    Matrix matFboProjection = MatrixPerspective(90.0*DEG2RAD, 1.0, rlGetCullDistanceNear(), rlGetCullDistanceFar());
    rlSetUniformMatrix(shader.locs[SHADER_LOC_MATRIX_PROJECTION], matFboProjection);

    // Define view matrix for every side of the cubemap
    Matrix fboViews[6] = {
        MatrixLookAt((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){  1.0f,  0.0f,  0.0f }, (Vector3){ 0.0f, -1.0f,  0.0f }),
        MatrixLookAt((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ -1.0f,  0.0f,  0.0f }, (Vector3){ 0.0f, -1.0f,  0.0f }),
        MatrixLookAt((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){  0.0f,  1.0f,  0.0f }, (Vector3){ 0.0f,  0.0f,  1.0f }),
        MatrixLookAt((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){  0.0f, -1.0f,  0.0f }, (Vector3){ 0.0f,  0.0f, -1.0f }),
        MatrixLookAt((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){  0.0f,  0.0f,  1.0f }, (Vector3){ 0.0f, -1.0f,  0.0f }),
        MatrixLookAt((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){  0.0f,  0.0f, -1.0f }, (Vector3){ 0.0f, -1.0f,  0.0f })
    };

    rlViewport(0, 0, size, size);   // Set viewport to current fbo dimensions

    // Activate and enable texture for drawing to cubemap faces
    rlActiveTextureSlot(0);
    rlEnableTexture(panorama.id);

    for (int i = 0; i < 6; i++)
    {
        // Set the view matrix for the current cube face
        rlSetUniformMatrix(shader.locs[SHADER_LOC_MATRIX_VIEW], fboViews[i]);

        // Select the current cubemap face attachment for the fbo
        // WARNING: This function by default enables->attach->disables fbo!!!
        rlFramebufferAttach(fbo, cubemap.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_CUBEMAP_POSITIVE_X + i, 0);
        rlEnableFramebuffer(fbo);

        // Load and draw a cube, it uses the current enabled texture
        rlClearScreenBuffers();
        rlLoadDrawCube();

        // ALTERNATIVE: Try to use internal batch system to draw the cube instead of rlLoadDrawCube
        // for some reason this method does not work, maybe due to cube triangles definition? normals pointing out?
        // TODO: Investigate this issue...
        //rlSetTexture(panorama.id); // WARNING: It must be called after enabling current framebuffer if using internal batch system!
        //rlClearScreenBuffers();
        //DrawCubeV(Vector3Zero(), Vector3One(), WHITE);
        //rlDrawRenderBatchActive();
    }
    //------------------------------------------------------------------------------------------

    // STEP 3: Unload framebuffer and reset state
    //------------------------------------------------------------------------------------------
    rlDisableShader();          // Unbind shader
    rlDisableTexture();         // Unbind texture
    rlDisableFramebuffer();     // Unbind framebuffer
    rlUnloadFramebuffer(fbo);   // Unload framebuffer (and automatically attached depth texture/renderbuffer)

    // Reset viewport dimensions to default
    rlViewport(0, 0, rlGetFramebufferWidth(), rlGetFramebufferHeight());
    rlEnableBackfaceCulling();
    //------------------------------------------------------------------------------------------

    cubemap.width = size;
    cubemap.height = size;
    cubemap.mipmaps = 1;
    cubemap.format = format;

    return cubemap;

}