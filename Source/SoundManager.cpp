#include "Core.h"

#include "acmstrm.h"
#include "ogg/codec.h"
#include "ogg/vorbisfile.h"
#include "portaudio.h"

#include "AutoPointers.h"
#include "FileManager.h"
#include "Log.h"
#include "Random.h"
#include "ResourceManager.h"
#include "SoundManager.h"
#include "Text.h"
#include "Timer.h"

// Manager instance
SoundManager SndMngr;

// Sound structure
class Sound
{
public:
    PaStream* Stream;

    uint8*    Buf;
    uint      BufSize;
    uint      BufCur;

    uint      SampleSize;
    uint      Channels;
    uint      SampleRate;

    bool      IsMusic;
    uint      NextPlay;
    uint      RepeatTime;

    bool      Streamable;
    enum { WAV, ACM, OGG } StreamType;

    OggVorbis_File OggDescriptor;

    Sound() : Stream( NULL ),
        Buf( NULL ), BufSize( 0 ), BufCur( 0 ),
        SampleSize( 0 ), Channels( 0 ), SampleRate( 0 ),
        IsMusic( false ), NextPlay( 0 ), RepeatTime( 0 ),
        Streamable( false ), StreamType( WAV ) {}
    ~Sound()
    {
        SAFEDELA( Buf );
        if( Streamable && StreamType == OGG )
            ov_clear( &OggDescriptor );
        if( Stream )
            Pa_CloseStream( Stream );
    }
};

// SoundManager
bool SoundManager::Init()
{
    if( isActive )
        return true;

    WriteLog( "Sound manager initialization...\n" );

    PaError err = Pa_Initialize();
    if( err != paNoError )
    {
        WriteLog( "PortAudio initialization error<%s,%d>.\n", Pa_GetErrorText( err ), err );
        return false;
    }

    isActive = true;
    WriteLog( "Sound manager initialization complete.\n" );
    return true;
}

void SoundManager::Finish()
{
    WriteLog( "Sound manager finish.\n" );
    ClearSounds();
    Pa_Terminate();
    isActive = false;
    WriteLog( "Sound manager finish complete.\n" );
}

void SoundManager::Process()
{
    for( auto it = soundsActive.begin(); it != soundsActive.end();)
    {
        Sound* sound = *it;
        if( !Pa_IsStreamActive( sound->Stream ) )
        {
            delete sound;
            it = soundsActive.erase( it );
        }
        else
        {
            ++it;
        }
    }
}

void SoundManager::ClearSounds()
{
    for( auto it = soundsActive.begin(); it != soundsActive.end(); ++it )
    {
        Sound* sound = *it;
        Pa_AbortStream( sound->Stream );
        delete sound;
    }
    soundsActive.clear();
}

int SoundManager::GetSoundVolume()
{
    return soundVolume;
}

int SoundManager::GetMusicVolume()
{
    return musicVolume;
}

void SoundManager::SetSoundVolume( int volume )
{
    soundVolume = CLAMP( volume, 0, 100 );
}

void SoundManager::SetMusicVolume( int volume )
{
    musicVolume = CLAMP( volume, 0, 100 );
}

bool SoundManager::ProcessSound( Sound* sound, uint8* output, uint outputSamples )
{
    // Playing
    if( sound->BufCur < sound->BufSize )
    {
        uint whole = outputSamples * sound->SampleSize * sound->Channels;
        if( whole > sound->BufSize - sound->BufCur )
        {
            // Flush last part of buffer
            uint offset = sound->BufSize - sound->BufCur;
            memcpy( output, sound->Buf + sound->BufCur, offset );
            sound->BufCur += offset;

            // Stream new parts
            while( offset < whole && sound->Streamable && Streaming( sound ) )
            {
                uint write = sound->BufSize - sound->BufCur;
                if( offset + write > whole )
                    write = whole - offset;
                memcpy( output + offset, sound->Buf + sound->BufCur, write );
                sound->BufCur += write;
                offset += write;
            }

            // Cut off end
            if( offset < whole )
                memzero( output + offset, whole - offset );
        }
        else
        {
            // Copy
            memcpy( output, sound->Buf + sound->BufCur, whole );
            sound->BufCur += whole;
        }

        // Stream empty buffer
        if( sound->BufCur == sound->BufSize && sound->Streamable )
            Streaming( sound );

        // Volume
        int volume = (sound->IsMusic ? musicVolume : soundVolume);
        if( volume < 100 )
        {
            for( uint i = 0, j = outputSamples * sound->Channels; i < j; i++ )
            {
                short& s = *( (short*)output + i );
                s = (int)s * volume / 100;
            }
        }

        return true;
    }

    // Repeat
    if( sound->RepeatTime )
    {
        if( !sound->NextPlay )
        {
            // Set next playing time
            sound->NextPlay = Timer::GameTick() + (sound->RepeatTime > 1 ? sound->RepeatTime : 0);
        }

        if( Timer::GameTick() >= sound->NextPlay )
        {
            // Set buffer to beginning
            if( sound->Streamable && sound->StreamType == Sound::OGG )
                ov_raw_seek( &sound->OggDescriptor, 0 );
            sound->BufCur = 0;

            // Stream
            if( sound->Streamable )
                Streaming( sound );

            // Drop timer
            sound->NextPlay = 0;

            // Process without silent
            return ProcessSound( sound, output, outputSamples );
        }

        // Give silent
        memzero( output, outputSamples * sound->SampleSize * sound->Channels );
        return true;
    }

    // Give silent
    memzero( output, outputSamples * sound->SampleSize * sound->Channels );
    return false;
}

Sound* SoundManager::Load( const char* fname, int path_type )
{
    char fname_[MAX_FOPATH];
    Str::Copy( fname_, fname );

    if( Str::Substring( fname_, "\\" ) || Str::Substring( fname_, "/" ) )
        path_type = PATH_DATA;

    const char* ext = FileManager::GetExtension( fname );
    if( !ext )
    {
        // Default ext
        ext = fname_ + Str::Length( fname_ );
        Str::Append( fname_, SOUND_DEFAULT_EXT );
    }
    else
    {
        --ext;
    }

    Sound* sound = new Sound();
    if( !sound )
    {
        WriteLogF( _FUNC_, " - Allocation error.\n" );
        return NULL;
    }

    if( !( (Str::CompareCase( ext, ".wav" ) && LoadWAV( sound, fname_, path_type ) ) ||
           (Str::CompareCase( ext, ".acm" ) && LoadACM( sound, fname_, path_type ) ) ||
           (Str::CompareCase( ext, ".ogg" ) && LoadOGG( sound, fname_, path_type ) ) ) )
    {
        delete sound;
        return NULL;
    }

    struct PaStreamProcessor
    {
        static int Process( const void* input, void* output,
                            unsigned long frameCount, const PaStreamCallbackTimeInfo* timeInfo,
                            PaStreamCallbackFlags statusFlags, void* userData )
        {
            if( SndMngr.ProcessSound( (Sound*)userData, (uint8*)output, frameCount ) )
                return paContinue;
            return paComplete;
        }
    };

    PaStream* stream;
    PaError   err = Pa_OpenDefaultStream( &stream, 0, sound->Channels, paInt16,
                                          sound->SampleRate, paFramesPerBufferUnspecified, PaStreamProcessor::Process, sound );
    if( err != paNoError )
    {
        WriteLog( "Pa_OpenDefaultStream error<%s,%d>, file name<%s>.\n", Pa_GetErrorText( err ), err, fname_ );
        delete sound;
        return NULL;
    }
    sound->Stream = stream;

    soundsActive.push_back( sound );
    return sound;
}

bool SoundManager::LoadWAV( Sound* sound, const char* fname, int path_type )
{
    FileManager fm;
    if( !fm.LoadFile( fname, path_type ) )
        return NULL;

    uint dw_buf = fm.GetLEUInt();
    if( dw_buf != MAKEUINT( 'R', 'I', 'F', 'F' ) )
    {
        WriteLogF( _FUNC_, " - <RIFF> not found.\n" );
        return false;
    }

    fm.GoForward( 4 );

    dw_buf = fm.GetLEUInt();
    if( dw_buf != MAKEUINT( 'W', 'A', 'V', 'E' ) )
    {
        WriteLogF( _FUNC_, " - <WAVE> not found.\n" );
        return false;
    }

    dw_buf = fm.GetLEUInt();
    if( dw_buf != MAKEUINT( 'f', 'm', 't', ' ' ) )
    {
        WriteLogF( _FUNC_, " - <fmt > not found.\n" );
        return false;
    }

    dw_buf = fm.GetLEUInt();
    if( !dw_buf )
    {
        WriteLogF( _FUNC_, " - Unknown format.\n" );
        return false;
    }

    struct                      // WAVEFORMATEX
    {
        uint16 wFormatTag;      // Integer identifier of the format
        uint16 nChannels;       // Number of audio channels
        uint   nSamplesPerSec;  // Audio sample rate
        uint   nAvgBytesPerSec; // Bytes per second (possibly approximate)
        uint16 nBlockAlign;     // Size in bytes of a sample block (all channels)
        uint16 wBitsPerSample;  // Size in bits of a single per-channel sample
        uint16 cbSize;          // Bytes of extra data appended to this struct
    } waveformatex;

    fm.CopyMem( &waveformatex, 16 );
    sound->Channels = waveformatex.nChannels;
    sound->SampleRate = waveformatex.nSamplesPerSec;
    sound->SampleSize = waveformatex.wBitsPerSample / 8;

    if( waveformatex.wFormatTag != 1 )
    {
        WriteLogF( _FUNC_, " - Compressed files not supported.\n" );
        return false;
    }

    fm.GoForward( dw_buf - 16 );

    dw_buf = fm.GetLEUInt();
    if( dw_buf == MAKEUINT( 'f', 'a', 'c', 't' ) )
    {
        dw_buf = fm.GetLEUInt();
        fm.GoForward( dw_buf );
        dw_buf = fm.GetLEUInt();
    }

    if( dw_buf != MAKEUINT( 'd', 'a', 't', 'a' ) )
    {
        WriteLogF( _FUNC_, " - Unknown format2.\n" );
        return false;
    }

    dw_buf = fm.GetLEUInt();
    sound->BufSize = dw_buf;
    sound->Buf = new unsigned char[dw_buf];

    if( !fm.CopyMem( sound->Buf, dw_buf ) )
    {
        WriteLogF( _FUNC_, " - File truncated.\n" );
        return false;
    }

    return true;
}

bool SoundManager::LoadACM( Sound* sound, const char* fname, int path_type )
{
    FileManager fm;
    if( !fm.LoadFile( fname, path_type ) )
        return NULL;

    int                   channels = 0;
    int                   freq = 0;
    int                   samples = 0;
    AutoPtr<CACMUnpacker> acm( new CACMUnpacker( fm.GetBuf(), (int)fm.GetFsize(), channels, freq, samples ) );
    if( !acm.IsValid() )
    {
        WriteLogF( _FUNC_, " - ACMUnpacker init fail.\n" );
        return false;
    }

    sound->Channels = (path_type == PATH_SND_MUSIC ? 2 : 1);
    sound->SampleRate = 22050;
    sound->SampleSize = 2;

    sound->BufSize = samples * sound->SampleSize;
    sound->Buf = new unsigned char[sound->BufSize];
    int dec_data = acm->readAndDecompress( (uint16*)sound->Buf, sound->BufSize );
    if( dec_data != (int)sound->BufSize )
    {
        WriteLogF( _FUNC_, " - Decode Acm error.\n" );
        return false;
    }

    return true;
}

size_t Ogg_read_func( void* ptr, size_t size, size_t nmemb, void* datasource )
{
    FileManager* fm = (FileManager*)datasource;
    return fm->CopyMem( ptr, (uint)size ) ? size : 0;
}

int Ogg_seek_func( void* datasource, ogg_int64_t offset, int whence )
{
    FileManager* fm = (FileManager*)datasource;
    switch( whence )
    {
        case SEEK_SET:
            fm->SetCurPos( (uint)offset );
            break;
        case SEEK_CUR:
            fm->GoForward( (uint)offset );
            break;
        case SEEK_END:
            fm->SetCurPos( fm->GetFsize() );
            break;
        default:
            return -1;
    }
    return 0;
}

int Ogg_close_func( void* datasource )
{
    FileManager* fm = (FileManager*)datasource;
    delete fm;
    return 0;
}

long Ogg_tell_func( void* datasource )
{
    FileManager* fm = (FileManager*)datasource;
    return fm->GetCurPos();
}

bool SoundManager::LoadOGG( Sound* sound, const char* fname, int path_type )
{
    FileManager* fm = new FileManager();
    if( !fm || !fm->LoadFile( fname, path_type ) )
    {
        SAFEDEL( fm );
        return NULL;
    }

    ov_callbacks callbacks;
    callbacks.read_func = &Ogg_read_func;
    callbacks.seek_func = &Ogg_seek_func;
    callbacks.close_func = &Ogg_close_func;
    callbacks.tell_func = &Ogg_tell_func;

    int error = ov_open_callbacks( fm, &sound->OggDescriptor, NULL, 0, callbacks );
    if( error )
    {
        WriteLogF( _FUNC_, " - Open OGG file<%s> fail, error:\n", fname );
        switch( error )
        {
            case OV_EREAD:
                WriteLog( "<A read from media returned an error>.\n" );
                break;
            case OV_ENOTVORBIS:
                WriteLog( "<Bitstream does not contain any Vorbis data>.\n" );
                break;
            case OV_EVERSION:
                WriteLog( "<Vorbis version mismatch>.\n" );
                break;
            case OV_EBADHEADER:
                WriteLog( "<Invalid Vorbis bitstream header>.\n" );
                break;
            case OV_EFAULT:
                WriteLog( "<Internal logic fault; indicates a bug or heap/stack corruption>.\n" );
                break;
            default:
                WriteLog( "<Unknown error code %d>.\n", error );
                break;
        }
        return false;
    }

    vorbis_info* vi = ov_info( &sound->OggDescriptor, -1 );
    if( !vi )
    {
        WriteLogF( _FUNC_, " - ov_info error.\n" );
        ov_clear( &sound->OggDescriptor );
        return false;
    }

    sound->Channels = vi->channels;
    sound->SampleRate = vi->rate;
    sound->SampleSize = 2;

    sound->Buf = new unsigned char[STREAMING_PORTION];
    if( !sound->Buf )
        return false;

    int  result = 0;
    uint decoded = 0;
    while( true )
    {
        result = ov_read( &sound->OggDescriptor, (char*)sound->Buf + decoded, STREAMING_PORTION - decoded, 0, 2, 1, NULL );
        if( result <= 0 )
            break;
        decoded += result;
        if( decoded >= STREAMING_PORTION )
            break;
    }
    if( result < 0 )
        return false;

    sound->BufSize = decoded;

    if( !result )
    {
        sound->Streamable = false;
        ov_clear( &sound->OggDescriptor );
    }
    else
    {
        sound->Streamable = true;
        sound->StreamType = Sound::OGG;
    }
    return true;
}

bool SoundManager::Streaming( Sound* sound )
{
    if( !( (sound->StreamType == Sound::WAV && StreamingWAV( sound ) ) ||
           (sound->StreamType == Sound::ACM && StreamingACM( sound ) ) ||
           (sound->StreamType == Sound::OGG && StreamingOGG( sound ) ) ) )
        return false;
    return true;
}

bool SoundManager::StreamingWAV( Sound* sound )
{
    return false;
}

bool SoundManager::StreamingACM( Sound* sound )
{
    return false;
}

bool SoundManager::StreamingOGG( Sound* sound )
{
    int  result = 0;
    uint decoded = 0;
    while( true )
    {
        result = ov_read( &sound->OggDescriptor, (char*)sound->Buf + decoded, STREAMING_PORTION - decoded, 0, 2, 1, NULL );
        if( result <= 0 )
            break;
        decoded += result;
        if( decoded >= STREAMING_PORTION )
            break;
    }
    if( result < 0 || !decoded )
        return false;

    sound->BufCur = 0;
    sound->BufSize = decoded;
    return true;
}

bool SoundManager::PlaySound( const char* name )
{
    if( !isActive || !GetSoundVolume() )
        return true;
    Sound* sound = Load( name, PATH_SND_SFX );
    if( !sound )
        return false;
    Pa_StartStream( sound->Stream );
    return true;
}

bool SoundManager::PlaySoundType( uint8 sound_type, uint8 sound_type_ext, uint8 sound_id, uint8 sound_id_ext )
{
    if( !isActive || !GetSoundVolume() )
        return true;

    // Generate name of the sound
    StrMap& names = ResMngr.GetSoundNames();
    char    name[9];
    if( sound_type == 'W' ) // Weapon, W123XXXR
    {
        name[0] = 'W';
        name[1] = sound_type_ext;
        name[2] = sound_id;
        name[3] = sound_id_ext;
        name[4] = 'X';
        name[5] = 'X';
        name[6] = 'X';
        name[7] = '1';
        name[8] = '\0';

        // Try count dublier
        if( !Random( 0, 1 ) )
        {
            name[7] = '2';
            Str::Upper( name );
            if( !names.count( name ) )
                name[7] = '1';
        }
    }
    else if( sound_type == 'S' ) // Door
    {
        name[0] = 'S';
        name[1] = sound_type_ext;
        name[2] = 'D';
        name[3] = 'O';
        name[4] = 'O';
        name[5] = 'R';
        name[6] = 'S';
        name[7] = sound_id;
        name[8] = '\0';
    }
    else     // Other
    {
        name[0] = sound_type;
        name[1] = sound_type_ext;
        name[2] = sound_id;
        name[3] = sound_id_ext;
        name[4] = 'X';
        name[5] = 'X';
        name[6] = 'X';
        name[7] = 'X';
        name[8] = '\0';
    }
    Str::Upper( name );

    auto it = names.find( name );
    if( it == names.end() )
        return false;

    // Play
    return PlaySound( (*it).second.c_str() );
}

bool SoundManager::PlayMusic( const char* fname, uint pos, uint repeat )
{
    if( !isActive || !GetMusicVolume() )
        return true;

    StopMusic();

    // Load new
    Sound* sound = Load( fname, PATH_SND_MUSIC );
    if( !sound )
        return false;

    sound->IsMusic = true;
    sound->RepeatTime = repeat;
    Pa_StartStream( sound->Stream );
    return true;
}

void SoundManager::StopMusic()
{
    // Find and erase old music
    for( auto it = soundsActive.begin(); it != soundsActive.end();)
    {
        Sound* sound = *it;
        if( sound->IsMusic )
        {
            Pa_AbortStream( sound->Stream );
            delete sound;
            it = soundsActive.erase( it );
        }
        else
        {
            ++it;
        }
    }
}

void SoundManager::PlayAmbient( const char* str )
{
    if( !isActive || !GetSoundVolume() )
        return;

    int  rnd = Random( 1, 100 );

    char name[MAX_FOPATH];
    char num[64];

    for( int i = 0; *str; ++i, ++str )
    {
        // Read name
        name[i] = *str;

        if( *str == ':' )
        {
            if( !i )
                return;
            name[i] = '\0';
            str++;

            // Read number
            int j;
            for( j = 0; *str && *str != ','; ++j, ++str )
                num[j] = *str;
            if( !j )
                return;
            num[j] = '\0';

            // Check
            int k = Str::AtoI( num );
            if( rnd <= k )
            {
                if( !Str::CompareCase( name, "blank" ) )
                    PlaySound( name );
                return;
            }

            rnd -= k;
            i = -1;

            while( *str == ' ' )
                str++;
            if( *str != ',' )
                return;
            while( *str == ' ' )
                str++;
            str++;
        }
    }
}
