/**
 * @file Map.c
 * @author Prof. Dr. David Buzatto
 * @brief Map implementation: procedural voxel terrain generation and
 *        optimized rendering through a single batched mesh.
 *
 * Overview of how the map works:
 *
 *  - The world is a 3D grid of blocks stored in a single linear array
 *    (map->blocks). A block at grid coordinate (la, i, j) lives at index:
 *
 *        p = la * (rows * columns) + i * columns + j
 *
 *  - Axis mapping (how grid coordinates relate to world space):
 *        j  (columns) -> X axis
 *        la (layers)  -> Y axis (height / vertical)
 *        i  (rows)    -> Z axis
 *
 *  - Terrain is generated as a 2D height map: for each column (i, j) we sample
 *    Perlin/fBm noise to get a surface height "h"; blocks above the surface are
 *    flagged as "broken" (air) and never rendered. See fillMap().
 *
 *  - Rendering does NOT draw one cube per block (that would be tens of thousands
 *    of draw calls). Instead, buildMesh() bakes every VISIBLE face into a single
 *    static Mesh that is uploaded to the GPU once and drawn with a single
 *    DrawMesh() call. A face is "visible" only when the neighbor block on that
 *    side is air (face culling), so buried blocks cost nothing.
 *
 * @copyright Copyright (c) 2026
 */
#include <stdlib.h>
#include <stdbool.h>

#include "raylib/raylib.h"
#include "stb/stb_perlin.h"

#include "Block.h"
#include "Macros.h"
#include "Map.h"
#include "ResourceManager.h"

/**
 * @brief Static description of one of the six faces of a cube.
 *
 * Used by buildMesh() to generate face geometry in a data-driven way instead
 * of hard-coding the six faces separately.
 *
 * Fields:
 *  - dla, di, dj: offset (in grid coordinates) to the NEIGHBOR block that sits
 *    on the other side of this face. buildMesh() checks that neighbor: if it is
 *    air, the face is exposed and must be drawn; if it is solid, the face is
 *    hidden and skipped (face culling).
 *
 *  - corners: the four corners of the face, expressed in "half-block" units
 *    (each component is -1 or +1) relative to the block CENTER. The order is
 *    counter-clockwise as seen from OUTSIDE the cube, so the resulting triangle
 *    normals point outward. This matters because back-face culling (enabled by
 *    default) discards triangles whose front side faces away from the camera;
 *    a wrong winding order makes a face disappear. Do not reorder the corners.
 *
 *  - shade: a 0..1 multiplier applied to the block color for this face. This
 *    fakes directional lighting cheaply (top faces bright, bottom faces dark,
 *    side faces in between) without any real light source or shader.
 */
typedef struct {
    int dla;
    int di;
    int dj;
    float corners[4][3];
    float shade;
} CubeFace;

/** @brief The six faces of a cube (neighbor offset, corners, shade). */
static const CubeFace cubeFaces[6] = {
    // +X (right): neighbor at j+1
    {  0,  0,  1, { {1,-1,-1}, {1,1,-1}, {1,1,1}, {1,-1,1} }, 0.8f },
    // -X (left): neighbor at j-1
    {  0,  0, -1, { {-1,-1,1}, {-1,1,1}, {-1,1,-1}, {-1,-1,-1} }, 0.65f },
    // +Y (top): neighbor at la+1
    {  1,  0,  0, { {-1,1,-1}, {-1,1,1}, {1,1,1}, {1,1,-1} }, 1.0f },
    // -Y (bottom): neighbor at la-1
    { -1,  0,  0, { {-1,-1,-1}, {1,-1,-1}, {1,-1,1}, {-1,-1,1} }, 0.5f },
    // +Z (front): neighbor at i+1
    {  0,  1,  0, { {-1,-1,1}, {1,-1,1}, {1,1,1}, {-1,1,1} }, 0.75f },
    // -Z (back): neighbor at i-1
    {  0, -1,  0, { {-1,-1,-1}, {-1,1,-1}, {1,1,-1}, {1,-1,-1} }, 0.6f },
};

/* forward declarations of file-private (static) helpers. */
static void fillMap( Map *map, float scale, float seed );
static void buildMesh( Map *map );
static bool isSolid( Map *map, int la, int i, int j );
static bool isBlockHidden( Map *map, int la, int i, int j );
static void drawBlock( Block *block );

/* three interchangeable rendering strategies; pick one in createMap(). */
static void drawNaive( Map *map, Camera3D *camera );
static void drawCulled( Map *map, Camera3D *camera );
static void drawMesh( Map *map, Camera3D *camera );

/**
 * @brief Creates a dynamically allocated Map, generates its terrain and builds
 *        its render mesh.
 *
 * @param x,y,z       World-space position of the map origin (block 0,0,0).
 * @param layers      Number of vertical layers (grid size along Y).
 * @param rows        Number of rows (grid size along Z).
 * @param columns     Number of columns (grid size along X).
 * @param blockSize   Edge length of a single cubic block, in world units.
 * @return Map*       Pointer to the newly created map.
 */
Map *createMap( int x, int y, int z, int layers, int rows, int columns, int blockSize ) {

    Map *new = (Map*) malloc( sizeof( Map ) );

    new->pos.x = x;
    new->pos.y = y;
    new->pos.z = z;

    new->layers = layers;
    new->rows = rows;
    new->columns = columns;

    new->blockSize = blockSize;

    // one contiguous array holding every block of the grid.
    new->blocks = (Block*) malloc( sizeof( Block ) * new->layers * new->rows * new->columns );

    // generate the terrain (fills the block array).
    fillMap( new, 0.05f, 0 );
    //fillMap( new, 0.1f, GetRandomValue( 0, 10000 ) );

    // bake the terrain into a mesh. Only needed by drawMesh; if you switch to a
    // cube-by-cube strategy below you may comment this out to skip the cost.
    buildMesh( new );

    // choose a rendering strategy by uncommenting exactly one. All three produce
    // the same image but with very different performance (good for comparison):
    //new->draw = drawNaive;    // (1) one cube per block, no culling   (slowest)
    //new->draw = drawCulled;   // (2) one cube per block, skip hidden  (faster)
    new->draw = drawMesh;       // (3) single batched mesh              (fastest)

    return new;

}

/**
 * @brief Frees a Map and all resources it owns (GPU mesh and block array).
 *
 * @param map The map to destroy. Safe to call with NULL.
 */
void destroyMap( Map *map ) {
    if ( map != NULL ) {
        UnloadMesh( map->mesh );   // releases the GPU buffers and CPU vertex data
        free( map->blocks );
        free( map );
    }
}

/**
 * @brief Procedurally generates the terrain, filling map->blocks.
 *
 * The terrain is a 2D height map: for each column (i, j) we sample fractal
 * (fBm) Perlin noise to obtain a surface height "h". Then, for every layer la:
 *   - la <= h  -> solid block (visible terrain)
 *   - la >  h  -> "broken" block (air, never drawn)
 *
 * Block color is chosen by depth below the surface (grass / dirt / stone).
 *
 * @param map    The map to fill.
 * @param scale  Horizontal noise frequency. Smaller = smoother, wider hills;
 *               larger = more jagged terrain.
 * @param seed   Offset added to the sample coordinates to vary the terrain.
 */
static void fillMap( Map *map, float scale, float seed ) {

    /*
     * Terrain vertical placement, independent of the total number of layers:
     *   baseHeight = average ground level ("sea level")
     *   amplitude  = how far the surface rises/falls around baseHeight
     * The surface height therefore stays within [baseHeight - amplitude,
     *   baseHeight + amplitude]. Old behavior (relief used the whole map height):
     */
    /*int baseHeight = map->layers / 2;
    int amplitude = map->layers / 2;*/
    int baseHeight = 10;
    int amplitude = 10;

    for ( int i = 0; i < map->rows; i++ ) {
        for ( int j = 0; j < map->columns; j++ ) {

            // sample the noise on the horizontal plane only (depends on i, j).
            float nx = ( j + seed ) * scale;
            float ny = ( i + seed ) * scale;
            //float n = stb_perlin_noise3( nx, ny, 0.0f, 0, 0, 0 );
            // fBm noise (~[-1, 1]); the last three args are lacunarity, gain, octaves.
            float n = stb_perlin_fbm_noise3( nx, ny, 0.0f, 2.0f, 0.5f, 6 );

            // map noise to a surface height, clamped to valid layer indices.
            int h = baseHeight + (int) ( n * amplitude );
            if ( h < 0 ) {
                h = 0;
            } else if ( h >= map->layers ) {
                h = map->layers - 1;
            }

            // fill the whole vertical column for this (i, j).
            for ( int la = 0; la < map->layers; la++ ) {

                Color color = ORANGE;
                int hitsToBreak = 1;
                int materialsToAquire = 0;

                // everything above the surface is air.
                bool broken = la > h;

                // color by depth: surface = grass, just below = dirt, deep = stone.
                if ( la == h ) {
                    color = GREEN;
                } else if ( la >= h - 2 ) {
                    color = BROWN;
                } else {
                    color = GRAY;
                }

                // linear index of this block in the flat array.
                int p = la * ( map->rows * map->columns ) + i * map->columns + j;
                map->blocks[p] = (Block) {
                    .pos = {
                        map->pos.x + map->blockSize * j,
                        map->pos.y + map->blockSize * la,
                        map->pos.z + map->blockSize * i
                    },
                    .dim = {
                        map->blockSize,
                        map->blockSize,
                        map->blockSize
                    },
                    .color = color,
                    .hitsToBreak = hitsToBreak,
                    .hits = 0,
                    .materialsToAquire = materialsToAquire,
                    .broken = broken
                };

            }

        }
    }

}

/**
 * @brief Bakes all visible block faces into a single static mesh and uploads
 *        it to the GPU.
 *
 * Only faces that touch air are emitted (face culling), so the cost is
 * proportional to the terrain's surface area, not its volume. Each emitted face
 * is a quad made of two triangles (6 vertices). Every vertex also stores a
 * color (block color * face shade) so the relief reads as 3D without any real
 * lighting.
 *
 * Two passes are used:
 *   1. Count how many faces will be visible (to know how much memory to alloc).
 *   2. Allocate the vertex/color arrays and fill them.
 *
 * @param map The map whose mesh is built. Stores the result in map->mesh and
 *            a default (white) material in map->material.
 */
static void buildMesh( Map *map ) {

    float hs = map->blockSize / 2.0f; // half edge: corners are at center +/- hs

    // pass 1: count the visible faces
    int faceCount = 0;
    for ( int la = 0; la < map->layers; la++ ) {
        for ( int i = 0; i < map->rows; i++ ) {
            for ( int j = 0; j < map->columns; j++ ) {
                if ( !isSolid( map, la, i, j ) ) {
                    continue;                       // empty cell emits no faces
                }
                for ( int f = 0; f < 6; f++ ) {
                    const CubeFace *face = &cubeFaces[f];
                    // if the neighbor on this side is air, the face is visible.
                    if ( !isSolid( map, la + face->dla, i + face->di, j + face->dj ) ) {
                        faceCount++;
                    }
                }
            }
        }
    }

    // Pass 2: allocate and fill the vertex data
    int vertexCount = faceCount * 6;                // 6 vertices per face (2 triangles)

    Mesh mesh = { 0 };                              // zero every field first
    mesh.vertexCount = vertexCount;
    mesh.triangleCount = faceCount * 2;
    // 3 floats (x, y, z) per vertex; MemAlloc is raylib's allocator.
    mesh.vertices = (float*) MemAlloc( vertexCount * 3 * sizeof( float ) );
    // 4 bytes (r, g, b, a) per vertex.
    mesh.colors = (unsigned char*) MemAlloc( vertexCount * 4 * sizeof( unsigned char ) );

    int v = 0;                                      // write cursor into vertices[]
    int ci = 0;                                     // write cursor into colors[]
    // corner indices that turn the 4-corner quad into two triangles.
    static const int order[6] = { 0, 1, 2, 0, 2, 3 };

    for ( int la = 0; la < map->layers; la++ ) {
        for ( int i = 0; i < map->rows; i++ ) {
            for ( int j = 0; j < map->columns; j++ ) {

                if ( !isSolid( map, la, i, j ) ) {
                    continue;
                }

                int p = la * ( map->rows * map->columns ) + i * map->columns + j;
                Vector3 center = map->blocks[p].pos;
                Color color = map->blocks[p].color;

                for ( int f = 0; f < 6; f++ ) {

                    const CubeFace *face = &cubeFaces[f];

                    // skip this face if its neighbor is solid (hidden face).
                    if ( isSolid( map, la + face->dla, i + face->di, j + face->dj ) ) {
                        continue;
                    }

                    // emit the 6 vertices (2 triangles) of this face.
                    for ( int k = 0; k < 6; k++ ) {

                        const float *c = face->corners[order[k]];

                        // position = block center + corner offset scaled by half edge.
                        mesh.vertices[v++] = center.x + c[0] * hs;
                        mesh.vertices[v++] = center.y + c[1] * hs;
                        mesh.vertices[v++] = center.z + c[2] * hs;

                        // color = block color darkened by this face's shade factor.
                        mesh.colors[ci++] = (unsigned char) ( color.r * face->shade );
                        mesh.colors[ci++] = (unsigned char) ( color.g * face->shade );
                        mesh.colors[ci++] = (unsigned char) ( color.b * face->shade );
                        mesh.colors[ci++] = 255;     // fully opaque

                    }

                }

            }
        }
    }

    // upload to the GPU once (false = static; we won't update it every frame).
    UploadMesh( &mesh, false );
    map->mesh = mesh;

    // default material, tinted white so the per-vertex colors show unchanged
    // (final color = material color * vertex color; white * c == c).
    map->material = LoadMaterialDefault();
    map->material.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;

}

/**
 * @brief Tests whether the grid cell (la, i, j) contains a solid (opaque) block.
 *
 * Out-of-bounds coordinates count as AIR (return false) so that faces on the
 * edges of the world are still generated.
 *
 * @param map         The map.
 * @param la, i, j    Grid coordinates to test.
 * @return true if the cell holds a solid block, false if it is air or outside.
 */
static bool isSolid( Map *map, int la, int i, int j ) {

    // out of bounds -> treat as air.
    if ( la < 0 || la >= map->layers  ||
          i < 0 ||  i >= map->rows    ||
          j < 0 ||  j >= map->columns ) {
        return false;
    }

    int p = la * ( map->rows * map->columns ) + i * map->columns + j;

    // solid means NOT broken.
    return !map->blocks[p].broken;

}

/**
 * @brief Tests whether a block is completely surrounded by solid neighbors.
 *
 * If all six neighbors are solid, none of the block's faces touch air, so the
 * block is invisible and can be skipped. This is the block-level form of face
 * culling used by drawCulled().
 *
 * @param map         The map.
 * @param la, i, j    Grid coordinates of the block.
 * @return true if every one of the six neighbors is solid.
 */
static bool isBlockHidden( Map *map, int la, int i, int j ) {
    return isSolid( map, la + 1, i, j ) &&   // up     (+Y)
           isSolid( map, la - 1, i, j ) &&   // down   (-Y)
           isSolid( map, la, i + 1, j ) &&   // front  (+Z)
           isSolid( map, la, i - 1, j ) &&   // back   (-Z)
           isSolid( map, la, i, j + 1 ) &&   // right  (+X)
           isSolid( map, la, i, j - 1 );     // left   (-X)
}

/**
 * @brief Draws a single block as a solid cube with a black wireframe outline.
 *
 * Used by the cube-by-cube strategies (drawNaive / drawCulled).
 *
 * @param block The block to draw.
 */
static void drawBlock( Block *block ) {
    DrawCubeV( block->pos, block->dim, block->color );
    DrawCubeWiresV( block->pos, block->dim, BLACK );
}

/**
 * @brief Rendering strategy 1: draw every solid block as its own cube, with no
 *        culling at all.
 *
 * The simplest approach and the slowest: it issues draw calls even for blocks
 * buried deep inside the terrain that nobody can see. Kept for teaching and
 * benchmark comparison.
 *
 * @param map     The map to draw.
 * @param camera  Active camera (unused).
 */
static void drawNaive( Map *map, Camera3D *camera ) {

    for ( int la = 0; la < map->layers; la++ ) {
        for ( int i = 0; i < map->rows; i++ ) {
            for ( int j = 0; j < map->columns; j++ ) {

                int p = la * ( map->rows * map->columns ) + i * map->columns + j;
                Block *block = &map->blocks[p];

                if ( block->broken ) {
                    continue;                 // air: nothing to draw
                }

                drawBlock( block );

            }
        }
    }

}

/**
 * @brief Rendering strategy 2: draw blocks cube-by-cube, but skip blocks that
 *        are fully surrounded (block-level face culling).
 *
 * Much faster than drawNaive because the whole solid interior is skipped; only
 * the visible "crust" is drawn. Still one draw call per surface block, so
 * slower than the batched mesh. Kept for comparison.
 *
 * @param map     The map to draw.
 * @param camera  Active camera (unused).
 */
static void drawCulled( Map *map, Camera3D *camera ) {

    for ( int la = 0; la < map->layers; la++ ) {
        for ( int i = 0; i < map->rows; i++ ) {
            for ( int j = 0; j < map->columns; j++ ) {

                int p = la * ( map->rows * map->columns ) + i * map->columns + j;
                Block *block = &map->blocks[p];

                if ( block->broken ) {
                    continue;                 // air
                }

                if ( isBlockHidden( map, la, i, j ) ) {
                    continue;                 // fully buried: invisible
                }

                drawBlock( block );

            }
        }
    }

}

/**
 * @brief Rendering strategy 3: draw the whole map with a single draw call using
 *        the pre-built batched mesh.
 *
 * The fastest approach: all visible geometry was baked once in buildMesh(), so
 * here we just hand the mesh to the GPU.
 *
 * @param map     The map to draw.
 * @param camera  Active camera (unused for now; kept for future culling).
 */
static void drawMesh( Map *map, Camera3D *camera ) {
    // MatrixIdentity() means "draw the mesh where it already is" (no transform).
    DrawMesh( map->mesh, map->material, MatrixIdentity() );
}
