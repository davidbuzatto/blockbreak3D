#include <stdlib.h>

#include "raylib/raylib.h"

#include "Macros.h"
#include "SoundPool.h"

SoundPool *createSoundPool( const char *soundPath, int quantity ) {

    SoundPool *new = (SoundPool*) malloc( sizeof( SoundPool ) );

    new->quantity = quantity;
    new->current = 0;

    new->pool = (Sound*) malloc( sizeof( Sound ) * quantity );

    for ( int i = 0; i < quantity; i++ ) {
        new->pool[i] = LoadSound( soundPath );
    }

    return new;

}

void destroySoundPool( SoundPool *soundPool ) {
    if ( soundPool != NULL ) {
        if ( soundPool->pool != NULL ) {
            for ( int i = 0; i < soundPool->quantity; i++ ) {
                UnloadSound( soundPool->pool[i] );
            }
            free( soundPool->pool );
        }
        free( soundPool );
    }
}

void playSoundFromSoundPool( SoundPool *soundPool ) {
    PlaySound( soundPool->pool[soundPool->current%soundPool->quantity]);
    soundPool->current++;
}