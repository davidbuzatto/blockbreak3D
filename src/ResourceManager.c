/**
 * @file ResourceManager.c
 * @author Prof. Dr. David Buzatto
 * @brief ResourceManager implementation.
 * 
 * @copyright Copyright (c) 2026
 */
#include <stdio.h>
#include <stdlib.h>

#include "ResourceManager.h"
#include "raylib/raylib.h"

ResourceManager rm = { 0 };

void loadResourcesResourceManager( void ) {

    // a .glb loads its mesh + material/texture together; the texture path is
    // resolved relative to the .glb's own folder.
    rm.playerModel = LoadModel( "resources/models/player/character-k.glb" );

    rm.pickaxeModel = LoadModel( "resources/models/pickaxe/minecraft-diamond-pickaxe.glb" );

    rm.blockTypeAtlas = LoadTexture( "resources/images/block-type-atlas.png" );
    SetTextureFilter( rm.blockTypeAtlas, TEXTURE_FILTER_POINT );
    
}

void unloadResourcesResourceManager( void ) {
    UnloadModel( rm.playerModel );   // frees the mesh, material and textures
    UnloadModel( rm.pickaxeModel );
    UnloadTexture( rm.blockTypeAtlas );
}