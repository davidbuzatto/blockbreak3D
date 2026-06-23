#pragma once

#include "raylib/raylib.h"

typedef enum {
    SOUND_TYPE_CLOTH,
    SOUND_TYPE_GRASS,
    SOUND_TYPE_GRAVEL,
    SOUND_TYPE_PLACE,
    SOUND_TYPE_SAND,
    SOUND_TYPE_SNOW,
    SOUND_TYPE_STONE,
    SOUND_TYPE_WOOD,
} SoundType;

typedef struct {
    int quantity;
    int current;
    Sound *pool;
} SoundPool;

SoundPool *createSoundPool( const char *soundPath, int quantity );
void destroySoundPool( SoundPool *soundPool );
void playSoundFromSoundPool( SoundPool *soundPool );