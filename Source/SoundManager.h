#ifndef __SOUND_MANAGER__
#define __SOUND_MANAGER__

#include "Random.h"
#include "Types.h"

#define SOUND_DEFAULT_EXT    ".acm"
#define MUSIC_REPEAT_TIME    (Random( 240, 360 ) * 1000)     // 4-6 minutes
#define STREAMING_PORTION    (0x10000)

class Sound;
typedef std::vector<Sound*> SoundVec;

class SoundManager
{
public:
    SoundManager() : isActive( false ), soundVolume( 100 ), musicVolume( 100 ) {}

    bool Init();
    void Finish();
    void Process();
    void ClearSounds();

    int  GetSoundVolume();
    int  GetMusicVolume();
    void SetSoundVolume( int volume );
    void SetMusicVolume( int volume );

    bool PlaySound( const char* name );
    bool PlaySoundType( uint8 sound_type, uint8 sound_type_ext, uint8 sound_id, uint8 sound_id_ext );
    bool PlayMusic( const char* fname, uint pos = 0, uint repeat = MUSIC_REPEAT_TIME );
    void StopMusic();
    void PlayAmbient( const char* str );

private:
    bool   ProcessSound( Sound* sound, uint8* output, uint outputSamples );
    Sound* Load( const char* fname, int path_type );
    bool   LoadWAV( Sound* sound, const char* fname, int path_type );
    bool   LoadACM( Sound* sound, const char* fname, int path_type );
    bool   LoadOGG( Sound* sound, const char* fname, int path_type );
    bool   Streaming( Sound* sound );
    bool   StreamingWAV( Sound* sound );
    bool   StreamingACM( Sound* sound );
    bool   StreamingOGG( Sound* sound );

    bool     isActive;
    int      soundVolume;
    int      musicVolume;
    SoundVec soundsActive;
};

extern SoundManager SndMngr;

#endif // __SOUND_MANAGER__
