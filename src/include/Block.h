#pragma once

#include <stdbool.h>

#include "raylib/raylib.h"

/**
 * @brief The material a block is made of. It selects which atlas tile(s) the
 *        textured render strategy uses for each face. The older strategies keep
 *        using Block.color, so color and type coexist (no strategy is broken).
 */
typedef enum {
    BLOCK_GRASS,
    BLOCK_DIRT,
    BLOCK_STONE,
    BLOCK_OAK_PLANKS,
    BLOCK_OAK_LOG,
    BLOCK_OAK_LEAVES,
    BLOCK_WATER,
    BLOCK_SNOW,
    BLOCK_GLASS,
    BLOCK_ICE,
    BLOCK_IRON_BLOCK,
    BLOCK_SAND,
    BLOCK_LAVA,
    BLOCK_SLATE,
    BLOCK_OBSIDIAN,
    BLOCK_BRICKS,
    BLOCK_MOSSY_STONE,
    BLOCK_GRAVEL,
    BLOCK_IRON,
    BLOCK_GOLD,
    BLOCK_GEM,
    BLOCK_COAL
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