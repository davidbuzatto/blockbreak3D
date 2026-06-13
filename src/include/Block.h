#pragma once

#include <stdbool.h>

#include "raylib/raylib.h"

typedef struct Block {

    Vector3 pos;
    Vector3 dim;
    Color color;

    int hitsToBreak;
    int hits;

    int materialsToAquire;

    bool broken;

} Block;