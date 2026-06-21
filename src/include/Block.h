#pragma once

#include <stdbool.h>

#include "raylib/raylib.h"

/**
 * @brief The material a block is made of. It selects which atlas tile(s) the
 *        textured render strategy uses for each face. The older strategies keep
 *        using Block.color, so color and type coexist (no strategy is broken).
 */
typedef enum {
    BLOCK_GRASS = 0,
    BLOCK_DIRT,
    BLOCK_STONE,
    BLOCK_WOOD,
    BLOCK_IRON,
    BLOCK_GOLD,
    BLOCK_GEM
} BlockType;

typedef struct Block {

    Vector3 pos;
    Vector3 dim;
    Color color;
    BlockType type;    // material; selects atlas tiles in the textured strategy

    int hitsToBreak;
    int hits;

    int materialsToAquire;

    bool broken;

} Block;