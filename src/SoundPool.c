#include <stdlib.h>

#include "raylib/raylib.h"

#include "Macros.h"
#include "SoundPool.h"

SoundPool *createSoundPool( const char *soundPath, int quantity ) {

    SoundPool *new = (SoundPool*) malloc( sizeof( SoundPool ) );

    new->quantity = quantity;
    new->current = 0;

    new->pool = (Sound*) malloc( sizeof( Sound ) * quantity );

    // load the audio data only once: pool[0] owns it, and the remaining voices
    // are lightweight aliases that share that same data but keep their own
    // playback state. this lets the sound overlap with itself without decoding
    // the file N times or duplicating its samples in memory.
    new->pool[0] = LoadSound( soundPath );
    for ( int i = 1; i < quantity; i++ ) {
        new->pool[i] = LoadSoundAlias( new->pool[0] );
    }

    return new;

}

void destroySoundPool( SoundPool *soundPool ) {
    if ( soundPool != NULL ) {
        if ( soundPool->pool != NULL ) {
            // unload the aliases first (each frees only its own playback voice,
            // not the shared data), then the owner, which frees the audio data.
            for ( int i = 1; i < soundPool->quantity; i++ ) {
                UnloadSoundAlias( soundPool->pool[i] );
            }
            UnloadSound( soundPool->pool[0] );
            free( soundPool->pool );
        }
        free( soundPool );
    }
}

void playSoundFromSoundPool( SoundPool *soundPool ) {
    PlaySound( soundPool->pool[soundPool->current%soundPool->quantity]);
    soundPool->current++;
}