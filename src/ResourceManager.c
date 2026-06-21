/**
 * @file ResourceManager.c
 * @author Prof. Dr. David Buzatto
 * @brief ResourceManager implementation.
 * 
 * @copyright Copyright (c) 2026
 */
#include <stdio.h>
#include <stdlib.h>

#include "raylib/raylib.h"
#include "raylib/rlgl.h"

#include "ResourceManager.h"

#if defined( PLATFORM_DESKTOP )
    #define GLSL_VERSION            330
#else   // PLATFORM_ANDROID, PLATFORM_WEB
    #define GLSL_VERSION            100
#endif

ResourceManager rm = { 0 };

void loadResourcesResourceManager( void ) {

    // a .glb loads its mesh + material/texture together; the texture path is
    // resolved relative to the .glb's own folder.
    rm.playerModel = LoadModel( "resources/models/player/character-k.glb" );

    rm.pickaxeModel = LoadModel( "resources/models/pickaxe/minecraft-diamond-pickaxe.glb" );

    rm.blockTypeAtlas = LoadTexture( "resources/images/block-type-atlas.png" );
    SetTextureFilter( rm.blockTypeAtlas, TEXTURE_FILTER_POINT );

    // equirectangular panorama, loaded as a GPU texture so genTextureCubemap can
    // render it onto the 6 cubemap faces. Our raylib build has no HDR support, so
    // we use a plain PNG panorama (not a .hdr file).
    rm.skyPanorama = LoadTexture( "resources/images/raylib-example-sky-panorama.png" );

    rm.skyboxShader = LoadShader(
        TextFormat( "resources/shaders/glsl%i/skybox.vs", GLSL_VERSION ),
        TextFormat( "resources/shaders/glsl%i/skybox.fs", GLSL_VERSION )
    );

    rm.skyboxCubemapShader = LoadShader(
        TextFormat( "resources/shaders/glsl%i/cubemap.vs", GLSL_VERSION ),
        TextFormat( "resources/shaders/glsl%i/cubemap.fs", GLSL_VERSION )
    );
    
}

void unloadResourcesResourceManager( void ) {
    UnloadModel( rm.playerModel );
    UnloadModel( rm.pickaxeModel );
    UnloadTexture( rm.blockTypeAtlas );
    UnloadTexture( rm.skyPanorama );
    UnloadShader( rm.skyboxShader );
    UnloadShader( rm.skyboxCubemapShader );
}