#include "Core.h"

#include <stdarg.h>

#if defined (FOCLASSIC_SERVER) && !defined (SERVER_DAEMON)
# include <FL/Fl_Text_Display.H>
#endif

#include "FileSystem.h"
#include "Log.h"
#include "Mutex.h"
#include "Text.h"
#include "Thread.h"
#include "Timer.h"

using namespace std;

Mutex              LogLocker;
void*              LogFileHandle = NULL;
vector<LogFuncPtr> LogFunctions;
bool               LogFunctionsInProcess = false;
void*              LogTextBox = NULL;
string*            LogBufferStr = NULL;
bool               ToDebugOutput = false;
bool               LoggingWithTime = false;
bool               LoggingWithThread = false;
uint               StartLogTime = 0;

void WriteLogInternal( bool prefixes, const char* func, const char* frmt, va_list& list );

void LogToFile( const char* fname )
{
    SCOPE_LOCK( LogLocker );

    if( LogFileHandle )
        FileClose( LogFileHandle );
    LogFileHandle = NULL;

    if( fname && fname[0] )
        LogFileHandle = FileOpen( fname, true, true );
}

void LogToFunc( LogFuncPtr func_ptr, bool enable )
{
    SCOPE_LOCK( LogLocker );

    if( func_ptr )
    {
        vector<LogFuncPtr>::iterator it = std::find( LogFunctions.begin(), LogFunctions.end(), func_ptr );
        if( enable && it == LogFunctions.end() )
            LogFunctions.push_back( func_ptr );
        else if( !enable && it != LogFunctions.end() )
            LogFunctions.erase( it );
    }
    else if( !enable )
    {
        LogFunctions.clear();
    }
}

void LogToTextBox( void* text_box )
{
    SCOPE_LOCK( LogLocker );

    LogTextBox = text_box;
}

void LogToBuffer( bool enable )
{
    SCOPE_LOCK( LogLocker );

    SAFEDEL( LogBufferStr );
    if( enable )
    {
        LogBufferStr = new string();
        LogBufferStr->reserve( MAX_LOGTEXT * 2 );
    }
}

void LogToDebugOutput( bool enable )
{
    SCOPE_LOCK( LogLocker );

    ToDebugOutput = enable;
}

void LogFinish()
{
    SCOPE_LOCK( LogLocker );

    LogToFile( NULL );
    LogToFunc( NULL, false );
    LogToTextBox( NULL );
    LogToBuffer( false );
    LogToDebugOutput( false );
}

void WriteLog( const char* frmt, ... )
{
    va_list list;
    va_start( list, frmt );
    WriteLogInternal( true, NULL, frmt, list );
    va_end( list );
}

void WriteLogF( const char* func, const char* frmt, ... )
{
    va_list list;
    va_start( list, frmt );
    WriteLogInternal( true, func, frmt, list );
    va_end( list );
}

void WriteLogX( const char* frmt, ... )
{
    va_list list;
    va_start( list, frmt );
    WriteLogInternal( false, NULL, frmt, list );
    va_end( list );
}

void WriteLogInternal( bool prefixes, const char* func, const char* frmt, va_list& list )
{
    SCOPE_LOCK( LogLocker );

    if( LogFunctionsInProcess )
        return;

    char str_tid[64] = { 0 };
    char str_time[64] = { 0 };

    if( prefixes )
    {
        #if !defined (FONLINE_NPCEDITOR) && !defined (FONLINE_MRFIXIT)
        if( LoggingWithThread )
        {
            const char* name = Thread::GetCurrentName();
            if( name[0] )
                Str::Format( str_tid, "[%s]", name );
            else
                Str::Format( str_tid, "[%04u]", Thread::GetCurrentId() );
        }
        #endif

        if( LoggingWithTime )
        {
            if( !StartLogTime )
                StartLogTime = Timer::FastTick();

            uint delta = Timer::FastTick() - StartLogTime;
            uint seconds = delta / 1000;
            uint minutes = seconds / 60 % 60;
            uint hours = seconds / 60 / 60;
            // if( hours )
            Str::Format( str_time, "[%04u:%02u:%02u:%03u]", hours, minutes, seconds % 60, delta % 1000 );
            // else if( minutes )
            //    Str::Format( str_time, "[%02u:%02u:%03u]", minutes, seconds % 60, delta % 1000 );
            // else
            //    Str::Format( str_time, "[%02u:%03u]", seconds % 60, delta % 1000 );
        }
    }

    char str[MAX_LOGTEXT] = { 0 };
    if( str_tid[0] )
        Str::Append( str, str_tid );
    if( str_time[0] )
        Str::Append( str, str_time );
    if( str_tid[0] || str_time[0] )
        Str::Append( str, " " );
    if( func )
        Str::Append( str, func );

    size_t len = Str::Length( str );
    vsnprintf( &str[len], MAX_LOGTEXT - len, frmt, list );
    str[MAX_LOGTEXT - 1] = 0;

    if( LogFileHandle )
    {
        FileWrite( LogFileHandle, str, Str::Length( str ) );
    }
    if( !LogFunctions.empty() )
    {
        LogFunctionsInProcess = true;
        for( size_t i = 0, j = LogFunctions.size(); i < j; i++ )
            (*LogFunctions[i])( str );
        LogFunctionsInProcess = false;
    }
    if( LogTextBox )
    {
        #if defined (FOCLASSIC_SERVER) && !defined (SERVER_DAEMON)
        ( (Fl_Text_Display*)LogTextBox )->buffer()->append( str );
        #endif
    }
    if( LogBufferStr )
    {
        *LogBufferStr += str;
    }
    if( ToDebugOutput )
    {
        #ifdef FO_WINDOWS
        OutputDebugString( str );
        #else
        printf( "%s", str );
        #endif
    }
}

void LogWithTime( bool enable )
{
    SCOPE_LOCK( LogLocker );

    LoggingWithTime = enable;
}

void LogWithThread( bool enable )
{
    SCOPE_LOCK( LogLocker );

    LoggingWithThread = enable;
}

void LogGetBuffer( std::string& buf )
{
    SCOPE_LOCK( LogLocker );

    if( LogBufferStr )
    {
        buf = *LogBufferStr;
        LogBufferStr->clear();
    }
}
