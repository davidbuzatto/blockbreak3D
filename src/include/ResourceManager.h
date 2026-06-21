/**
 * @file ResourceManager.h
 * @author Prof. Dr. David Buzatto
 * @brief ResourceManager struct and function declarations.
 * 
 * @copyright Copyright (c) 2026
 */
#pragma once

#include "raylib/raylib.h"

typedef struct ResourceManager {

    Model playerModel;      // the player's 3D model (character-k.glb)
    Model pickaxeModel;     // the pickaxe held by the player

    Texture blockTypeAtlas; // 4x4 tile atlas for block faces (textured strategy)
    Texture skyPanorama;    // sky panorama for skybox

    Shader skyboxShader;         // skybox shader from raylib offical example
    Shader skyboxCubemapShader;  // skybox cubemap shader from raylib offical example

} ResourceManager;

/**
 * @brief Global ResourceManager instance.
 */
extern ResourceManager rm;

/**
 * @brief Load global game resources, linking them in the global instance of
 * ResourceManager called rm.
 */
void loadResourcesResourceManager( void );

/**
 * @brief Unload global game resources.
 */
void unloadResourcesResourceManager( void );