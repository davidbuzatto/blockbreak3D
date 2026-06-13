/**
 * @file Map.h
 * @author Prof. Dr. David Buzatto
 * @brief Map interface: a 3D grid of blocks (voxel terrain) with procedural
 *        generation and a swappable rendering strategy.
 *
 * @copyright Copyright (c) 2026
 */
#pragma once

#include "raylib/raylib.h"
#include "raylib/raymath.h"

#include "Block.h"

typedef struct Map Map;

/**
 * @brief A 3D grid of blocks representing the world's terrain.
 *
 * Blocks are stored in a single linear array (blocks) and addressed by grid
 * coordinates (la, i, j) through:
 *
 *     p = la * (rows * columns) + i * columns + j
 *
 * Axis mapping: j (columns) -> X, la (layers) -> Y (height), i (rows) -> Z.
 */
struct Map {

    Vector3 pos;            // World-space position of grid origin (block 0,0,0).

    int layers;             // Grid size along Y (vertical layers).
    int rows;               // Grid size along Z.
    int columns;            // Grid size along X.

    int blockSize;          // Edge length of a cubic block, in world units.
    Block *blocks;          // Flat array of layers*rows*columns blocks.

    Mesh mesh;              // Batched geometry of all visible faces (GPU).
    Material material;      // Material used to draw the mesh.

    /**
     * @brief Strategy used to render the map.
     *
     * Set in createMap() to one of the rendering strategies. Call as
     * map->draw( map, camera ).
     */
    void (*draw)( Map *map, Camera3D *camera );

};

/**
 * @brief Creates a map, generates its terrain and builds its render mesh.
 * @see createMap implementation in Map.c for parameter details.
 */
Map *createMap( int x, int y, int z, int layers, int rows, int columns, int blockSize );

/**
 * @brief Frees a map and all resources it owns (GPU mesh and block array).
 */
void destroyMap( Map *map );
