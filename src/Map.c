#include <stdlib.h>
#include <stdbool.h>

#include "raylib/raylib.h"
#include "stb/stb_perlin.h"

#include "Block.h"
#include "Macros.h"
#include "Map.h"
#include "ResourceManager.h"

static void fillMap( Map *map, float scale, float seed );
static void draw( Map *map, Camera3D *camera );
static void drawBlock( Block *block );

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

    fillMap( new, 0.1f, 0 );
    //fillMap( new, 0.1f, GetRandomValue( 0, 10000 ) );

    new->draw = draw;

    return new;

}

void destroyMap( Map *map ) {
    if ( map != NULL ) {
        free( map->blocks );
        free( map );
    }
}

static void fillMap( Map *map, float scale, float seed ) {

    // sea level
    int baseHeight = map->layers / 2;
    int amplitude = map->layers / 2;

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

static void draw( Map *map, Camera3D *camera ) {

    for ( int la = 0; la < map->layers; la++ ) {
        for ( int i = 0; i < map->rows; i++ ) {
            for ( int j = 0; j < map->columns; j++ ) {
                int p = la * ( map->rows * map->columns ) + i * map->columns + j;
                drawBlock( &map->blocks[p] );
            }

        }
    }

}

static void drawBlock( Block *block ) {
    if ( !block->broken ) {
        DrawCubeV( block->pos, block->dim, block->color );
        DrawCubeWiresV( block->pos, block->dim, BLACK );
    }
}