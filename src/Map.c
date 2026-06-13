#include <stdlib.h>
#include <stdbool.h>

#include "raylib/raylib.h"
#include "stb/stb_perlin.h"

#include "Block.h"
#include "Macros.h"
#include "Map.h"
#include "ResourceManager.h"

typedef struct {
    int dla;
    int di;
    int dj;
    float corners[4][3];
    float shade;
} CubeFace;

static const CubeFace cubeFaces[6] = {
    // +X (direita): vizinho em j+1
    {  0,  0,  1, { {1,-1,-1}, {1,1,-1}, {1,1,1}, {1,-1,1} }, 0.8f },
    // -X (esquerda): vizinho em j-1
    {  0,  0, -1, { {-1,-1,1}, {-1,1,1}, {-1,1,-1}, {-1,-1,-1} }, 0.65f },
    // +Y (cima): vizinho em la+1
    {  1,  0,  0, { {-1,1,-1}, {-1,1,1}, {1,1,1}, {1,1,-1} }, 1.0f },
    // -Y (baixo): vizinho em la-1
    { -1,  0,  0, { {-1,-1,-1}, {1,-1,-1}, {1,-1,1}, {-1,-1,1} }, 0.5f },
    // +Z (frente): vizinho em i+1
    {  0,  1,  0, { {-1,-1,1}, {1,-1,1}, {1,1,1}, {-1,1,1} }, 0.75f },
    // -Z (trás): vizinho em i-1
    {  0, -1,  0, { {-1,-1,-1}, {-1,1,-1}, {1,1,-1}, {1,-1,-1} }, 0.6f },
};

static void fillMap( Map *map, float scale, float seed );
static void buildMesh( Map *map );
static bool isSolid( Map *map, int la, int i, int j );
static void draw( Map *map, Camera3D *camera );

Map *createMap( int x, int y, int z, int layers, int rows, int columns, int blockSize ) {

    Map *new = (Map*) malloc( sizeof( Map ) );

    new->pos.x = x;
    new->pos.y = y;
    new->pos.z = z;

    new->layers = layers;
    new->rows = rows;
    new->columns = columns;

    new->blockSize = blockSize;
    new->blocks = (Block*) malloc( sizeof( Block ) * new->layers * new->rows * new->columns );

    fillMap( new, 0.05f, 0 );
    //fillMap( new, 0.1f, GetRandomValue( 0, 10000 ) );
    buildMesh( new );

    new->draw = draw;

    return new;

}

void destroyMap( Map *map ) {
    if ( map != NULL ) {
        UnloadMesh( map->mesh );
        free( map->blocks );
        free( map );
    }
}

static void fillMap( Map *map, float scale, float seed ) {

    // sea level
    /*int baseHeight = map->layers / 2;
    int amplitude = map->layers / 2;*/

    int baseHeight = 10;
    int amplitude = 10;

    for ( int i = 0; i < map->rows; i++ ) {
        for ( int j = 0; j < map->columns; j++ ) {

            float nx = ( j + seed ) * scale;
            float ny = ( i + seed ) * scale;
            //float n = stb_perlin_noise3( nx, ny, 0.0f, 0, 0, 0 );
            float n = stb_perlin_fbm_noise3( nx, ny, 0.0f, 2.0f, 0.5f, 6 );

            int h = baseHeight + (int) ( n * amplitude );
            if ( h < 0 ) {
                h = 0;
            } else if ( h >= map->layers ) {
                h = map->layers - 1;
            }

            for ( int la = 0; la < map->layers; la++ ) {

                Color color = ORANGE;
                int hitsToBreak = 1;
                int materialsToAquire = 0;

                bool broken = la > h;

                if ( la == h ) {
                    color = GREEN;
                } else if ( la >= h - 2 ) {
                    color = BROWN;
                } else {
                    color = GRAY;
                }

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

static void buildMesh( Map *map ) {

    float hs = map->blockSize / 2.0f; // meio

    // step 1: count visible faces
    int faceCount = 0;
    for ( int la = 0; la < map->layers; la++ ) {
        for ( int i = 0; i < map->rows; i++ ) {
            for ( int j = 0; j < map->columns; j++ ) {
                if ( !isSolid( map, la, i, j ) ) {
                    continue;
                }
                for ( int f = 0; f < 6; f++ ) {
                    const CubeFace *face = &cubeFaces[f];
                    // if neighbor is air, this face will be showed
                    if ( !isSolid( map, la + face->dla, i + face->di, j + face->dj ) ) {
                        faceCount++;
                    }
                }
            }
        }
    }

    // step 2: allocate memory and fill vertices
    int vertexCount = faceCount * 6;

    Mesh mesh = { 0 };
    mesh.vertexCount = vertexCount;
    mesh.triangleCount = faceCount * 2;
    mesh.vertices = (float*) MemAlloc( vertexCount * 3 * sizeof( float ) );
    mesh.colors = (unsigned char*) MemAlloc( vertexCount * 4 * sizeof( unsigned char ) );

    int v = 0;
    int ci = 0;
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

                    // ignore face if neighbor is solid
                    if ( isSolid( map, la + face->dla, i + face->di, j + face->dj ) ) {
                        continue;
                    }

                    for ( int k = 0; k < 6; k++ ) {

                        const float *c = face->corners[order[k]];

                        mesh.vertices[v++] = center.x + c[0] * hs;
                        mesh.vertices[v++] = center.y + c[1] * hs;
                        mesh.vertices[v++] = center.z + c[2] * hs;

                        mesh.colors[ci++] = (unsigned char) ( color.r * face->shade );
                        mesh.colors[ci++] = (unsigned char) ( color.g * face->shade );
                        mesh.colors[ci++] = (unsigned char) ( color.b * face->shade );
                        mesh.colors[ci++] = 255;

                    }

                }

            }
        }
    }
    
    UploadMesh( &mesh, false );
    map->mesh = mesh;

    map->material = LoadMaterialDefault();
    map->material.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;


}

static bool isSolid( Map *map, int la, int i, int j ) {

    // checks limits
    if ( la < 0 || la >= map->layers  ||
          i < 0 ||  i >= map->rows    ||
          j < 0 ||  j >= map->columns ) {
        return false;
    }

    int p = la * ( map->rows * map->columns ) + i * map->columns + j;

    return !map->blocks[p].broken;

}

static void draw( Map *map, Camera3D *camera ) {
    // draws in its position
    DrawMesh( map->mesh, map->material, MatrixIdentity() );
}
