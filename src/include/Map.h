#pragma once

#include "raylib/raylib.h"
#include "raylib/raymath.h"

#include "Block.h"

typedef struct Map Map;
struct Map {

    Vector3 pos;

    int layers;
    int rows;
    int columns;

    int blockSize;
    Block *blocks;

    Mesh mesh;
    Material material;

    void (*draw)( Map *map, Camera3D *camera );

};

Map *createMap( int x, int y, int z, int layers, int rows, int columns, int blockSize );
void destroyMap( Map *map );