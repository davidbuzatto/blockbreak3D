/**
 * @file Map.c
 * @author Prof. Dr. David Buzatto
 * @brief Map implementation: procedural voxel terrain generation and
 *        optimized rendering (face culling + batched meshes / chunks).
 *
 * Overview of how the map works:
 *
 *  - The world is a 3D grid of blocks stored in a single linear array
 *    (map->blocks). A block at grid coordinate (la, i, j) lives at index:
 *
 *        p = la * (rows * cols) + i * cols + j
 *
 *  - Axis mapping (how grid coordinates relate to world space):
 *        j  (cols) -> X axis
 *        la (layers)  -> Y axis (height / vertical)
 *        i  (rows)    -> Z axis
 *
 *  - Terrain is generated as a 2D height map: for each column (i, j) we sample
 *    Perlin/fBm noise to get a surface height "h"; blocks above the surface are
 *    flagged as "broken" (air) and never rendered. See fillMap().
 *
 *  - Rendering does NOT draw one cube per block (that would be tens of thousands
 *    of draw calls). Instead, the visible faces are baked into static meshes that
 *    are uploaded to the GPU once and drawn with DrawMesh(). A face is "visible"
 *    only when the neighbor block on that side is air (face culling), so buried
 *    blocks cost nothing. The world can be baked either as one big mesh
 *    (buildMesh / drawMesh) or split into per-chunk meshes (buildChunks /
 *    drawChunked) so a single edit later rebuilds just one chunk.
 *
 * @copyright Copyright (c) 2026
 */
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "raylib/raylib.h"
#include "stb/stb_perlin.h"

#include "Block.h"
#include "Macros.h"
#include "Map.h"
#include "ResourceManager.h"

#define CHUNK_SIZE 16

/**
 * @brief Defines the types of build strategies that can be used to create 
 * and process the map.
 */
typedef enum {
    MAP_STRATEGY_NAIVE,           // block by block
    MAP_STRATEGY_CULLED,          // block by block with face culling
    MAP_STRATEGY_MESH,            // one mesh
    MAP_STRATEGY_CHUNKED,          // mesh chunks
    MAP_STRATEGY_CHUNKED_FRUSTUM  // mesh chunks + frustum culling
} MapStrategy;

/** @brief Selected map build + render strategy (change here to switch). */
static const MapStrategy mapStrategy = MAP_STRATEGY_CHUNKED_FRUSTUM;

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

/**
 * @brief The camera's view volume as 6 planes (left, right, bottom, top, near,
 *        far). Each plane is a Vector4: (x,y,z) is the normal (a,b,c) and w is
 *        d. A point is inside a plane when a*x + b*y + c*z + d >= 0.
 */
typedef struct {
    Vector4 planes[6];
} Frustum;

typedef struct {
    float scale;
    float threashold;
    float seed;
    Color color;
    int materialsToAquire;
} OreType;

/* forward declarations of file-private (static) helpers. */
static void fillMap( Map *map, float scale, float seed, OreType *oreTypes, int oreTypeCount );
static Mesh buildMeshRange( Map *map, int i0, int j0, int rows, int cols );
static void buildMesh( Map *map );
static void buildChunks( Map *map );
static bool isSolid( Map *map, int la, int i, int j );
static bool isBlockHidden( Map *map, int la, int i, int j );
static int chunkIndexAt( Map *map, int i, int j );
static void rebuildChunk( Map *map, int chunkIndex );
static Frustum extractFrustum( Camera3D *camera );
static bool boxInFrustum( Frustum *f, Vector3 min, Vector3 max );
static void chunkBounds( Map *map, Chunk *ch, Vector3 *min, Vector3 *max );
static void refreshGeometryAfterEdit( Map *map, int i, int j );

/* five interchangeable rendering strategies; pick one in createMap(). */
static void drawNaive( Map *map, Camera3D *camera );
static void drawCulled( Map *map, Camera3D *camera );
static void drawMesh( Map *map, Camera3D *camera );
static void drawChunked( Map *map, Camera3D *camera );
static void drawChunkedFrustum( Map *map, Camera3D *camera );

/* helper shared by the cube-by-cube strategies (drawNaive / drawCulled). */
static void drawBlock( Block *block );

/**
 * @brief Creates a dynamically allocated Map, generates its terrain and builds
 *        its render mesh.
 *
 * @param x,y,z       World-space position of the map origin (block 0,0,0).
 * @param layers      Number of vertical layers (grid size along Y).
 * @param rows        Number of rows (grid size along Z).
 * @param cols     Number of columns (grid size along X).
 * @param blockSize   Edge length of a single cubic block, in world units.
 * @return Map*       Pointer to the newly created map.
 */
Map *createMap( int x, int y, int z, int layers, int rows, int cols, int blockSize ) {

    Map *new = (Map*) malloc( sizeof( Map ) );

    new->pos.x = x;
    new->pos.y = y;
    new->pos.z = z;

    new->layers = layers;
    new->rows = rows;
    new->cols = cols;

    new->blockSize = blockSize;

    // one contiguous array holding every block of the grid.
    new->blocks = (Block*) malloc( sizeof( Block ) * new->layers * new->rows * new->cols );

    // initialize render resources up front so destroyMap is safe no matter
    // which build steps below are enabled/disabled.
    new->mesh = (Mesh) { 0 };
    new->chunks = NULL;
    new->chunkCount = 0;

    OreType oreTypes[] = {
        { 0.14f, 0.45f, 900.0f, BLUE, 15 },    // gem (rarest, most valuable)
        { 0.12f, 0.38f, 500.0f, GOLD, 8 },     // gold
        { 0.10f, 0.30f, 200.0f, ORANGE, 3 },   // iron (most common)
    };
    int oreTypeCount = sizeof( oreTypes ) / sizeof( OreType );

    // generate the terrain (fills the block array).
    fillMap( new, 0.05f, 0, oreTypes, oreTypeCount );
    //fillMap( new, 0.1f, GetRandomValue( 0, 10000 ) );

    // material is shared by every mesh-based strategy
    new->material = LoadMaterialDefault();
    new->material.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;

    // build geometry only for the selected strategy
    // (naive/culled draw from the block array directly and need no prebuild).
    if ( mapStrategy == MAP_STRATEGY_MESH ) {
        buildMesh( new );      // needed by drawMesh    (one mesh for the whole world)
    } else if ( mapStrategy == MAP_STRATEGY_CHUNKED || mapStrategy == MAP_STRATEGY_CHUNKED_FRUSTUM ) {
        buildChunks( new );    // needed by drawChunked and drawChunkedFrustum (one mesh per chunk)
    }

    // select the draw function matching mapStrategy. All five produce the same
    // image but with very different performance (good for comparison):
    switch ( mapStrategy ) {
        case MAP_STRATEGY_NAIVE:
            new->draw = drawNaive;             // (1) one cube per block, no culling   (slowest)
            break;
        case MAP_STRATEGY_CULLED:
            new->draw = drawCulled;            // (2) one cube per block, skip hidden  (faster)
            break;
        case MAP_STRATEGY_MESH:
            new->draw = drawMesh;              // (3) single batched mesh              (fastest)
            break;
        case MAP_STRATEGY_CHUNKED:
            new->draw = drawChunked;           // (4) one mesh per chunk
            break;
        case MAP_STRATEGY_CHUNKED_FRUSTUM:
            new->draw = drawChunkedFrustum;    // (5) one mesh per chunk with frustum culling
            break;
        default:
            new->draw = drawChunkedFrustum;    // fallback for an unhandled strategy
            break;
    }    

    return new;

}

/**
 * @brief Frees a Map and all resources it owns (GPU mesh and block array).
 *
 * @param map The map to destroy. Safe to call with NULL.
 */
void destroyMap( Map *map ) {

    if ( map != NULL ) {

        UnloadMesh( map->mesh );   // safe even if it was never built (zeroed)

        if ( map->chunks != NULL ) {
            for ( int c = 0; c < map->chunkCount; c++ ) {
                UnloadMesh( map->chunks[c].mesh );
            }
            free( map->chunks );
        }

        free( map->blocks );
        free( map );
        
    }

}

/**
 * @brief Breaks (removes) the block at grid coordinate (la, i, j), turning it
 *        into air, and updates the rendered geometry incrementally.
 *
 * Cube-by-cube strategies read the block array live (nothing to rebuild); the
 * single mesh must be fully rebuilt; the chunked strategy rebuilds only the
 * affected chunk(s).
 */
int breakBlock( Map *map, int la, int i, int j ) {

    // ignore out-of-bounds coordinates or cells that are already air.
    if ( !isSolid( map, la, i, j ) ) {
        return 0;
    }

    int p = la * ( map->rows * map->cols ) + i * map->cols + j;

    int materialsToAquire = map->blocks[p].materialsToAquire;

    // 1) change the data: the block becomes air.
    map->blocks[p].broken = true;

    // 2) update the geometry for the active strategy.
    refreshGeometryAfterEdit( map, i, j );

    return materialsToAquire;

}

/**
 * @brief Places a solid block of 'color' at the (empty) cell (la, i, j) and
 *        refreshes the geometry. Returns false if the cell is out of bounds or
 *        already solid, so the caller only spends material on a real placement.
 *        (pos/dim were already set for every cell by fillMap.)
 */
bool placeBlock( Map *map, int la, int i, int j, Color color ) {

    // is a valid cell (bound checking)...
    if ( la < 0 || la >= map->layers ||
         i  < 0 || i  >= map->rows ||
         j  < 0 || j  >= map->cols ) {
        return false;
    }

    int p = la * ( map->rows * map->cols ) + i * map->cols + j;

    // ... and is currently air.
    if ( !map->blocks[p].broken ) {
        return false;
    }

    // turn it solid.
    map->blocks[p].broken = false;
    map->blocks[p].color = color;
    map->blocks[p].hits = 0;
    map->blocks[p].hitsToBreak = 1;
    map->blocks[p].materialsToAquire = 1;

    refreshGeometryAfterEdit( map, i, j );
    return true;

}

/**
 * @brief Returns true if an axis-aligned box overlaps any solid block.
 *
 * Converts the box (center +/- half size) into the range of grid cells it can
 * touch and asks isSolid() about each one. isSolid treats out-of-bounds cells
 * as air, so no clamping is needed. Used for player collision.
 *
 * @param map     The map.
 * @param center  World-space center of the box.
 * @param size    Full extents of the box (width, height, depth).
 * @return true if any overlapped cell holds a solid block.
 */
bool mapBoxCollides( Map *map, Vector3 center, Vector3 size ) {

    float hx = size.x * 0.5f;
    float hy = size.y * 0.5f;
    float hz = size.z * 0.5f;

    // world-space AABB of the box.
    float minX = center.x - hx;
    float maxX = center.x + hx;
    float minY = center.y - hy;
    float maxY = center.y + hy;
    float minZ = center.z - hz;
    float maxZ = center.z + hz;

    // range of grid cells the box spans. The cell index of a world coordinate c
    // is round((c - origin)/blockSize), and round(x) == floor(x + 0.5).
    int jmin  = (int) floorf( ( minX - map->pos.x ) / map->blockSize + 0.5f );
    int jmax  = (int) floorf( ( maxX - map->pos.x ) / map->blockSize + 0.5f );
    int lamin = (int) floorf( ( minY - map->pos.y ) / map->blockSize + 0.5f );
    int lamax = (int) floorf( ( maxY - map->pos.y ) / map->blockSize + 0.5f );
    int imin  = (int) floorf( ( minZ - map->pos.z ) / map->blockSize + 0.5f );
    int imax  = (int) floorf( ( maxZ - map->pos.z ) / map->blockSize + 0.5f );

    // any solid cell in that range means a collision.
    for ( int la = lamin; la <= lamax; la++ ) {
        for ( int i = imin; i <= imax; i++ ) {
            for ( int j = jmin; j <= jmax; j++ ) {
                if ( isSolid( map, la, i, j ) ) {
                    return true;
                }
            }
        }
    }

    return false;

}

/**
 * @brief Marches a ray through the voxel grid (Amanatides & Woo DDA) and returns
 *        the first solid block hit within maxDistance.
 *
 * Walks cell to cell, each step crossing whichever axis boundary (X/Y/Z) is
 * nearest along the ray. It also remembers the previous cell, so the result
 * carries the empty neighbor across the hit face (used later to place blocks).
 *
 * @param map          The map.
 * @param ray          World-space ray (origin + direction).
 * @param maxDistance  How far (world units) to march before giving up.
 * @return A RayHit (.hit == false when nothing was hit).
 */
RayHit mapRaycast( Map *map, Ray ray, float maxDistance ) {

    RayHit result = { 0 };   // .hit == false (no hit) by default

    Vector3 d = Vector3Normalize( ray.direction );

    // ray origin in "shifted" grid coords: block centers sit on integers + 0.5,
    // so a cell spans [c, c+1] here and floor() yields its index.
    float gx = ( ray.position.x - map->pos.x ) / map->blockSize + 0.5f;
    float gy = ( ray.position.y - map->pos.y ) / map->blockSize + 0.5f;
    float gz = ( ray.position.z - map->pos.z ) / map->blockSize + 0.5f;

    // starting cell (X=j, Y=la, Z=i)
    int j  = (int) floorf( gx );
    int la = (int) floorf( gy );
    int i  = (int) floorf( gz );

    // step direction on each axis (+1 / -1)
    int stepX = d.x >= 0.0f ? 1 : -1;
    int stepY = d.y >= 0.0f ? 1 : -1;
    int stepZ = d.z >= 0.0f ? 1 : -1;

    // world distance to cross one full cell on each axis (INF if not moving on it)
    float tDeltaX = d.x != 0.0f ? map->blockSize / fabsf( d.x ) : INFINITY;
    float tDeltaY = d.y != 0.0f ? map->blockSize / fabsf( d.y ) : INFINITY;
    float tDeltaZ = d.z != 0.0f ? map->blockSize / fabsf( d.z ) : INFINITY;

    // world distance from the origin to the first cell boundary on each axis
    float tMaxX = INFINITY;
    float tMaxY = INFINITY;
    float tMaxZ = INFINITY;

    if ( d.x != 0.0f ) {
        if ( d.x > 0 ) {
            tMaxX = j + 1 - gx;
        } else {
            tMaxX = gx - j;
        }
        tMaxX *= tDeltaX;
    }

    if ( d.y != 0.0f ) {
        if ( d.y > 0 ) {
            tMaxY = la + 1 - gy;
        } else {
            tMaxY = gy - la;
        }
        tMaxY *= tDeltaY;
    }

    if ( d.z != 0.0f ) {
        if ( d.z > 0 ) {
            tMaxZ = i + 1 - gz;
        } else {
            tMaxZ = gz - i;
        }
        tMaxZ *= tDeltaZ;
    }

    // the cell we came FROM (the empty neighbor where a placed block would go)
    int pj = j;
    int pla = la;
    int pi = i;

    float t = 0.0f;

    while ( t <= maxDistance ) {

        // first solid block along the ray? report it (plus the previous cell).
        if ( isSolid( map, la, i, j ) ) {
            result.hit = true;
            result.la  = la;
            result.i   = i;
            result.j   = j;
            result.pla = pla;
            result.pi  = pi;
            result.pj  = pj;
            return result;
        }

        // remember this cell, then advance across the nearest axis boundary.
        pj = j;
        pla = la;
        pi = i;

        if ( tMaxX < tMaxY && tMaxX < tMaxZ ) {
            j += stepX;
            t = tMaxX;
            tMaxX += tDeltaX;
        } else if ( tMaxY < tMaxZ ) {
            la += stepY;
            t = tMaxY;
            tMaxY += tDeltaY;
        } else {
            i += stepZ;
            t = tMaxZ;
            tMaxZ += tDeltaZ;
        }

    }

    return result;

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
static void fillMap( Map *map, float scale, float seed, OreType *oreTypes, int oreTypeCount ) {

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
        for ( int j = 0; j < map->cols; j++ ) {

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
                int materialsToAquire = 1;

                // everything above the surface is air.
                bool broken = la > h;

                // color by depth: surface = grass, just below = dirt, deep = stone.
                if ( la == h ) {
                    color = GREEN;
                } else if ( la >= h - 2 ) {
                    color = BROWN;
                } else {

                    color = GRAY;

                    for ( int o = 0; o < oreTypeCount; o++ ) {
                        
                        OreType ore = oreTypes[o];

                        float oreNoise = stb_perlin_fbm_noise3(
                            ( j + ore.seed ) * ore.scale,
                            la * ore.scale,
                            ( i + ore.seed ) * ore.scale,
                            2.0f, 0.5f, 4
                        );

                        if ( oreNoise > ore.threashold ) {
                            color = ore.color;
                            materialsToAquire = ore.materialsToAquire;
                            break;
                        }

                    }

                }

                // linear index of this block in the flat array.
                int p = la * ( map->rows * map->cols ) + i * map->cols + j;
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
 * @brief Builds and uploads a mesh with the visible faces of the blocks in a
 *        rectangular region: all layers, rows [i0, i0+rows), cols [j0, j0+cols).
 *
 * Neighbor tests use the GLOBAL block array, so face culling stays correct at
 * the borders between regions (a face on a chunk edge is hidden when the block
 * just across the border, in the next chunk, is solid). buildMesh() uses this
 * for the whole world; buildChunks() calls it once per chunk.
 *
 * @param map           The map.
 * @param i0, j0        First row/column of the region (inclusive).
 * @param rows, cols    How many rows/columns the region spans.
 * @return The uploaded mesh (the caller stores it).
 */
static Mesh buildMeshRange( Map *map, int i0, int j0, int rows, int cols ) {

    float hs = map->blockSize / 2.0f; // half edge: corners are at center +/- hs

    int iEnd = i0 + rows;
    int jEnd = j0 + cols;

    // pass 1: count the visible faces in this region
    int faceCount = 0;
    for ( int la = 0; la < map->layers; la++ ) {
        for ( int i = i0; i < iEnd; i++ ) {
            for ( int j = j0; j < jEnd; j++ ) {
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

    // pass 2: allocate and fill the vertex data
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
        for ( int i = i0; i < iEnd; i++ ) {
            for ( int j = j0; j < jEnd; j++ ) {

                if ( !isSolid( map, la, i, j ) ) {
                    continue;
                }

                int p = la * ( map->rows * map->cols ) + i * map->cols + j;
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
    return mesh;

}

/**
 * @brief Builds the single whole-world mesh used by drawMesh.
 *
 * Convenience wrapper around buildMeshRange(): the whole world is just the
 * region that covers every row and column.
 *
 * @param map The map; the result is stored in map->mesh.
 */
static void buildMesh( Map *map ) {
    map->mesh = buildMeshRange( map, 0, 0, map->rows, map->cols );
}

/**
 * @brief Splits the world into a 2D grid of full-height chunks and builds one
 *        mesh per chunk. Used by drawChunked.
 *
 * Each chunk covers all layers and up to CHUNK_SIZE x CHUNK_SIZE blocks in X/Z.
 * A chunk stores only its grid range and a mesh; the block data stays in the
 * shared map->blocks array.
 *
 * @param map The map; results are stored in map->chunks and the chunk counts.
 */
static void buildChunks( Map *map ) {

    // number of chunks along each axis (round up so edge chunks are included).
    map->chunkCountRows = ( map->rows + CHUNK_SIZE - 1 ) / CHUNK_SIZE;
    map->chunkCountCols = ( map->cols + CHUNK_SIZE - 1 ) / CHUNK_SIZE;
    map->chunkCount = map->chunkCountRows * map->chunkCountCols;

    map->chunks = (Chunk*) MemAlloc( sizeof( Chunk ) * map->chunkCount );

    int c = 0;
    for ( int ci = 0; ci < map->chunkCountRows; ci++ ) {
        for ( int cj = 0; cj < map->chunkCountCols; cj++ ) {

            // top-left block of this chunk in grid coordinates.
            int i0 = ci * CHUNK_SIZE;
            int j0 = cj * CHUNK_SIZE;

            // clamp edge chunks so they don't run past the world bounds.
            int rows = CHUNK_SIZE;
            int cols = CHUNK_SIZE;

            if ( i0 + rows > map->rows ) {
                rows = map->rows - i0;
            }
            if ( j0 + cols > map->cols ) {
                cols = map->cols - j0;
            }

            // store the chunk's range and bake its mesh.
            map->chunks[c++] = (Chunk) {
                .i0 = i0,
                .j0 = j0,
                .rows = rows,
                .cols = cols,
                .mesh = buildMeshRange( map, i0, j0, rows, cols )
            };

        }
    }

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
          j < 0 ||  j >= map->cols ) {
        return false;
    }

    int p = la * ( map->rows * map->cols ) + i * map->cols + j;

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
 * @brief Returns the index (in map->chunks) of the chunk that owns grid
 *        column/row (i, j), or -1 if (i, j) is outside the world.
 */
static int chunkIndexAt( Map *map, int i, int j ) {

    // outside the world: there is no chunk.
    if ( i < 0 || i >= map->rows || j < 0 || j >= map->cols ) {
        return -1;
    }

    int ci = i / CHUNK_SIZE;   // which chunk row
    int cj = j / CHUNK_SIZE;   // which chunk column

    return ci * map->chunkCountCols + cj;

}

/**
 * @brief Rebuilds a single chunk's mesh from the current block data.
 *
 * Frees the old GPU mesh first. Safe to call with -1 (does nothing), so callers
 * can pass the result of chunkIndexAt() without checking it first.
 */
static void rebuildChunk( Map *map, int chunkIndex ) {

    if ( chunkIndex < 0 ) {
        return;   // out-of-bounds neighbor: nothing to rebuild
    }

    Chunk *ch = &map->chunks[chunkIndex];
    UnloadMesh( ch->mesh );   // release the old mesh
    ch->mesh = buildMeshRange( map, ch->i0, ch->j0, ch->rows, ch->cols );

}

/**
 * @brief Builds the 6 frustum planes from the camera's view-projection matrix
 *        (Gribb-Hartmann extraction).
 *
 * The planes are NOT normalized: the box test only needs the sign of the
 * plane equation, and normalizing would not change it.
 */
static Frustum extractFrustum( Camera3D *camera ) {

    // view-projection matrix: world space -> clip space.
    float aspect = (float) GetScreenWidth() / (float) GetScreenHeight();
    Matrix view = GetCameraMatrix( *camera );
    Matrix proj = MatrixPerspective( camera->fovy * DEG2RAD, aspect, 0.01, 1000.0 );
    Matrix m = MatrixMultiply( view, proj );

    Frustum f;

    // each plane = a combination of rows of the clip matrix
    f.planes[0] = (Vector4) { m.m3 + m.m0, m.m7 + m.m4, m.m11 + m.m8,  m.m15 + m.m12 }; // left
    f.planes[1] = (Vector4) { m.m3 - m.m0, m.m7 - m.m4, m.m11 - m.m8,  m.m15 - m.m12 }; // right
    f.planes[2] = (Vector4) { m.m3 + m.m1, m.m7 + m.m5, m.m11 + m.m9,  m.m15 + m.m13 }; // bottom
    f.planes[3] = (Vector4) { m.m3 - m.m1, m.m7 - m.m5, m.m11 - m.m9,  m.m15 - m.m13 }; // top
    f.planes[4] = (Vector4) { m.m3 + m.m2, m.m7 + m.m6, m.m11 + m.m10, m.m15 + m.m14 }; // near
    f.planes[5] = (Vector4) { m.m3 - m.m2, m.m7 - m.m6, m.m11 - m.m10, m.m15 - m.m14 }; // far

    return f;

}

/**
 * @brief Returns true if the axis-aligned box [min, max] is at least partially
 *        inside the frustum (false = fully outside, so the chunk can be skipped).
 *
 * Uses the "positive vertex" test: for each plane, only the box corner furthest
 * along the plane's normal is checked. If even that corner is outside, the whole
 * box is outside.
 */
static bool boxInFrustum( Frustum *f, Vector3 min, Vector3 max ) {

    for ( int p = 0; p < 6; p++ ) {

        Vector4 pl = f->planes[p];

        // "positive vertex": the box corner furthest along this plane's normal.
        float px = ( pl.x >= 0.0f ) ? max.x : min.x;
        float py = ( pl.y >= 0.0f ) ? max.y : min.y;
        float pz = ( pl.z >= 0.0f ) ? max.z : min.z;

        // if even that corner is behind the plane, the whole box is outside.
        if ( pl.x * px + pl.y * py + pl.z * pz + pl.w < 0.0f ) {
            return false;
        }

    }

    return true; // not fully outside any plane

}

/**
 * @brief Computes the world-space axis-aligned bounding box (AABB) of a chunk:
 *        its X/Z grid range and the full vertical height.
 *
 * Block positions are CENTERS, so we extend half a block (hs) past the first
 * and last block on each axis to cover their full extent.
 */
static void chunkBounds( Map *map, Chunk *ch, Vector3 *min, Vector3 *max ) {

    float hs = map->blockSize / 2.0f;   // half a block

    // X spans the chunk's columns (j0 .. j0 + cols - 1).
    min->x = map->pos.x + map->blockSize * ch->j0 - hs;
    max->x = map->pos.x + map->blockSize * ( ch->j0 + ch->cols - 1 ) + hs;

    // Z spans the chunk's rows (i0 .. i0 + rows - 1).
    min->z = map->pos.z + map->blockSize * ch->i0 - hs;
    max->z = map->pos.z + map->blockSize * ( ch->i0 + ch->rows - 1 ) + hs;

    // Y spans the full world height (chunks are full-height).
    min->y = map->pos.y - hs;
    max->y = map->pos.y + map->blockSize * ( map->layers - 1 ) + hs;

}

/**
 * @brief Refreshes the rendered geometry after a block in column (i, j) changed
 *        (broken or placed), per the active render strategy. Shared by
 *        breakBlock and placeBlock.
 */
static void refreshGeometryAfterEdit( Map *map, int i, int j ) {

    switch ( mapStrategy ) {

        case MAP_STRATEGY_NAIVE:
        case MAP_STRATEGY_CULLED:
            // these read the block array every frame; nothing to rebuild.
            break;

        case MAP_STRATEGY_MESH:
            // one giant mesh: the only option is to rebuild it whole (slow!).
            UnloadMesh( map->mesh );
            buildMesh( map );
            break;

        case MAP_STRATEGY_CHUNKED:
        case MAP_STRATEGY_CHUNKED_FRUSTUM: {

                // always rebuild the chunk that owns this block.
                rebuildChunk( map, chunkIndexAt( map, i, j ) );

                // if the block sits on a chunk border, the neighbor chunk across
                // that border also needs rebuilding (one of ITS blocks just got a
                // newly exposed face). Only horizontal borders matter, because
                // chunks span the full height.
                int li = i % CHUNK_SIZE;   // local row inside the chunk
                int lj = j % CHUNK_SIZE;   // local column inside the chunk

                if ( li == 0 ) {
                    rebuildChunk( map, chunkIndexAt( map, i - 1, j ) );
                }
                if ( li == CHUNK_SIZE - 1 ) {
                    rebuildChunk( map, chunkIndexAt( map, i + 1, j ) );
                }
                if ( lj == 0 ) {
                    rebuildChunk( map, chunkIndexAt( map, i, j - 1 ) );
                }
                if ( lj == CHUNK_SIZE - 1 ) {
                    rebuildChunk( map, chunkIndexAt( map, i, j + 1 ) );
                }

            }

            break;

    }

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
            for ( int j = 0; j < map->cols; j++ ) {

                int p = la * ( map->rows * map->cols ) + i * map->cols + j;
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
            for ( int j = 0; j < map->cols; j++ ) {

                int p = la * ( map->rows * map->cols ) + i * map->cols + j;
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

/**
 * @brief Rendering strategy 4: draw one mesh per chunk.
 *
 * Same image as drawMesh, just split across chunks. By itself this is not
 * faster (one draw call per chunk instead of one total); its value is enabling
 * per-chunk mesh rebuilds (block editing) and per-chunk frustum culling (step 4).
 *
 * @param map     The map to draw.
 * @param camera  Active camera (unused for now; used by frustum culling later).
 */
static void drawChunked( Map *map, Camera3D *camera ) {
    // identity transform: chunk meshes are already in world coordinates.
    Matrix transform = MatrixIdentity();
    for ( int c = 0; c < map->chunkCount; c++ ) {
        DrawMesh( map->chunks[c].mesh, map->material, transform );
    }
}

/**
 * @brief Rendering strategy 5: chunked meshes, skipping chunks whose bounding
 *        box falls entirely outside the camera frustum (frustum culling).
 *
 * @param map     The map to draw.
 * @param camera  Active camera; defines the frustum used for culling.
 */
static void drawChunkedFrustum( Map *map, Camera3D *camera ) {

    Frustum frustum = extractFrustum( camera );
    Matrix transform = MatrixIdentity();

    //int drawnChunks = 0;

    for ( int c = 0; c < map->chunkCount; c++ ) {
        
        Chunk *ch = &map->chunks[c];
        Vector3 min;
        Vector3 max;
        chunkBounds( map, ch, &min, &max );

        if ( boxInFrustum( &frustum, min, max ) ) {
            DrawMesh( ch->mesh, map->material, transform );
            //drawnChunks++;
        }

    }

    //trace( "%d", drawnChunks );

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