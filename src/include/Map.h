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
typedef struct Chunk Chunk;
typedef struct RayHit RayHit;

/**
 * @brief A 3D grid of blocks representing the world's terrain.
 *
 * Blocks are stored in a single linear array (blocks) and addressed by grid
 * coordinates (la, i, j) through:
 *
 *     p = la * (rows * cols) + i * cols + j
 *
 * Axis mapping: j (cols) -> X, la (layers) -> Y (height), i (rows) -> Z.
 */
struct Map {

    Vector3 pos;            // world-space position of grid origin (block 0,0,0).

    int layers;             // grid size along Y (vertical layers).
    int rows;               // grid size along Z.
    int cols;               // grid size along X.

    int blockSize;          // edge length of a cubic block, in world units.
    Block *blocks;          // flat array of layers*rows*cols blocks.

    Mesh mesh;              // batched geometry of all visible faces (GPU).
    Material material;      // material used to draw the mesh.

    Chunk *chunks;          // array os chunks (NULL if unused)
    int chunkCountRows;     // number of chunks in Z
    int chunkCountCols;     // number of chunks in X
    int chunkCount;         // total chunks (rows * cols)

    /**
     * @brief Strategy used to render the map.
     *
     * Set in createMap() to one of the rendering strategies. Call as
     * map->draw( map, camera ).
     */
    void (*draw)( Map *map, Camera3D *camera );

};

/**
 * @brief A rectangular slice of the World (full height, 
 * CHUNK_SIZE x CHUNK_SIZE in X/Z) with is own baked mesh.
 * 
 * Chunks do not own data; they only group faces into separate meshes so a
 * single block edit can rebuild just one slice instead of the whole world. 
 */
struct Chunk {
    int i0;
    int j0;
    int rows;
    int cols;
    Mesh mesh;
};

/**
 * @brief Result of a voxel raycast (see mapRaycast).
 *
 * (la, i, j) is the first solid block the ray hit; (pla, pi, pj) is the empty
 * cell just before it, across the hit face — where a new block would be placed.
 */
struct RayHit {
    bool hit;    // true if a solid block was hit within range, false otherwise
    int la;      // hit block: layer (Y)
    int i;       // hit block: row (Z)
    int j;       // hit block: column (X)
    int pla;     // empty cell across the hit face: layer (Y) — where to place
    int pi;      // empty cell across the hit face: row (Z)
    int pj;      // empty cell across the hit face: column (X)
};

/**
 * @brief Creates a map, generates its terrain and builds its render mesh.
 * @see createMap implementation in Map.c for parameter details.
 */
Map *createMap( int x, int y, int z, int layers, int rows, int cols, int blockSize );

/**
 * @brief Frees a map and all resources it owns (GPU mesh and block array).
 */
void destroyMap( Map *map );

/**
 * @brief Breaks (removes) the block at the grid coordinate (la, i, j), turning it
 *        into air, and update the rendered geometry incrementally. Returns the material
 *        it yielded (0 if there was nothing to break).
 */
int breakBlock( Map *map, int la, int i, int j );

/**
 * @brief Returns true if an axis-aligned box (centered at 'center', with the
 *        given 'size') overlaps any solid block. Used for player collision.
 */
bool mapBoxCollides( Map *map, Vector3 center, Vector3 size );

/**
 * @brief Marches 'ray' through the voxel grid (DDA) and returns the first solid
 *        block hit within 'maxDistance' (RayHit.hit == 0 if none).
 */
RayHit mapRaycast( Map *map, Ray ray, float maxDistance );
