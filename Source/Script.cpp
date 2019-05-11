#include "Core.h"

#include <angelscript.h>
#include <scriptany.h>
#include <scriptarray.h>
#include <scriptdictionary.h>
#include <scriptfile.h>
#include <scriptmath.h>
#include <scriptstring.h>
#include <preprocessor.h>

#include "App.h"
#include "ConfigFile.h" // LogicMT
#include "DynamicLibrary.h"
#include "Exception.h"
#include "Extension.h"
#include "FileManager.h"
#include "FileSystem.h"
#include "GameOptions.h"
#include "Ini.h"
#include "Log.h"
#include "Script.h"
#include "ScriptPragmas.h"
#include "ScriptReservedFunctions.h"
#include "ScriptUtils.h"
#include "Text.h"
#include "Thread.h"
#include "Timer.h"
#include "Types.h"
#include "Version.h"

#ifdef SCRIPT_MULTITHREADING
# include "Mutex.h"
# include "ThreadSync.h"
#endif

using namespace std;

const char* ContextStatesStr[] =
{
    "Finished",
    "Suspended",
    "Aborted",
    "Exception",
    "Prepared",
    "Uninitialized",
    "Active",
    "Error",
};

class BindFunction
{
public:
    bool               IsScriptCall;
    asIScriptFunction* ScriptFunc;
    string             ModuleName;
    string             FuncName;
    string             FuncDecl;
    size_t             NativeFuncAddr;

    BindFunction( asIScriptFunction* script_func, size_t native_func_addr, const char* module_name,  const char* func_name, const char* func_decl )
    {
        IsScriptCall = (native_func_addr == 0);
        ScriptFunc = script_func;
        NativeFuncAddr = native_func_addr;
        ModuleName = module_name;
        FuncName = func_name;
        FuncDecl = (func_decl ? func_decl : "");
    }
};
typedef vector<BindFunction> BindFunctionVec;
asIScriptEngine* Engine = nullptr;
void*            EngineLogFile = nullptr;
int              ScriptsPath = PATH_SCRIPTS;
bool             LogDebugInfo = true;
StrVec           WrongGlobalObjects;

uint             GarbagerCycle = 120000;
uint             EvaluationCycle = 120000;
double           MaxGarbagerTime = 40.0;
#ifdef SCRIPT_MULTITHREADING
MutexCode        GarbageLocker;
#endif
uint GetGCStatistics();
void CollectGarbage( asDWORD flags );

BindFunctionVec BindedFunctions;
#ifdef SCRIPT_MULTITHREADING
Mutex           BindedFunctionsLocker;
#endif
StrVec          ScriptFuncCache;
IntVec          ScriptFuncBindId;

bool            ConcurrentExecution = false;
#ifdef SCRIPT_MULTITHREADING
Mutex           ConcurrentExecutionLocker;
#endif

#ifdef FOCLASSIC_SERVER
Mutex ProfilerLocker;
Mutex ProfilerCallStacksLocker;
void* ProfilerFileHandle = NULL;

enum EProfilerStage
{
    ProfilerUninitialized = 0,
    ProfilerDataSet       = 1,
    ProfilerInitialized   = 2,
    ProfilerModulesAdded  = 3,
    ProfilerWorking       = 4
};

EProfilerStage ProfilerStage = ProfilerUninitialized;

uint           ProfilerSampleInterval = 0;
uint           ScriptTimeoutTime = 0;
uint           ProfilerTimeoutTime = 0;
uint           ProfilerSaveInterval = 0;
bool           ProfilerDynamicDisplay = false;

// extern in AngelScript code
void CheckProfiler()
{
    if( ProfilerSampleInterval )
    {
        ProfilerLocker.Unlock();
        ProfilerLocker.Lock();
    }
}

struct Call
{
    Call() : Id( 0 ), Line( 0 ) {}
    Call( int id, uint line )
    {
        Id = id;
        Line = line;
    }
    int  Id;
    uint Line;
};

struct CallPath
{
    uint                Id;
    map<int, CallPath*> Children;
    uint                Incl;
    uint                Excl;
    CallPath( int id ) : Id( id ), Incl( 1 ), Excl( 0 ) {}
    CallPath*           AddChild( int id )
    {
        auto it = Children.find( id );
        if( it != Children.end() ) return it->second;

        CallPath* child = new CallPath( id );
        Children[id] = child;
        return child;
    }
    void StackEnd()
    {
        Excl++;
    }
};

typedef map<int, CallPath*> IntCallPathMap;
IntCallPathMap CallPaths;
uint           TotalCallPaths = 0;

typedef vector<Call>        CallStack;
vector<CallStack*> Stacks;

void ProcessStack( CallStack* stack )
{
    SCOPE_LOCK( ProfilerCallStacksLocker );
    TotalCallPaths++;
    int       top_id = stack->back().Id;
    CallPath* path;

    auto      itp = CallPaths.find( top_id );
    if( itp == CallPaths.end() )
    {
        path = new CallPath( top_id );
        CallPaths[top_id] = path;
    }
    else
    {
        path = itp->second;
        path->Incl++;
    }

    for( CallStack::reverse_iterator it = stack->rbegin() + 1, end = stack->rend(); it != end; ++it )
    {
        path = path->AddChild( it->Id );
    }

    path->StackEnd();
}

BINARY_SIGNATURE( ProfilerSaveSignature, BINARY_TYPE_PROFILERSAVE, 1 );
#endif

BINARY_SIGNATURE( ScriptSaveSignature,   BINARY_TYPE_SCRIPTSAVE, FOCLASSIC_VERSION );

bool LoadLibraryCompiler = false;

// Contexts
THREAD asIScriptContext* GlobalCtx[GLOBAL_CONTEXT_STACK_SIZE] = { 0 };
THREAD uint              GlobalCtxIndex = 0;
class ActiveContext
{
public:
    asIScriptContext** Contexts;
    uint               StartTick;
    ActiveContext( asIScriptContext** ctx, uint tick ) : Contexts( ctx ), StartTick( tick ) {}
    bool operator==( asIScriptContext** ctx ) { return ctx == Contexts; }
};
typedef vector<ActiveContext> ActiveContextVec;
ActiveContextVec ActiveContexts;
Mutex            ActiveGlobalCtxLocker;

// Timeouts
uint   RunTimeoutSuspend = 600000;     // 10 minutes
uint   RunTimeoutMessage = 300000;     // 5 minutes
Thread RunTimeoutThread;
void RunTimeout( void* );

Preprocessor* ScriptPreprocessor;

bool Script::Init( bool with_log, uint8 app )
{
    if( with_log && !StartLog() )
    {
        WriteLogF( _FUNC_, " - Log creation error.\n" );
        return false;
    }

    // Create default engine
    ScriptPragmaCallback* pragma_callback = new ScriptPragmaCallback( app );
    Engine = CreateEngine( pragma_callback, App.TypeToName( app ) );
    if( !Engine )
    {
        WriteLogF( _FUNC_, " - Can't create AS engine.\n" );
        delete pragma_callback;
        return false;
    }

    BindedFunctions.clear();
    BindedFunctions.reserve( 10000 );
    BindedFunctions.push_back( BindFunction( 0, 0, "", "", "" ) );     // None
    BindedFunctions.push_back( BindFunction( 0, 0, "", "", "" ) );     // Temp

    if( !InitThread() )
        return false;

    ScriptPreprocessor = new Preprocessor();

    // Game options callback
    struct GameOptScript
    {
        static bool ScriptLoadModule( const char* module_name )
        {
            return Script::LoadScript( module_name, NULL, false, NULL );
        }
        static uint ScriptBind( const char* module_name, const char* func_decl, bool temporary_id )
        {
            return (uint)Script::Bind( module_name, func_decl, NULL, temporary_id, false );
        }
        static bool ScriptPrepare( uint bind_id )
        {
            return Script::PrepareContext( bind_id, _FUNC_, "ScriptDllCall" );
        }
        static void ScriptSetArgInt8( char value )
        {
            Script::SetArgUChar( value );
        }
        static void ScriptSetArgInt16( short value )
        {
            Script::SetArgUShort( value );
        }
        static void ScriptSetArgInt( int value )
        {
            Script::SetArgUInt( value );
        }
        static void ScriptSetArgInt64( int64 value )
        {
            Script::SetArgUInt64( value );
        }
        static void ScriptSetArgUInt8( uint8 value )
        {
            Script::SetArgUChar( value );
        }
        static void ScriptSetArgUInt16( uint16 value )
        {
            Script::SetArgUShort( value );
        }
        static void ScriptSetArgUInt( uint value )
        {
            Script::SetArgUInt( value );
        }
        static void ScriptSetArgUInt64( uint64 value )
        {
            Script::SetArgUInt64( value );
        }
        static void ScriptSetArgBool( bool value )
        {
            Script::SetArgBool( value );
        }
        static void ScriptSetArgFloat( float value )
        {
            Script::SetArgFloat( value );
        }
        static void ScriptSetArgDouble( double value )
        {
            Script::SetArgDouble( value );
        }
        static void ScriptSetArgObject( void* value )
        {
            Script::SetArgObject( value );
        }
        static void ScriptSetArgAddress( void* value )
        {
            Script::SetArgAddress( value );
        }
        static bool ScriptRunPrepared()
        {
            return Script::RunPrepared();
        }
        static char ScriptGetReturnedInt8()
        {
            return *(char*)Script::GetReturnedRawAddress();
        }
        static short ScriptGetReturnedInt16()
        {
            return *(short*)Script::GetReturnedRawAddress();
        }
        static int ScriptGetReturnedInt()
        {
            return *(int*)Script::GetReturnedRawAddress();
        }
        static int64 ScriptGetReturnedInt64()
        {
            return *(int64*)Script::GetReturnedRawAddress();
        }
        static uint8 ScriptGetReturnedUInt8()
        {
            return *(uint8*)Script::GetReturnedRawAddress();
        }
        static uint16 ScriptGetReturnedUInt16()
        {
            return *(uint16*)Script::GetReturnedRawAddress();
        }
        static uint ScriptGetReturnedUInt()
        {
            return *(uint*)Script::GetReturnedRawAddress();
        }
        static uint64 ScriptGetReturnedUInt64()
        {
            return *(uint64*)Script::GetReturnedRawAddress();
        }
        static bool ScriptGetReturnedBool()
        {
            return *(bool*)Script::GetReturnedRawAddress();
        }
        static float ScriptGetReturnedFloat()
        {
            return Script::GetReturnedFloat();
        }
        static double ScriptGetReturnedDouble()
        {
            return Script::GetReturnedDouble();
        }
        static void* ScriptGetReturnedObject()
        {
            return *(void**)Script::GetReturnedRawAddress();
        }
        static void* ScriptGetReturnedAddress()
        {
            return *(void**)Script::GetReturnedRawAddress();
        }
    };
    GameOpt.ScriptLoadModule = &GameOptScript::ScriptLoadModule;
    GameOpt.ScriptBind = &GameOptScript::ScriptBind;
    GameOpt.ScriptPrepare = &GameOptScript::ScriptPrepare;
    GameOpt.ScriptSetArgInt8 = &GameOptScript::ScriptSetArgInt8;
    GameOpt.ScriptSetArgInt16 = &GameOptScript::ScriptSetArgInt16;
    GameOpt.ScriptSetArgInt = &GameOptScript::ScriptSetArgInt;
    GameOpt.ScriptSetArgInt64 = &GameOptScript::ScriptSetArgInt64;
    GameOpt.ScriptSetArgUInt8 = &GameOptScript::ScriptSetArgUInt8;
    GameOpt.ScriptSetArgUInt16 = &GameOptScript::ScriptSetArgUInt16;
    GameOpt.ScriptSetArgUInt = &GameOptScript::ScriptSetArgUInt;
    GameOpt.ScriptSetArgUInt64 = &GameOptScript::ScriptSetArgUInt64;
    GameOpt.ScriptSetArgBool = &GameOptScript::ScriptSetArgBool;
    GameOpt.ScriptSetArgFloat = &GameOptScript::ScriptSetArgFloat;
    GameOpt.ScriptSetArgDouble = &GameOptScript::ScriptSetArgDouble;
    GameOpt.ScriptSetArgObject = &GameOptScript::ScriptSetArgObject;
    GameOpt.ScriptSetArgAddress = &GameOptScript::ScriptSetArgAddress;
    GameOpt.ScriptRunPrepared = &GameOptScript::ScriptRunPrepared;
    GameOpt.ScriptGetReturnedInt8 = &GameOptScript::ScriptGetReturnedInt8;
    GameOpt.ScriptGetReturnedInt16 = &GameOptScript::ScriptGetReturnedInt16;
    GameOpt.ScriptGetReturnedInt = &GameOptScript::ScriptGetReturnedInt;
    GameOpt.ScriptGetReturnedInt64 = &GameOptScript::ScriptGetReturnedInt64;
    GameOpt.ScriptGetReturnedUInt8 = &GameOptScript::ScriptGetReturnedUInt8;
    GameOpt.ScriptGetReturnedUInt16 = &GameOptScript::ScriptGetReturnedUInt16;
    GameOpt.ScriptGetReturnedUInt = &GameOptScript::ScriptGetReturnedUInt;
    GameOpt.ScriptGetReturnedUInt64 = &GameOptScript::ScriptGetReturnedUInt64;
    GameOpt.ScriptGetReturnedBool = &GameOptScript::ScriptGetReturnedBool;
    GameOpt.ScriptGetReturnedFloat = &GameOptScript::ScriptGetReturnedFloat;
    GameOpt.ScriptGetReturnedDouble = &GameOptScript::ScriptGetReturnedDouble;
    GameOpt.ScriptGetReturnedObject = &GameOptScript::ScriptGetReturnedObject;
    GameOpt.ScriptGetReturnedAddress = &GameOptScript::ScriptGetReturnedAddress;

    RunTimeoutThread.Start( RunTimeout, "ScriptTimeout" );
    return true;
}

void Script::Finish()
{
    if( !Engine )
        return;

    EndLog();
    #ifdef FOCLASSIC_SERVER
    Profiler::Finish();
    #endif
    RunTimeoutSuspend = 0;
    RunTimeoutMessage = 0;
    RunTimeoutThread.Wait();

    BindedFunctions.clear();
    ScriptPreprocessor->SetPragmaCallback( NULL );
    ScriptPreprocessor->UndefAll();
    UnloadScripts();
    delete ScriptPreprocessor;

    FinishEngine( Engine );     // Finish default engine

    #pragma MESSAGE("Client crashed here, disable finishing until fix angelscript.")
    #ifndef FOCLASSIC_CLIENT
    FinishThread();
    #endif

}

bool Script::InitThread()
{
    for( int i = 0; i < GLOBAL_CONTEXT_STACK_SIZE; i++ )
    {
        GlobalCtx[i] = CreateContext();
        if( !GlobalCtx[i] )
        {
            WriteLogF( _FUNC_, " - Create global contexts fail.\n" );
            Engine->Release();
            Engine = NULL;
            return false;
        }
    }

    return true;
}

void Script::FinishThread()
{
    ActiveGlobalCtxLocker.Lock();
    auto it = std::find( ActiveContexts.begin(), ActiveContexts.end(), (asIScriptContext**)GlobalCtx );
    if( it != ActiveContexts.end() )
        ActiveContexts.erase( it );
    ActiveGlobalCtxLocker.Unlock();

    for( int i = 0; i < GLOBAL_CONTEXT_STACK_SIZE; i++ )
        FinishContext( GlobalCtx[i] );

    asThreadCleanup();
}

void* Script::LoadDynamicLibrary( const char* dll_name )
{
    // Find in already loaded
    char dll_name_lower[MAX_FOPATH];
    Str::Copy( dll_name_lower, dll_name );
    #ifdef FO_WINDOWS
    Str::Lower( dll_name_lower );
    #endif
    EngineData* edata = (EngineData*)Engine->GetUserData();
    auto        it = edata->LoadedDlls.find( dll_name_lower );
    if( it != edata->LoadedDlls.end() )
        return (*it).second.second;

    // Make path
    char dll_path[MAX_FOPATH];
    Str::Copy( dll_path, dll_name_lower );
    FileManager::EraseExtension( dll_path );

    #if defined (FO_X64)
    // Add '64' appendix
    Str::Append( dll_path, "64" );
    #endif

    // DLL extension
    #ifdef FO_WINDOWS
    Str::Append( dll_path, ".dll" );
    #else
    Str::Append( dll_path, ".so" );
    #endif

    #pragma TODO("Better paths handling")
    // Client path fixes
    #if defined (FOCLASSIC_CLIENT)
    Str::Insert( dll_path, FileManager::GetPath( PATH_SERVER_SCRIPTS ) );
    Str::Replacement( dll_path, '\\', '.' );
    Str::Replacement( dll_path, '/', '.' );
    #endif

    // Insert base path
    Str::Insert( dll_path, FileManager::GetFullPath( "", ScriptsPath ) );

    // Load dynamic library
    void* dll = DLL_Load( dll_path );
    if( !dll )
        return NULL;

    // Verify compilation target
    size_t* ptr = DLL_GetAddress( dll, edata->DllTarget.c_str() );
    if( !ptr )
    {
        WriteLogF( _FUNC_, " - Wrong script DLL<%s>, expected target<%s>, but found<%s%s%s%s>.\n", dll_name, edata->DllTarget.c_str(),
                   DLL_GetAddress( dll, "SERVER" ) ? "SERVER" : "", DLL_GetAddress( dll, "CLIENT" ) ? "CLIENT" : "", DLL_GetAddress( dll, "MAPPER" ) ? "MAPPER" : "",
                   !DLL_GetAddress( dll, "SERVER" ) && !DLL_GetAddress( dll, "CLIENT" ) && !DLL_GetAddress( dll, "MAPPER" ) ? "Nothing" : "" );
        DLL_Free( dll );
        return NULL;
    }

    // Register variables
    ptr = DLL_GetAddress( dll, "ASEngine" );
    if( ptr )
        *ptr = (size_t)Engine;
    ptr = DLL_GetAddress( dll, "FOClassic" );
    if( ptr )
        *ptr = (size_t)&GameOpt;
    ptr = DLL_GetAddress( dll, "FOClassicExt" );
    if( ptr )
        *ptr = (size_t)&GameOptExt;

    // Register functions
    ptr = DLL_GetAddress( dll, "Log" );
    if( ptr )
        *ptr = (size_t)&WriteLog;
    ptr = DLL_GetAddress( dll, "ScriptGetActiveContext" );
    if( ptr )
        *ptr = (size_t)&asGetActiveContext;
    ptr = DLL_GetAddress( dll, "ScriptGetLibraryOptions" );
    if( ptr )
        *ptr = (size_t)&asGetLibraryOptions;
    ptr = DLL_GetAddress( dll, "ScriptGetLibraryVersion" );
    if( ptr )
        *ptr = (size_t)&asGetLibraryVersion;

    // Call init function
    typedef void ( * DllMainEx )( bool );
    DllMainEx func = (DllMainEx)DLL_GetAddress( dll, "DllMainEx" );
    if( func )
        (func)(LoadLibraryCompiler);

    // Add to collection for current engine
    edata->LoadedDlls.insert( PAIR( string( dll_name_lower ), PAIR( string( dll_path ), dll ) ) );

    return dll;
}

void Script::SetWrongGlobalObjects( StrVec& names )
{
    WrongGlobalObjects = names;
}

void Script::SetConcurrentExecution( bool enabled )
{
    ConcurrentExecution = enabled;
}

void Script::SetLoadLibraryCompiler( bool enabled )
{
    LoadLibraryCompiler = enabled;
}

void Script::UnloadScripts()
{
    EngineData*      edata = (EngineData*)Engine->GetUserData();
    ScriptModuleVec& modules = edata->Modules;

    for( auto it = modules.begin(), end = modules.end(); it != end; ++it )
    {
        asIScriptModule* module = *it;
        int              result = module->ResetGlobalVars();
        if( result < 0 )
            WriteLogF( _FUNC_, " - Reset global vars fail, module<%s>, error<%d>.\n", module->GetName(), result );
        result = module->UnbindAllImportedFunctions();
        if( result < 0 )
            WriteLogF( _FUNC_, " - Unbind fail, module<%s>, error<%d>.\n", module->GetName(), result );
    }

    for( auto it = modules.begin(), end = modules.end(); it != end; ++it )
    {
        asIScriptModule* module = *it;
        Engine->DiscardModule( module->GetName() );
    }

    modules.clear();
    CollectGarbage( asGC_FULL_CYCLE );
}

bool Script::ReloadScripts( const string& section_modules, const char* app, bool skip_binaries, const char* file_pefix /* = NULL */ )
{
    WriteLog( "Reload %s scripts...\n", app );

    Script::UnloadScripts();

    int    loaded = 0, errors = 0;

    StrVec modules;
    ConfigFile->GetSectionKeys( section_modules, modules, true );

    for( auto it = modules.begin(), end = modules.end(); it != end; ++it )
    {
        string module_name = *it;

        #ifdef FOCLASSIC_SERVER
        if( ConfigFile->GetBool( SECTION_SERVER, "VerboseInit", false ) )
            WriteLog( "Load %s module<%s>\n", app, module_name.c_str() );
        #endif

        if( !LoadScript( module_name.c_str(), NULL, skip_binaries, file_pefix ) )
        {
            WriteLog( "Load %s module fail, name<%s>.\n", app, module_name.c_str() );
            errors++;
            continue;
        }

        #ifdef FOCLASSIC_SERVER
        Script::Profiler::AddModule( module_name.c_str() );
        #endif

        loaded++;
    }

    #ifdef FOCLASSIC_SERVER
    Script::Profiler::EndModules();
    Script::Profiler::SaveFunctionsData();
    #endif

    errors += BindImportedFunctions();
    errors += RebindFunctions();

    WriteLog( "Reload %s scripts... ", app );
    if( errors )
        WriteLogX( "failed" );
    else
        WriteLogX( "loaded<%d>", loaded );
    WriteLogX( "\n" );

    return errors == 0;
}

bool Script::BindReservedFunctions( const string& section_binds, const char* app, ReservedFunctionsMap& bind_map, bool use_temp /* = false */ )
{
    WriteLog( "Bind reserved %s functions...\n", app );

    int errors = 0;

    // Stage #0
    // Initial validation

    if( bind_map.find( "all" ) != bind_map.end() )
    {
        WriteLog( "Bind reserved %s functions... INTERNAL ERROR: bind_map[\"all\"] found\n", app );
        return false;
    }

    // Stage #1
    // Fill ReservedFunctionsMap with values from <ConfigFile>

    StrVec cfg_binds;
    ConfigFile->GetSectionKeys( section_binds, cfg_binds, true );
    for( auto it = cfg_binds.begin(), end = cfg_binds.end(); it != end; ++it )
    {
        const string& bind_name = *it;

        // First word : target (required)
        // No checks here; empty entries should never be added to ConfigFile

        StrVec bind_options = ConfigFile->GetStrVec( section_binds, bind_name );

        string bind_target = bind_options.front();
        bind_options.erase( bind_options.begin() );

        // Second word : type (optional; default: "script")

        ReservedFunctionType bind_type = RESERVED_FUNCTION_UNKNOWN;

        if( bind_options.empty() )
            bind_type = RESERVED_FUNCTION_SCRIPT;
        else
        {
            string tmp = bind_options.front();
            bind_options.erase( bind_options.begin() );

            if( tmp == "script" )
                bind_type = RESERVED_FUNCTION_SCRIPT;
            else if( tmp == "extension" )
                bind_type = RESERVED_FUNCTION_EXTENSION;
        }

        // Configure "all" bind; set single target for all not yet configured entries

        if( bind_name == "all" )
        {
            for( auto map_it = bind_map.begin(), map_end = bind_map.end(); map_it != map_end; ++map_it )
            {
                ReservedFunction& bind_func = map_it->second;

                if( bind_func.Target.empty() )
                {
                    bind_func.Target = bind_target;
                    bind_func.Type = bind_type;
                }
            }

            continue;
        }

        // Ignore unknown binds

        auto bind_map_it = bind_map.find( bind_name );
        if( bind_map_it == bind_map.end() )
            continue;

        // Configure regular bind

        ReservedFunction& bind_func = bind_map_it->second;

        bind_func.Target = bind_target;
        bind_func.Type = bind_type;
    }

    // Stage #2
    // Validate ReservedFunctionsMap

    for( auto it = bind_map.begin(), end = bind_map.end(); it != end; ++it )
    {
        const ReservedFunction& bind_func = it->second;

        if( bind_func.Target.empty() )
        {
            WriteLog( "Bind %s function %s -> ERROR: missing entry\n", app, it->first.c_str() );
            errors++;
        }
        else if( bind_func.Type == RESERVED_FUNCTION_UNKNOWN )
        {
            WriteLog( "Bind %s function %s -> ERROR: unknown type<%u>\n", app, it->first.c_str(), bind_func.Type );
            errors++;
        }
    }

    if( errors )
    {
        WriteLog( "Bind reserved %s functions... failed\n", app );
        return false;
    }

    // Stage #3
    // Bind functions

    for( auto it = bind_map.begin(), end = bind_map.end(); it != end; ++it )
    {
        const string&     bind_name = it->first;
        ReservedFunction& bind_func = it->second;
        int               bind_id = 0;

        #if defined (FOCLASSIC_SERVER)
        if( ConfigFile->GetBool( SECTION_SERVER, "VerboseInit", false ) )
            WriteLog( "Bind %s function %s -> %s -> ", app, bind_name.c_str(), bind_func.Target.c_str() );
        #endif

        bind_id = Bind( bind_name, bind_func, use_temp );

        if( bind_id > 0 )
        {
            #if defined (FOCLASSIC_SERVER)
            if( ConfigFile->GetBool( SECTION_SERVER, "VerboseInit", false ) )
                WriteLogX( "OK [%d]\n", bind_id );
            #endif

            *(bind_func.BindId) = bind_id;
        }
        else
        {
            #if defined (FOCLASSIC_SERVER)
            if( ConfigFile->GetBool( SECTION_SERVER, "VerboseInit", false ) )
                WriteLogX( "ERROR\n" );
            #endif

            WriteLog( "Bind reserved %s function fail, name<%s>.\n", app, bind_name.c_str() );
            errors++;
        }
    }

    WriteLog( "Bind reserved %s functions... %s\n", app, errors == 0 ? "complete" : "failed" );
    return errors == 0;
}

#ifdef FOCLASSIC_SERVER
void Script::Profiler::SetData( uint sample_time, uint save_time, bool dynamic_display )
{
    if( ProfilerStage != ProfilerUninitialized )
    {
        WriteLogF( _FUNC_, " - Internal profiler error.\n" );
        return;
    }
    ProfilerStage = ProfilerDataSet;
    ProfilerSampleInterval = sample_time;

    if( !ProfilerSampleInterval )
    {
        ProfilerSaveInterval = 0;
        ProfilerDynamicDisplay = false;
        return;
    }
    ProfilerSaveInterval = save_time;
    ProfilerDynamicDisplay = dynamic_display;
    if( ProfilerSampleInterval && !ProfilerSaveInterval && !ProfilerDynamicDisplay )
    {
        WriteLog( "Profiler may not be active with both saving and dynamic display disabled. Disabling profiler.\n" );
        ProfilerSampleInterval = 0;
    }
}

void Script::Profiler::Init()
{
    if( !ProfilerSampleInterval )
        return;
    if( ProfilerStage != ProfilerDataSet )
    {
        WriteLogF( _FUNC_, " - Internal profiler error.\n" );
        return;
    }
    ProfilerStage = ProfilerInitialized;

    ProfilerLocker.Lock();
    if( !ProfilerSaveInterval )
        return;

    DateTime dt;
    Timer::GetCurrentDateTime( dt );

    char dump_file_path[MAX_FOPATH];
    FileManager::GetFullPath( NULL, PATH_SERVER_PROFILER, dump_file_path );
    FileManager::CreateDirectoryTree( dump_file_path );

    char dump_file[MAX_FOPATH];
    Str::Format( dump_file, "%sFOnlineServer_Profiler_%04u.%02u.%02u_%02u-%02u-%02u.foprof",
                 dump_file_path, dt.Year, dt.Month, dt.Day, dt.Hour, dt.Minute, dt.Second );

    ProfilerFileHandle = FileOpen( dump_file, true );
    if( !ProfilerFileHandle )
    {
        WriteLog( "Couldn't open profiler dump file, disabling saving.\n" );
        ProfilerSaveInterval = 0;
        if( !ProfilerDynamicDisplay )
        {
            WriteLog( "Disabling profiler.\n" );
            ProfilerSampleInterval = 0;
            return;
        }
    }

    FileWrite( ProfilerFileHandle, ProfilerSaveSignature, sizeof(ProfilerSaveSignature) );
}

void Script::Profiler::AddModule( const char* module_name )
{
    if( !ProfilerSaveInterval )
        return;
    if( ProfilerStage != ProfilerInitialized )
    {
        WriteLogF( _FUNC_, " - Internal profiler error.\n" );
        return;
    }
    // No stage change

    char fname_real[MAX_FOPATH];
    Str::Copy( fname_real, module_name );
    Str::Replacement( fname_real, '.', DIR_SLASH_C );
    Str::Append( fname_real, ".fosp" );
    FileManager::FormatPath( fname_real );

    FileManager file;
    file.LoadFile( fname_real, ScriptsPath );
    if( file.IsLoaded() )
    {
        FileWrite( ProfilerFileHandle, module_name, Str::Length( module_name ) + 1 );
        FileWrite( ProfilerFileHandle, file.GetBuf(), Str::Length( (char*)file.GetBuf() ) + 1 );
    }
}

void Script::Profiler::EndModules()
{
    if( !ProfilerSampleInterval )
        return;
    if( ProfilerStage != ProfilerInitialized )
    {
        WriteLogF( _FUNC_, " - Internal profiler error.\n" );
        return;
    }
    ProfilerStage = ProfilerModulesAdded;
    if( !ProfilerSaveInterval )
        return;

    int dummy = 0;
    FileWrite( ProfilerFileHandle, &dummy, 1 );
}

void Script::Profiler::SaveFunctionsData()
{
    if( !ProfilerSampleInterval )
        return;
    if( ProfilerStage != ProfilerModulesAdded )
    {
        WriteLogF( _FUNC_, " - Internal profiler error.\n" );
        return;
    }
    ProfilerStage = ProfilerWorking;
    if( !ProfilerSaveInterval )
        return;

    asIScriptEngine* engine = GetEngine();

    // TODO: Proper way
    for( int i = 1; i < 1000000; i++ )
    {
        asIScriptFunction* func = engine->GetFunctionById( i );
        if( !func )
            continue;

        char buf[MAX_FOTEXT] = { 0 };
        FileWrite( ProfilerFileHandle, &i, 4 );

        Str::Format( buf, "%s", func->GetModuleName() );
        FileWrite( ProfilerFileHandle, buf, Str::Length( buf ) + 1 );
        Str::Format( buf, "%s", func->GetDeclaration() );
        FileWrite( ProfilerFileHandle, buf, Str::Length( buf ) + 1 );
    }
    int dummy = -1;
    FileWrite( ProfilerFileHandle, &dummy, 4 );
}

void Script::Profiler::Finish()
{
    if( !ProfilerSampleInterval )
        return;
    if( ProfilerStage != ProfilerWorking )
    {
        WriteLogF( _FUNC_, " - Internal profiler error.\n" );
        return;
    }
    ProfilerLocker.Unlock();
    if( ProfilerSaveInterval )
        FileClose( ProfilerFileHandle );
}

// Helper
struct OutputLine
{
    string FuncName;
    uint   Depth;
    float  Incl;
    float  Excl;
    OutputLine( char* text, uint depth, float incl, float excl ) : FuncName( text ), Depth( depth ), Incl( incl ), Excl( excl ) {}
};

void TraverseCallPaths( asIScriptEngine* engine, CallPath* path, vector<OutputLine>& lines, uint depth, uint& max_depth, uint& max_len )
{
    asIScriptFunction* func = engine->GetFunctionById( path->Id );
    char               name[MAX_FOTEXT] = { 0 };
    if( func )
        Str::Format( name, "%s@%s", func->GetModuleName(), func->GetDeclaration() );
    else
        Str::Copy( name, 4, "???\0" );

    lines.push_back( OutputLine( name, depth,
                                 100.0f * (float)(path->Incl) / float(TotalCallPaths),
                                 100.0f * (float)(path->Excl) / float(TotalCallPaths) ) );

    uint len = Str::Length( name ) + depth;
    if( len > max_len )
        max_len = len;
    depth += 2;
    if( depth > max_depth )
        max_depth = depth;

    for( auto it = path->Children.begin(), end = path->Children.end(); it != end; ++it )
        TraverseCallPaths( engine, it->second, lines, depth, max_depth, max_len );
}

string Script::Profiler::GetStatistics()
{
    if( !ProfilerDynamicDisplay )
        return "Dynamic display is disabled.";
    SCOPE_LOCK( ProfilerCallStacksLocker );
    if( !TotalCallPaths )
        return "No calls recorded.";
    string             result;

    vector<OutputLine> lines;
    uint               max_depth = 0;
    uint               max_len = 0;
    for( auto it = CallPaths.begin(), end = CallPaths.end(); it != end; ++it )
        TraverseCallPaths( GetEngine(), it->second, lines, 0, max_depth, max_len );

    char buf[MAX_FOTEXT] = { 0 };
    Str::Format( buf, "%-*s Inclusive %%  Exclusive %%\n\n", max_len, "" );
    result += buf;

    for( uint i = 0; i < lines.size(); i++ )
    {
        Str::Format( buf, "%*s%-*s   %6.2f       %6.2f\n", lines[i].Depth, "",
                     max_len - lines[i].Depth, lines[i].FuncName.c_str(),
                     lines[i].Incl, lines[i].Excl );
        result += buf;
    }
    return result;
}

bool Script::Profiler::IsActive()
{
    return ProfilerSampleInterval != 0;
}
#endif

void Script::DummyAddRef( void* )
{
    // Dummy
}

void Script::DummyRelease( void* )
{
    // Dummy
}

asIScriptEngine* Script::GetEngine()
{
    return Engine;
}

void Script::SetEngine( asIScriptEngine* engine )
{
    Engine = engine;
}

asIScriptEngine* Script::CreateEngine( Preprocessor::Pragma::Callback* pragma_callback, string dll_target )
{
    asIScriptEngine* engine = asCreateScriptEngine( ANGELSCRIPT_VERSION );
    if( !engine )
    {
        WriteLogF( _FUNC_, " - asCreateScriptEngine fail.\n" );
        return NULL;
    }

    engine->SetMessageCallback( asFUNCTION( CallbackMessage ), NULL, asCALL_CDECL );
    RegisterScriptArray( engine, true );
    RegisterScriptString( engine );
    RegisterScriptAny( engine );
    RegisterScriptDictionary( engine );
    RegisterScriptFile( engine );
    RegisterScriptMath( engine );

    EngineData* edata = new EngineData();
    edata->PragmaCallback = pragma_callback;
    edata->DllTarget = dll_target;
    engine->SetUserData( edata );

    return engine;
}

void Script::FinishEngine( asIScriptEngine*& engine )
{
    if( engine )
    {
        EngineData* edata = (EngineData*)engine->SetUserData( nullptr );
        delete edata->PragmaCallback;
        for( auto it = edata->LoadedDlls.begin(), end = edata->LoadedDlls.end(); it != end; ++it )
            DLL_Free( (*it).second.second );
        delete edata;
        engine->Release();
        engine = nullptr;
    }
}

asIScriptContext* Script::CreateContext()
{
    asIScriptContext* ctx = Engine->CreateContext();
    if( !ctx )
    {
        WriteLogF( _FUNC_, " - CreateContext fail.\n" );
        return NULL;
    }

    if( ctx->SetExceptionCallback( asFUNCTION( CallbackException ), NULL, asCALL_CDECL ) < 0 )
    {
        WriteLogF( _FUNC_, " - SetExceptionCallback to fail.\n" );
        ctx->Release();
        return NULL;
    }

    char* buf = new char[CONTEXT_BUFFER_SIZE];
    if( !buf )
    {
        WriteLogF( _FUNC_, " - Allocate memory for buffer fail.\n" );
        ctx->Release();
        return NULL;
    }

    Str::Copy( buf, CONTEXT_BUFFER_SIZE, "<error>" );
    ctx->SetUserData( buf );
    return ctx;
}

void Script::FinishContext( asIScriptContext*& ctx )
{
    if( ctx )
    {
        char* buf = (char*)ctx->GetUserData();
        if( buf )
            delete[] buf;
        ctx->Release();
        ctx = NULL;
    }
}

asIScriptContext* Script::GetGlobalContext()
{
    if( GlobalCtxIndex >= GLOBAL_CONTEXT_STACK_SIZE )
    {
        WriteLogF( _FUNC_, " - Script context stack overflow! Context call stack:\n" );
        for( int i = GLOBAL_CONTEXT_STACK_SIZE - 1; i >= 0; i-- )
            WriteLog( "  %d) %s.\n", i, GlobalCtx[i]->GetUserData() );
        return NULL;
    }
    GlobalCtxIndex++;
    return GlobalCtx[GlobalCtxIndex - 1];
}

void Script::PrintContextCallstack( asIScriptContext* ctx )
{
    int                      line, column;
    const asIScriptFunction* func;
    int                      stack_size = ctx->GetCallstackSize();
    WriteLog( "Context<%s>, state<%s>, call stack<%d>:\n", ctx->GetUserData(), ContextStatesStr[(int)ctx->GetState()], stack_size );

    // Print current function
    if( ctx->GetState() == asEXECUTION_EXCEPTION )
    {
        line = ctx->GetExceptionLineNumber( &column );
        func = ctx->GetExceptionFunction();
    }
    else
    {
        line = ctx->GetLineNumber( 0, &column );
        func = ctx->GetFunction( 0 );
    }
    if( func )
        WriteLog( "  %d) %s : %s : %d, %d.\n", stack_size - 1, func->GetModuleName(), func->GetDeclaration(), line, column );

    // Print call stack
    for( int i = 1; i < stack_size; i++ )
    {
        func = ctx->GetFunction( i );
        line = ctx->GetLineNumber( i, &column );
        if( func )
            WriteLog( "  %d) %s : %s : %d, %d.\n", stack_size - i - 1, func->GetModuleName(), func->GetDeclaration(), line, column );
    }
}

const char* Script::GetActiveModuleName()
{
    static const char error[] = "<error>";
    asIScriptContext* ctx = asGetActiveContext();
    if( !ctx )
        return error;
    asIScriptFunction* func = ctx->GetFunction( 0 );
    if( !func )
        return error;
    return func->GetModuleName();
}

const char* Script::GetActiveFuncName()
{
    static const char error[] = "<error>";
    asIScriptContext* ctx = asGetActiveContext();
    if( !ctx )
        return error;
    asIScriptFunction* func = ctx->GetFunction( 0 );
    if( !func )
        return error;
    return func->GetName();
}

asIScriptModule* Script::GetModule( const char* name )
{
    return Engine->GetModule( name, asGM_ONLY_IF_EXISTS );
}

asIScriptModule* Script::CreateModule( const char* module_name )
{
    // Delete old
    EngineData*      edata = (EngineData*)Engine->GetUserData();
    ScriptModuleVec& modules = edata->Modules;
    for( auto it = modules.begin(), end = modules.end(); it != end; ++it )
    {
        asIScriptModule* module = *it;
        if( Str::Compare( module->GetName(), module_name ) )
        {
            modules.erase( it );
            break;
        }
    }

    // Create new
    asIScriptModule* module = Engine->GetModule( module_name, asGM_ALWAYS_CREATE );
    if( !module )
        return NULL;
    modules.push_back( module );
    return module;
}

void Script::ScriptGarbager( bool collect_now /* = false */ )
{
    static uint8  garbager_state = 5;
    static uint   last_nongarbage = 0;
    static uint   best_count = 0;
    static uint   last_garbager_tick = 0;
    static uint   last_evaluation_tick = 0;

    static uint   garbager_count[3];       // Uses garbager_state as index
    static double garbager_time[3];        // Uses garbager_state as index

    if( !Engine )
        return;

    if( collect_now )
    {
        CollectGarbage( asGC_FULL_CYCLE );
        garbager_state = 5;
        return;
    }

    switch( garbager_state )
    {
        case 5: // First time
        {
            best_count = 1000;
        }
        case 6: // Statistics init and first time
        {
            CollectGarbage( asGC_FULL_CYCLE | asGC_DESTROY_GARBAGE );

            last_nongarbage = GetGCStatistics();
            garbager_state = 0;
            break;
        }
        case 0: // Statistics stage 0, 1, 2
        case 1:
        case 2:
        {
            uint current_size = GetGCStatistics();

            // Try 1x, 1.5x and 2x count time for time extrapolation
            if( current_size < last_nongarbage + (best_count * (2 + garbager_state) ) / 2 )
                break;

            garbager_time[garbager_state] = Timer::AccurateTick();
            CollectGarbage( asGC_FULL_CYCLE | asGC_DESTROY_GARBAGE );
            garbager_time[garbager_state] = Timer::AccurateTick() - garbager_time[garbager_state];

            last_nongarbage = GetGCStatistics();
            garbager_count[garbager_state] = current_size - last_nongarbage;

            if( !garbager_count[garbager_state] )
                break;                                             // Repeat this step
            garbager_state++;
            break;
        }
        case 3: // Statistics last stage, calculate best count
        {
            double obj_times[2];
            bool   undetermined = false;
            for( int i = 0; i < 2; i++ )
            {
                if( garbager_count[i + 1] == garbager_count[i] )
                {
                    undetermined = true;                   // Too low resolution, break statistics and repeat later
                    break;
                }

                obj_times[i] = (garbager_time[i + 1] - garbager_time[i]) / ( (double)garbager_count[i + 1] - (double)garbager_count[i] );

                if( obj_times[i] <= 0.0f )             // Should not happen
                {
                    undetermined = true;               // Too low resolution, break statistics and repeat later
                    break;
                }
            }
            garbager_state = 4;
            if( undetermined )
                break;

            double object_delete_time = (obj_times[0] + obj_times[1]) / 2;
            double overhead = 0.0f;
            for( int i = 0; i < 3; i++ )
                overhead += (garbager_time[i] - garbager_count[i] * object_delete_time);
            overhead /= 3;
            if( overhead > MaxGarbagerTime )
                overhead = MaxGarbagerTime;                                    // Will result on deletion on every frame
            best_count = (uint)( (MaxGarbagerTime - overhead) / object_delete_time );
            break;
        }
        case 4: // Normal garbage check
        {
            if( GarbagerCycle && Timer::FastTick() - last_garbager_tick >= GarbagerCycle )
            {
                CollectGarbage( asGC_ONE_STEP | asGC_DETECT_GARBAGE );
                last_garbager_tick = Timer::FastTick();
            }

            if( EvaluationCycle && Timer::FastTick() - last_evaluation_tick >= EvaluationCycle )
            {
                garbager_state = 6;               // Enter statistics after normal garbaging is done
                last_evaluation_tick = Timer::FastTick();
            }

            if( GetGCStatistics() > last_nongarbage + best_count )
            {
                CollectGarbage( asGC_FULL_CYCLE | asGC_DESTROY_GARBAGE );
            }
            break;
        }
        default:
            break;
    }
}

uint GetGCStatistics()
{
    asUINT current_size = 0;
    Engine->GetGCStatistics( &current_size );
    return (uint)current_size;
}

void CollectGarbage( asDWORD flags )
{
    #ifdef SCRIPT_MULTITHREADING
    if( LogicMT )
        GarbageLocker.LockCode();
    Engine->GarbageCollect( flags );
    if( LogicMT )
        GarbageLocker.UnlockCode();
    #else
    Engine->GarbageCollect( flags );
    #endif
}

void RunTimeout( void* data )
{
    while( RunTimeoutSuspend )
    {
        #ifndef FOCLASSIC_SERVER
        Thread::Sleep( 100 );
        #else
        if( ProfilerSampleInterval )
        {
            Thread::Sleep( ProfilerSampleInterval );
            CallStack*                                     stack = NULL;

            volatile MutexLocker<decltype(ProfilerLocker)> scope_lock__2( ProfilerLocker );
            SCOPE_LOCK( ActiveGlobalCtxLocker );

            for( auto it = ActiveContexts.begin(), end = ActiveContexts.end(); it != end; ++it )
            {
                ActiveContext& actx = *it;

                // Fetch the call stacks
                for( int i = GLOBAL_CONTEXT_STACK_SIZE - 1; i >= 0; i-- )
                {
                    asIScriptContext* ctx = actx.Contexts[i];
                    if( ctx->GetState() == asEXECUTION_ACTIVE )
                    {
                        asIScriptFunction* func;
                        int                line, column = 0;
                        uint               stack_size = ctx->GetCallstackSize();

                        // Add new entry
                        if( !stack )
                            stack = new CallStack();

                        for( uint j = 0; j < stack_size; j++ )
                        {
                            func = ctx->GetFunction( j );
                            if( func )
                            {
                                line = ctx->GetLineNumber( j );
                                stack->push_back( Call( func->GetId(), line ) );
                                // WriteLog("%d> %i) %s\n", i, j, func->GetName());
                            }
                            else
                                stack->push_back( Call( 0, 0 ) );
                        }
                    }
                }
            }

            if( stack )
            {
                if( ProfilerDynamicDisplay )
                    ProcessStack( stack );
                if( ProfilerSaveInterval )
                    Stacks.push_back( stack );
                else
                    delete stack;
            }

            uint cur_tick = Timer::FastTick();

            if( ProfilerSaveInterval && ProfilerStage == ProfilerWorking && cur_tick >= ProfilerTimeoutTime + ProfilerSaveInterval )
            {

                uint time = Timer::FastTick();
                uint size = Stacks.size();

                for( auto it = Stacks.begin(), end = Stacks.end(); it != end; ++it )
                {
                    CallStack* stack = *it;
                    for( auto it2 = stack->begin(), end2 = stack->end(); it2 != end2; ++it2 )
                    {
                        FileWrite( ProfilerFileHandle, &(it2->Id), 4 );
                        FileWrite( ProfilerFileHandle, &(it2->Line), 4 );
                    }
                    static int dummy = -1;
                    FileWrite( ProfilerFileHandle, &dummy, 4 );
                    delete *it;
                }

                Stacks.clear();
                ProfilerTimeoutTime = cur_tick;
            }

            if( cur_tick < ScriptTimeoutTime + 100 )
                continue;
            ScriptTimeoutTime = cur_tick;
        }
        else
            Thread::Sleep( 100 );
        #endif
        SCOPE_LOCK( ActiveGlobalCtxLocker );
        uint cur_tick = Timer::FastTick();

        for( auto it = ActiveContexts.begin(), end = ActiveContexts.end(); it != end; ++it )
        {
            ActiveContext& actx = *it;
            if( cur_tick >= actx.StartTick + RunTimeoutSuspend )
            {
                // Suspend all contexts
                for( int i = GLOBAL_CONTEXT_STACK_SIZE - 1; i >= 0; i-- )
                    if( actx.Contexts[i]->GetState() == asEXECUTION_ACTIVE )
                        actx.Contexts[i]->Suspend();

                // Erase from collection
                ActiveContexts.erase( it );
                break;
            }
        }
    }
}

void Script::SetRunTimeout( uint suspend_timeout, uint message_timeout )
{
    RunTimeoutSuspend = suspend_timeout;
    RunTimeoutMessage = message_timeout;
}

/************************************************************************/
/* Load / Bind                                                          */
/************************************************************************/

void Script::SetScriptsPath( int path_type )
{
    ScriptsPath = path_type;
}

void Script::Define( const char* def )
{
    ScriptPreprocessor->Define( def );
}

void Script::DefineVersion()
{
    Undef( "__VERSION" );
    Undef( "FOCLASSIC_STAGE" );
    Undef( "FOCLASSIC_VERSION" );
    Undef( "ANGELSCRIPT_VERSION" );

    Script::Define( Str::FormatBuf( "__VERSION %u", FOCLASSIC_VERSION ) );
    Script::Define( Str::FormatBuf( "FOCLASSIC_STAGE %u", FOCLASSIC_STAGE ) );
    Script::Define( Str::FormatBuf( "FOCLASSIC_VERSION %u", FOCLASSIC_VERSION ) );
    Script::Define( Str::FormatBuf( "ANGELSCRIPT_VERSION %u", ANGELSCRIPT_VERSION ) );
}

void Script::Undef( const char* def )
{
    if( def )
        ScriptPreprocessor->Undef( def );
    else
        ScriptPreprocessor->UndefAll();
}

void Script::CallPragmas( const StrVec& pragmas )
{
    EngineData* edata = (EngineData*)Engine->GetUserData();

    // Set current pragmas
    ScriptPreprocessor->SetPragmaCallback( edata->PragmaCallback );

    // Call pragmas
    for( uint i = 0, j = (uint)pragmas.size() / 2; i < j; i++ )
        ScriptPreprocessor->CallPragma( pragmas[i * 2], pragmas[i * 2 + 1] );
}

bool Script::LoadScript( const char* module_name, const char* source, bool skip_binary, const char* file_prefix /* = NULL */ )
{
    EngineData*      edata = (EngineData*)Engine->GetUserData();
    ScriptModuleVec& modules = edata->Modules;

    char             fname_real[MAX_FOPATH];
    Str::Copy( fname_real, module_name );
    Str::Append( fname_real, ".fos" );
    FileManager::FormatPath( fname_real );

    char fname_script[MAX_FOPATH] = { 0 };
    FileManager::ExtractPath( fname_real, fname_script );
    if( Str::Length( fname_script ) )
    {
        if( file_prefix )
            Str::Append( fname_script, file_prefix );

        char fname_file[MAX_FOPATH];
        FileManager::ExtractFileName( fname_real, fname_file );
        Str::Append( fname_script, fname_file );
    }
    else
    {
        Str::Copy( fname_script, module_name );
        Str::Append( fname_script, ".fos" );

        if( file_prefix )
            Str::Insert( fname_script, file_prefix );
    }
    FileManager::FormatPath( fname_script );

    char module_real[MAX_FOPATH];
    Str::Copy( module_real, module_name );
    FileManager::FormatPath( module_real );
    #ifdef FO_WINDOWS
    Str::Replacement( module_real, DIR_SLASH_C, '/' );
    #endif

    // Set current pragmas
    ScriptPreprocessor->SetPragmaCallback( edata->PragmaCallback );

    // Try load precompiled script
    FileManager file_bin;
    if( !skip_binary )
    {
        FileManager file;
        file.LoadFile( fname_real, ScriptsPath );
        file_bin.LoadFile( Str::FormatBuf( "%sb", fname_script ), ScriptsPath );

        if( file_bin.IsLoaded() && file_bin.GetFsize() > sizeof(ScriptSaveSignature) )
        {
            // Check signature
            uint8 signature[sizeof(ScriptSaveSignature)];
            bool  bin_signature = file_bin.CopyMem( signature, sizeof(signature) );
            bool  load = (bin_signature && memcmp( ScriptSaveSignature, signature, sizeof(ScriptSaveSignature) ) == 0);

            // Check AngelScript version
            if( load )
            {
                uint as_version = file_bin.GetBEUInt();
                load = (ANGELSCRIPT_VERSION == as_version);
            }

            StrVec pragmas;
            if( load )
            {
                // Check file dependencies and pragmas
                StrVec dependencies;
                char   str[1024];
                uint   dependencies_size = file_bin.GetBEUInt();
                for( uint i = 0; i < dependencies_size; i++ )
                {
                    file_bin.GetStr( str );
                    dependencies.push_back( str );
                }
                uint pragmas_size = file_bin.GetBEUInt();

                for( uint i = 0; i < pragmas_size; i++ )
                {
                    file_bin.GetStr( str );
                    pragmas.push_back( str );
                }

                // Check for outdated
                uint64 last_write, last_write_bin;
                file_bin.GetTime( NULL, NULL, &last_write_bin );

                // Main file
                file.GetTime( NULL, NULL, &last_write );
                bool have_source = file.IsLoaded();
                bool outdated = (have_source && last_write > last_write_bin);

                if( have_source )
                {
                    // Include files
                    for( uint d = 0, dLen = (uint)dependencies.size(); d < dLen; d++ )
                    {
                        FileManager file_dep;
                        file_dep.LoadFile( dependencies[d].c_str(), ScriptsPath );
                        file_dep.GetTime( NULL, NULL, &last_write );

                        if( !outdated )
                            outdated = !file_dep.IsLoaded();

                        if( !outdated )
                            outdated = (file_dep.IsLoaded() && last_write > last_write_bin);

                        if( outdated )
                            break;
                    }
                }

                if( outdated )
                    load = false;
            }

            if( load )
            {
                // Delete old
                for( auto it = modules.begin(), end = modules.end(); it != end; ++it )
                {
                    asIScriptModule* module = *it;
                    if( Str::Compare( module->GetName(), module_real ) )
                    {
                        WriteLogF( _FUNC_, " - Warning, script with name<%s> already exist. Discarding.\n", module_real );
                        Engine->DiscardModule( module_real );
                        modules.erase( it );
                        break;
                    }
                }

                asIScriptModule* module = Engine->GetModule( module_real, asGM_ALWAYS_CREATE );
                if( module )
                {
                    for( uint i = 0, j = (uint)pragmas.size() / 2; i < j; i++ )
                        ScriptPreprocessor->CallPragma( pragmas[i * 2], pragmas[i * 2 + 1] );

                    CBytecodeStream binary;
                    binary.Write( file_bin.GetCurBuf(), file_bin.GetFsize() - file_bin.GetCurPos() );

                    int result = module->LoadByteCode( &binary );
                    if( result >= 0 )
                    {
                        ScriptPreprocessor->GetParsedPragmas() = pragmas;
                        modules.push_back( module );
                        return true;
                    }
                    else
                        WriteLogF( _FUNC_, " - Can't load binary, script<%s> error<%s>\n", module_real, GetASReturnCode( result ) );
                }
                else
                    WriteLogF( _FUNC_, " - Create module fail, script<%s>.\n", module_real );
            }
        }

        if( !file.IsLoaded() )
        {
            WriteLogF( _FUNC_, " - Script<%s> not found.\n", fname_real );
            return false;
        }

        file_bin.UnloadFile();
    }

    class MemoryFileLoader : public Preprocessor::FileLoader
    {
protected:
        const char* source;

public:
        MemoryFileLoader( const char* s ) : source( s ) {}
        virtual ~MemoryFileLoader() {}

        virtual bool LoadFile( const std::string& dir, const std::string& file_name, std::vector<char>& data )
        {
            if( source )
            {
                size_t len = strlen( source );
                data.resize( len );
                memcpy( &data[0], source, len );
                source = NULL;
                return true;
            }
            return Preprocessor::FileLoader::LoadFile( dir, file_name, data );
        }
    };

    Preprocessor::StringOutStream result, errors;
    MemoryFileLoader              loader( source );
    int                           errors_count;
    errors_count = ScriptPreprocessor->Preprocess( FileManager::GetFullPath( fname_real, ScriptsPath ), result, &errors, &loader );

    if( !errors.String.empty() )
    {
        while( errors.String[errors.String.length() - 1] == '\n' )
            errors.String.pop_back();
        WriteLogF( _FUNC_, " - File<%s> preprocessor message<%s>.\n", fname_real, errors.String.c_str() );
    }

    if( errors_count )
    {
        WriteLogF( _FUNC_, " - Unable to preprocess file<%s>.\n", fname_real );
        return false;
    }

    // Delete old
    for( auto it = modules.begin(), end = modules.end(); it != end; ++it )
    {
        asIScriptModule* module = *it;
        if( Str::Compare( module->GetName(), module_real ) )
        {
            WriteLogF( _FUNC_, " - Warning, script for this name<%s> already exist. Discarding.\n", module_real );
            Engine->DiscardModule( module_real );
            modules.erase( it );
            break;
        }
    }

    asIScriptModule* module = Engine->GetModule( module_real, asGM_ALWAYS_CREATE );
    if( !module )
    {
        WriteLogF( _FUNC_, " - Create module fail, script<%s>.\n", module_real );
        return false;
    }

    int as_result = module->AddScriptSection( module_real, result.String.c_str() );
    if( as_result < 0 )
    {
        WriteLogF( _FUNC_, " - Unable to AddScriptSection module<%s>, result<%s>.\n", module_real, GetASReturnCode( as_result ) );
        return false;
    }

    as_result = module->Build();
    if( as_result < 0 )
    {
        WriteLogF( _FUNC_, " - Unable to Build module<%s>, result<%s>.\n", module_real, GetASReturnCode( as_result ) );
        return false;
    }

    // Check not allowed global variables
    if( WrongGlobalObjects.size() )
    {
        IntVec bad_typeids;
        bad_typeids.reserve( WrongGlobalObjects.size() );
        for( auto it = WrongGlobalObjects.begin(), end = WrongGlobalObjects.end(); it != end; ++it )
            bad_typeids.push_back( Engine->GetTypeIdByDecl( (*it).c_str() ) & asTYPEID_MASK_SEQNBR );

        IntVec bad_typeids_class;
        for( int m = 0, n = module->GetObjectTypeCount(); m < n; m++ )
        {
            asIObjectType* ot = module->GetObjectTypeByIndex( m );
            for( int i = 0, j = ot->GetPropertyCount(); i < j; i++ )
            {
                int type = 0;
                ot->GetProperty( i, NULL, &type, NULL, NULL );
                type &= asTYPEID_MASK_SEQNBR;
                for( uint k = 0; k < bad_typeids.size(); k++ )
                {
                    if( type == bad_typeids[k] )
                    {
                        bad_typeids_class.push_back( ot->GetTypeId() & asTYPEID_MASK_SEQNBR );
                        break;
                    }
                }
            }
        }

        bool global_fail = false;
        for( int i = 0, j = module->GetGlobalVarCount(); i < j; i++ )
        {
            int type = 0;
            module->GetGlobalVar( i, NULL, NULL, &type, NULL );

            while( type & asTYPEID_TEMPLATE )
            {
                asIObjectType* obj = (asIObjectType*)Engine->GetObjectTypeById( type );
                if( !obj )
                    break;
                type = obj->GetSubTypeId();
            }

            type &= asTYPEID_MASK_SEQNBR;

            for( uint k = 0; k < bad_typeids.size(); k++ )
            {
                if( type == bad_typeids[k] )
                {
                    const char* name = NULL;
                    module->GetGlobalVar( i, &name, NULL, NULL );
                    string      msg = "The global variable '" + string( name ) + "' uses a type that cannot be stored globally";
                    Engine->WriteMessage( "", 0, 0, asMSGTYPE_ERROR, msg.c_str() );
                    global_fail = true;
                    break;
                }
            }
            if( std::find( bad_typeids_class.begin(), bad_typeids_class.end(), type ) != bad_typeids_class.end() )
            {
                const char* name = NULL;
                module->GetGlobalVar( i, &name, NULL, NULL );
                string      msg = "The global variable '" + string( name ) + "' uses a type in class property that cannot be stored globally";
                Engine->WriteMessage( "", 0, 0, asMSGTYPE_ERROR, msg.c_str() );
                global_fail = true;
            }
        }

        if( global_fail )
        {
            WriteLogF( _FUNC_, " - Wrong global variables in module<%s>.\n", module_real );
            return false;
        }
    }

    // Done, add to collection
    modules.push_back( module );

    // Save binary version of script and preprocessed version
    if( !skip_binary && !file_bin.IsLoaded() )
    {
        CBytecodeStream binary;
        int             save_result = module->SaveByteCode( &binary );
        if( save_result >= 0 )
        {
            std::vector<asBYTE>& data = binary.GetBuf();
            StrVec&              dependencies = ScriptPreprocessor->GetFilesPreprocessed();
            const StrVec&        pragmas = ScriptPreprocessor->GetParsedPragmas();

            // Format dependencies paths and make them relative to PATH_SCRIPTS

            const char* scripts_path = FileManager::GetFullPath( "", PATH_SCRIPTS );

            for( int d = 0, dLen = dependencies.size(); d < dLen; d++ )
            {
                char dep[MAX_FOPATH];
                Str::Copy( dep, dependencies[d].c_str() );
                FileManager::FormatPath( dep );
                Str::Replacement( dep, '\\', '/' );
                dependencies[d].assign( dep );
                dependencies[d].erase( 0, Str::Length( scripts_path ) );
            }

            file_bin.SetData( (uint8*)ScriptSaveSignature, sizeof(ScriptSaveSignature) );
            file_bin.SetBEUInt( (uint)ANGELSCRIPT_VERSION );
            file_bin.SetBEUInt( (uint)dependencies.size() );
            for( uint i = 0, j = (uint)dependencies.size(); i < j; i++ )
                file_bin.SetData( (uint8*)dependencies[i].c_str(), (uint)dependencies[i].length() + 1 );
            file_bin.SetBEUInt( (uint)pragmas.size() );
            for( uint i = 0, j = (uint)pragmas.size(); i < j; i++ )
                file_bin.SetData( (uint8*)pragmas[i].c_str(), (uint)pragmas[i].length() + 1 );
            file_bin.SetData( &data[0], (uint)data.size() );

            if( !file_bin.SaveOutBufToFile( Str::FormatBuf( "%sb", fname_script ), ScriptsPath ) )
            {
                WriteLogF( _FUNC_, " - Can't save bytecode file<%sb>, script<%s>.\n", fname_script, module_real );
                return false;
            }
        }
        else
        {
            WriteLogF( _FUNC_, " - Can't write bytecode, script<%s> error<%s>\n", module_real, GetASReturnCode( save_result ) );
            return false;
        }

        FileManager file_prep;
        FormatPreprocessorOutput( result.String );
        file_prep.SetData( (void*)result.String.c_str(), result.String.length() );
        if( !file_prep.SaveOutBufToFile( Str::FormatBuf( "%sp", fname_script ), ScriptsPath ) )
        {
            WriteLogF( _FUNC_, " - Can't write preprocessed file<%sp>, script<%s>.\n", fname_script, module_real );
            return false;
        }
    }
    return true;
}

#if defined (FOCLASSIC_CLIENT)
bool Script::LoadScript( const char* module_name, const uint8* bytecode, uint len )
{
    if( !bytecode || !len )
    {
        WriteLogF( _FUNC_, " - Bytecode empty, module name<%s>.\n", module_name );
        return false;
    }

    EngineData*      edata = (EngineData*)Engine->GetUserData();
    ScriptModuleVec& modules = edata->Modules;

    for( auto it = modules.begin(), end = modules.end(); it != end; ++it )
    {
        asIScriptModule* module = *it;
        if( Str::Compare( module->GetName(), module_name ) )
        {
            WriteLogF( _FUNC_, " - Warning, script for this name<%s> already exist. Discarding.\n", module_name );
            Engine->DiscardModule( module_name );
            modules.erase( it );
            break;
        }
    }

    asIScriptModule* module = Engine->GetModule( module_name, asGM_ALWAYS_CREATE );
    if( !module )
    {
        WriteLogF( _FUNC_, " - Create module fail, script<%s>.\n", module_name );
        return false;
    }

    CBytecodeStream binary;
    binary.Write( bytecode, len );
    int             result = module->LoadByteCode( &binary );
    if( result < 0 )
    {
        WriteLogF( _FUNC_, " - Can't load binary, module<%s>, result<%s>.\n", module_name, GetASReturnCode( result ) );
        return false;
    }

    modules.push_back( module );
    return true;
}
#endif

int Script::BindImportedFunctions()
{
    EngineData*      edata = (EngineData*)Engine->GetUserData();
    ScriptModuleVec& modules = edata->Modules;
    int              errors = 0;

    WriteLog( "Import script functions...\n" );

    for( auto it = modules.begin(), end = modules.end(); it != end; ++it )
    {
        asIScriptModule* module = *it;
        /*
           int              result = module->BindAllImportedFunctions();
           if( result < 0 )
           {
           WriteLogF( _FUNC_, " - Fail to bind imported functions, module<%s>, error<%d>.\n", module->GetName(), result );
           errors++;
           continue;
           }
         */
        for( asUINT i = 0, j = module->GetImportedFunctionCount(); i < j; i++ )
        {
            const char* importModuleName = module->GetImportedFunctionSourceModule( i );
            const char* importFunctionDecl = module->GetImportedFunctionDeclaration( i );
            const char* importString = Str::FormatBuf( "import %s from \"%s\"", importFunctionDecl, importModuleName );

            #if defined (__SERVER)
            if( ConfigFile->GetBool( SECTION_SERVER, "VerboseInit", false ) )
                WriteLog( " %s : %s\n", module->GetName(), importString );
            #endif

            asIScriptModule* importModule = Engine->GetModule( importModuleName, asGM_ONLY_IF_EXISTS );
            if( !importModule )
            {
                WriteLogF( _FUNC_, " Module<%s> cannot %s : source module does not exists\n", module->GetName(), importString );
                errors++;
                continue;
            }

            asIScriptFunction* importFunction = importModule->GetFunctionByDecl( importFunctionDecl );
            if( !importFunction )
            {
                WriteLogF( _FUNC_, " Module<%s> cannot %s : source function does not exists\n", module->GetName(), importString );
                errors++;
                continue;
            }

            int result = module->BindImportedFunction( i, importFunction );
            if( result < 0 )
            {
                WriteLogF( _FUNC_, " Module<%s> cannot %s : bind error<%s>\n", module->GetName(), importString, GetASReturnCode( result ) );
                errors++;
                continue;
            }
        }
    }

    WriteLog( "Import scripts functions... %s\n", !errors ? "OK" : "failed" );

    return errors;
}

int Script::Bind( const string& func_name, const ReservedFunction& bind_func, bool is_temp, bool disable_log /* = false */ )
{
    // wrapper

    string module_name = bind_func.Target;

    if( bind_func.Type == RESERVED_FUNCTION_EXTENSION )
        module_name += ".extension";

    return Bind( module_name.c_str(), func_name.c_str(), bind_func.FuncDecl.c_str(), is_temp, disable_log );
}

int Script::Bind( const char* module_name, const char* func_name, const char* decl, bool is_temp, bool disable_log /* = false */ )
{
    #ifdef SCRIPT_MULTITHREADING
    SCOPE_LOCK( BindedFunctionsLocker );
    #endif

    // Detect extension/native dll
    if( !Str::Substring( module_name, ".extension" ) && !Str::Substring( module_name, ".dll" ) )
    {
        // Find module
        asIScriptModule* module = Engine->GetModule( module_name, asGM_ONLY_IF_EXISTS );
        if( !module )
        {
            if( !disable_log )
                WriteLogF( _FUNC_, " - Module<%s> not found.\n", module_name );
            return 0;
        }

        // Find function
        char decl_[MAX_FOTEXT];
        if( decl && decl[0] )
            Str::Format( decl_, decl, func_name );
        else
            Str::Copy( decl_, func_name );
        asIScriptFunction* script_func = module->GetFunctionByDecl( decl_ );
        if( !script_func )
        {
            if( !disable_log )
                WriteLogF( _FUNC_, " - Function<%s> in module<%s> not found.\n", decl_, module_name );
            return 0;
        }

        // Save to temporary bind
        if( is_temp )
        {
            BindedFunctions[1].IsScriptCall = true;
            BindedFunctions[1].ScriptFunc = script_func;
            BindedFunctions[1].NativeFuncAddr = 0;
            return 1;
        }

        // Find already binded
        for( int i = 2, j = (int)BindedFunctions.size(); i < j; i++ )
        {
            BindFunction& bf = BindedFunctions[i];
            if( bf.IsScriptCall && bf.ScriptFunc->GetId() == script_func->GetId() )
                return i;
        }

        // Create new bind
        BindedFunctions.push_back( BindFunction( script_func, 0, module_name, func_name, decl ) );
    }
    else
    {
        size_t func = 0;

        if( Str::Substring( module_name, ".extension" ) )
        {
            string extension_name = module_name;
            extension_name.resize( extension_name.size() - 10 ); // ...

            auto extension = Extension::Map.find( extension_name );
            if( extension == Extension::Map.end() )
            {
                if( !disable_log )
                    WriteLogF( _FUNC_, " - Extension<%s> not found\n", extension_name.c_str() );

                return 0;
            }

            func = extension->second->GetFunctionAddress( string( func_name ) );
            if( !func )
            {
                if( !disable_log )
                    WriteLogF( _FUNC_, " - Function<%s> in extension<%s> not found\n", func_name, extension_name.c_str() );

                return 0;
            }
        }
        else
        {
            // Load dynamic library
            void* dll = LoadDynamicLibrary( module_name );
            if( !dll )
            {
                if( !disable_log )
                    WriteLogF( _FUNC_, " - DynamicLibary<%s> not found in scripts folder, error<%s>.\n", module_name, DLL_Error() );
                return 0;
            }

            // Load function
            func = (size_t)DLL_GetAddress( dll, func_name );
            if( !func )
            {
                if( !disable_log )
                    WriteLogF( _FUNC_, " - Function<%s> in DynamicLibrary<%s> not found, error<%s>.\n", func_name, module_name, DLL_Error() );
                return 0;
            }
        }

        // Save to temporary bind
        if( is_temp )
        {
            BindedFunctions[1].IsScriptCall = false;
            BindedFunctions[1].ScriptFunc = NULL;
            BindedFunctions[1].NativeFuncAddr = func;
            return 1;
        }

        // Find already binded
        for( int i = 2, j = (int)BindedFunctions.size(); i < j; i++ )
        {
            BindFunction& bf = BindedFunctions[i];
            if( !bf.IsScriptCall && bf.NativeFuncAddr == func )
                return i;
        }

        // Create new bind
        BindedFunctions.push_back( BindFunction( 0, func, module_name, func_name, decl ) );
    }
    return (int)BindedFunctions.size() - 1;
}

int Script::Bind( const char* script_name, const char* decl, bool is_temp, bool disable_log /* = false */ )
{
    char module_name[MAX_FOTEXT];
    char func_name[MAX_FOTEXT];
    if( !ReparseScriptName( script_name, module_name, func_name, disable_log ) )
    {
        WriteLogF( _FUNC_, " - Parse script name<%s> fail.\n", script_name );
        return 0;
    }
    return Bind( module_name, func_name, decl, is_temp, disable_log );
}

int Script::RebindFunctions()
{
    #ifdef SCRIPT_MULTITHREADING
    SCOPE_LOCK( BindedFunctionsLocker );
    #endif

    int errors = 0;
    for( int i = 2, j = (int)BindedFunctions.size(); i < j; i++ )
    {
        BindFunction& bf = BindedFunctions[i];
        if( bf.IsScriptCall )
        {
            int bind_id = Bind( bf.ModuleName.c_str(), bf.FuncName.c_str(), bf.FuncDecl.c_str(), true );
            if( bind_id <= 0 )
            {
                WriteLogF( _FUNC_, " - Unable to bind function, module<%s>, function<%s>, declaration<%s>.\n", bf.ModuleName.c_str(), bf.FuncName.c_str(), bf.FuncDecl.c_str() );
                bf.ScriptFunc = NULL;
                errors++;
            }
            else
            {
                bf.ScriptFunc = BindedFunctions[1].ScriptFunc;
            }
        }
    }
    return errors;
}

bool Script::ReparseScriptName( const char* script_name, char* module_name, char* func_name, bool disable_log /* = false */ )
{
    if( !script_name || !module_name || !func_name )
    {
        WriteLogF( _FUNC_, " - Some name null ptr.\n" );
        CreateDump( "ReparseScriptName" );
        return false;
    }

    char script[MAX_SCRIPT_NAME * 2 + 2];
    Str::Copy( script, script_name );
    Str::EraseChars( script, ' ' );

    if( Str::Substring( script, "@" ) )
    {
        char* script_ptr = &script[0];
        Str::CopyWord( module_name, script_ptr, '@' );
        Str::GoTo( (char*&)script_ptr, '@', true );
        Str::CopyWord( func_name, script_ptr, '\0' );
    }
    else
    {
        Str::Copy( module_name, MAX_SCRIPT_NAME, GetActiveModuleName() );
        Str::Copy( func_name, MAX_SCRIPT_NAME, script );
    }

    if( !Str::Length( module_name ) || Str::Compare( module_name, "<error>" ) )
    {
        if( !disable_log )
            WriteLogF( _FUNC_, " - Script name parse error, string<%s>.\n", script_name );
        module_name[0] = func_name[0] = 0;
        return false;
    }
    if( !Str::Length( func_name ) )
    {
        if( !disable_log )
            WriteLogF( _FUNC_, " - Function name parse error, string<%s>.\n", script_name );
        module_name[0] = func_name[0] = 0;
        return false;
    }
    return true;
}

string Script::GetBindFuncName( int bind_id )
{
    #ifdef SCRIPT_MULTITHREADING
    SCOPE_LOCK( BindedFunctionsLocker );
    #endif

    if( bind_id <= 0 || bind_id >= (int)BindedFunctions.size() )
    {
        WriteLogF( _FUNC_, " - Wrong bind id<%d>, bind buffer size<%u>.\n", bind_id, BindedFunctions.size() );
        return "";
    }

    BindFunction& bf = BindedFunctions[bind_id];
    string        result;
    result += bf.ModuleName;
    result += "@";
    result += bf.FuncName;
    return result;
}

/************************************************************************/
/* Functions accord                                                     */
/************************************************************************/

const StrVec& Script::GetScriptFuncCache()
{
    return ScriptFuncCache;
}

void Script::ResizeCache( uint count )
{
    ScriptFuncCache.resize( count );
    ScriptFuncBindId.resize( count );
}

uint Script::GetScriptFuncNum( const char* script_name, const char* decl )
{
    #ifdef SCRIPT_MULTITHREADING
    SCOPE_LOCK( BindedFunctionsLocker );
    #endif

    char full_name[MAX_SCRIPT_NAME * 2 + 1];
    char module_name[MAX_SCRIPT_NAME + 1];
    char func_name[MAX_SCRIPT_NAME + 1];
    if( !Script::ReparseScriptName( script_name, module_name, func_name ) )
        return 0;
    Str::Format( full_name, "%s@%s", module_name, func_name );

    string str_cache = full_name;
    str_cache += "|";
    str_cache += decl;

    // Find already cached
    for( uint i = 0, j = (uint)ScriptFuncCache.size(); i < j; i++ )
        if( ScriptFuncCache[i] == str_cache )
            return i + 1;

    // Create new
    int bind_id = Script::Bind( full_name, decl, false );
    if( bind_id <= 0 )
        return 0;

    ScriptFuncCache.push_back( str_cache );
    ScriptFuncBindId.push_back( bind_id );
    return (uint)ScriptFuncCache.size();
}

int Script::GetScriptFuncBindId( uint func_num )
{
    #ifdef SCRIPT_MULTITHREADING
    SCOPE_LOCK( BindedFunctionsLocker );
    #endif

    func_num--;
    if( func_num >= ScriptFuncBindId.size() )
    {
        WriteLogF( _FUNC_, " - Function index<%u> is greater than bind buffer size<%u>.\n", func_num, ScriptFuncBindId.size() );
        return 0;
    }
    return ScriptFuncBindId[func_num];
}

string Script::GetScriptFuncName( uint func_num )
{
    return GetBindFuncName( GetScriptFuncBindId( func_num ) );
}

/************************************************************************/
/* Contexts                                                             */
/************************************************************************/

THREAD bool              ScriptCall = false;
THREAD asIScriptContext* CurrentCtx = NULL;
THREAD size_t            NativeFuncAddr = 0;
THREAD size_t            NativeArgs[256] = { 0 };
THREAD size_t            NativeRetValue[2] = { 0 };   // EAX:EDX
THREAD size_t            CurrentArg = 0;
THREAD int               ExecutionRecursionCounter = 0;

#ifdef SCRIPT_MULTITHREADING
uint       SynchronizeThreadId = 0;
int        SynchronizeThreadCounter = 0;
MutexEvent SynchronizeThreadLocker;
Mutex      SynchronizeThreadLocalLocker;

typedef vector<EndExecutionCallback> EndExecutionCallbackVec;
THREAD EndExecutionCallbackVec* EndExecutionCallbacks;
#endif

void Script::BeginExecution()
{
    #ifdef SCRIPT_MULTITHREADING
    if( !LogicMT )
        return;

    if( !ExecutionRecursionCounter )
    {
        GarbageLocker.EnterCode();

        SyncManager* sync_mngr = SyncManager::GetForCurThread();
        if( !ConcurrentExecution )
        {
            sync_mngr->Suspend();
            ConcurrentExecutionLocker.Lock();
            sync_mngr->PushPriority( 5 );
            sync_mngr->Resume();
        }
        else
        {
            sync_mngr->PushPriority( 5 );
        }
    }
    ExecutionRecursionCounter++;
    #endif
}

void Script::EndExecution()
{
    #ifdef SCRIPT_MULTITHREADING
    if( !LogicMT )
        return;

    ExecutionRecursionCounter--;
    if( !ExecutionRecursionCounter )
    {
        GarbageLocker.LeaveCode();

        if( !ConcurrentExecution )
        {
            ConcurrentExecutionLocker.Unlock();

            SyncManager* sync_mngr = SyncManager::GetForCurThread();
            sync_mngr->PopPriority();
        }
        else
        {
            SyncManager* sync_mngr = SyncManager::GetForCurThread();
            sync_mngr->PopPriority();

            SynchronizeThreadLocalLocker.Lock();
            bool sync_not_closed = (SynchronizeThreadId == Thread::GetCurrentId() );
            if( sync_not_closed )
            {
                SynchronizeThreadId = 0;
                SynchronizeThreadCounter = 0;
                SynchronizeThreadLocker.Allow();                 // Unlock synchronization section
            }
            SynchronizeThreadLocalLocker.Unlock();

            if( sync_not_closed )
            {
                WriteLogF( _FUNC_, " - Synchronization section is not closed in script!\n" );
                sync_mngr->PopPriority();
            }
        }

        if( EndExecutionCallbacks && !EndExecutionCallbacks->empty() )
        {
            for( auto it = EndExecutionCallbacks->begin(), end = EndExecutionCallbacks->end(); it != end; ++it )
            {
                EndExecutionCallback func = *it;
                (func)();
            }
            EndExecutionCallbacks->clear();
        }
    }
    #endif
}

void Script::AddEndExecutionCallback( EndExecutionCallback func )
{
    #ifdef SCRIPT_MULTITHREADING
    if( !LogicMT )
        return;

    if( !EndExecutionCallbacks )
        EndExecutionCallbacks = new EndExecutionCallbackVec();
    auto it = std::find( EndExecutionCallbacks->begin(), EndExecutionCallbacks->end(), func );
    if( it == EndExecutionCallbacks->end() )
        EndExecutionCallbacks->push_back( func );
    #endif
}

bool Script::PrepareContext( int bind_id, const char* call_func, const char* ctx_info )
{
    #ifdef SCRIPT_MULTITHREADING
    if( LogicMT )
        BindedFunctionsLocker.Lock();
    #endif

    if( bind_id <= 0 || bind_id >= (int)BindedFunctions.size() )
    {
        WriteLogF( _FUNC_, " - Invalid bind id<%d>. Context info<%s>.\n", bind_id, ctx_info );
        #ifdef SCRIPT_MULTITHREADING
        if( LogicMT )
            BindedFunctionsLocker.Unlock();
        #endif
        return false;
    }

    BindFunction&      bf = BindedFunctions[bind_id];
    bool               is_script = bf.IsScriptCall;
    asIScriptFunction* script_func = bf.ScriptFunc;
    size_t             func_addr = bf.NativeFuncAddr;

    #ifdef SCRIPT_MULTITHREADING
    if( LogicMT )
        BindedFunctionsLocker.Unlock();
    #endif

    if( is_script )
    {
        if( !script_func )
            return false;

        asIScriptContext* ctx = GetGlobalContext();
        if( !ctx )
            return false;

        BeginExecution();

        Str::Copy( (char*)ctx->GetUserData(), CONTEXT_BUFFER_SIZE, call_func );
        Str::Append( (char*)ctx->GetUserData(), CONTEXT_BUFFER_SIZE, " : " );
        Str::Append( (char*)ctx->GetUserData(), CONTEXT_BUFFER_SIZE, ctx_info );

        int result = ctx->Prepare( script_func );
        if( result < 0 )
        {
            WriteLogF( _FUNC_, " - Prepare error, context name<%s>, bind_id<%d>, func_id<%d>, error<%s>.\n", ctx->GetUserData(), bind_id, script_func->GetId(), GetASReturnCode( result ) );
            GlobalCtxIndex--;
            EndExecution();
            return false;
        }

        CurrentCtx = ctx;
        ScriptCall = true;
    }
    else
    {
        BeginExecution();

        NativeFuncAddr = func_addr;
        ScriptCall = false;
    }

    CurrentArg = 0;
    return true;
}

void Script::SetArgUChar( uint8 value )
{
    if( ScriptCall )
        CurrentCtx->SetArgByte( (asUINT)CurrentArg, value );
    else
        NativeArgs[CurrentArg] = value;
    CurrentArg++;
}

void Script::SetArgUShort( uint16 value )
{
    if( ScriptCall )
        CurrentCtx->SetArgWord( (asUINT)CurrentArg, value );
    else
        NativeArgs[CurrentArg] = value;
    CurrentArg++;
}

void Script::SetArgUInt( uint value )
{
    if( ScriptCall )
        CurrentCtx->SetArgDWord( (asUINT)CurrentArg, value );
    else
        NativeArgs[CurrentArg] = value;
    CurrentArg++;
}

void Script::SetArgUInt64( uint64 value )
{
    if( ScriptCall )
        CurrentCtx->SetArgQWord( (asUINT)CurrentArg, value );
    else
    {
        *( (uint64*)&NativeArgs[CurrentArg] ) = value;
        CurrentArg++;
    }
    CurrentArg++;
}

void Script::SetArgBool( bool value )
{
    if( ScriptCall )
        CurrentCtx->SetArgByte( (asUINT)CurrentArg, value );
    else
        NativeArgs[CurrentArg] = value;
    CurrentArg++;
}

void Script::SetArgFloat( float value )
{
    if( ScriptCall )
        CurrentCtx->SetArgFloat( (asUINT)CurrentArg, value );
    else
        *( (float*)&NativeArgs[CurrentArg] ) = value;
    CurrentArg++;
}

void Script::SetArgDouble( double value )
{
    if( ScriptCall )
        CurrentCtx->SetArgDouble( (asUINT)CurrentArg, value );
    else
    {
        *( (double*)&NativeArgs[CurrentArg] ) = value;
        CurrentArg++;
    }
    CurrentArg++;
}

void Script::SetArgObject( void* value )
{
    if( ScriptCall )
        CurrentCtx->SetArgObject( (asUINT)CurrentArg, value );
    else
        NativeArgs[CurrentArg] = (size_t)value;
    CurrentArg++;
}

void Script::SetArgAddress( void* value )
{
    if( ScriptCall )
        CurrentCtx->SetArgAddress( (asUINT)CurrentArg, value );
    else
        NativeArgs[CurrentArg] = (size_t)value;
    CurrentArg++;
}

// Taken from AS sources
#if defined (FO_MSVC)
uint64 CallCDeclFunction32( const size_t* args, size_t paramSize, size_t func )
#else
uint64 __attribute( (__noinline__) ) CallCDeclFunction32( const size_t * args, size_t paramSize, size_t func )
#endif
{
    volatile uint64 retQW;

    #if defined (FO_MSVC)
    // Copy the data to the real stack. If we fail to do
    // this we may run into trouble in case of exceptions.
    __asm
    {
        // We must save registers that are used
        push ecx

        // Clear the FPU stack, in case the called function doesn't do it by itself
        fninit

        // Copy arguments from script
        // stack to application stack
        mov ecx, paramSize
        mov  eax, args
        add  eax, ecx
        cmp  ecx, 0
        je   endcopy
copyloop:
        sub eax, 4
        push dword ptr[eax]
        sub  ecx, 4
        jne  copyloop
endcopy:

        // Call function
        call[func]

        // Pop arguments from stack
        add  esp, paramSize

        // Copy return value from EAX:EDX
        lea  ecx, retQW
            mov[ecx], eax
            mov  4[ecx], edx

        // Restore registers
        pop  ecx
    }
    #elif defined (FO_GCC)
    // It is not possible to rely on ESP or BSP to refer to variables or arguments on the stack
    // depending on compiler settings BSP may not even be used, and the ESP is not always on the
    // same offset from the local variables. Because the code adjusts the ESP register it is not
    // possible to inform the arguments through symbolic names below.

    // It's not also not possible to rely on the memory layout of the function arguments, because
    // on some compiler versions and settings the arguments may be copied to local variables with a
    // different ordering before they are accessed by the rest of the code.

    // I'm copying the arguments into this array where I know the exact memory layout. The address
    // of this array will then be passed to the inline asm in the EDX register.
    volatile size_t a[] = { size_t( args ), size_t( paramSize ), size_t( func ) };

    asm __volatile__ (
        "fninit                 \n"
        "pushl %%ebx            \n"
        "movl  %%edx, %%ebx     \n"

        // Need to align the stack pointer so that it is aligned to 16 bytes when making the function call.
        // It is assumed that when entering this function, the stack pointer is already aligned, so we need
        // to calculate how much we will put on the stack during this call.
        "movl  4(%%ebx), %%eax  \n"         // paramSize
        "addl  $4, %%eax        \n"         // counting esp that we will push on the stack
        "movl  %%esp, %%ecx     \n"
        "subl  %%eax, %%ecx     \n"
        "andl  $15, %%ecx       \n"
        "movl  %%esp, %%eax     \n"
        "subl  %%ecx, %%esp     \n"
        "pushl %%eax            \n"         // Store the original stack pointer

        // Copy all arguments to the stack and call the function
        "movl  4(%%ebx), %%ecx  \n"         // paramSize
        "movl  0(%%ebx), %%eax  \n"         // args
        "addl  %%ecx, %%eax     \n"         // push arguments on the stack
        "cmp   $0, %%ecx        \n"
        "je    endcopy          \n"
        "copyloop:              \n"
        "subl  $4, %%eax        \n"
        "pushl (%%eax)          \n"
        "subl  $4, %%ecx        \n"
        "jne   copyloop         \n"
        "endcopy:               \n"
        "call  *8(%%ebx)        \n"
        "addl  4(%%ebx), %%esp  \n"         // pop arguments

        // Pop the alignment bytes
        "popl  %%esp            \n"
        "popl  %%ebx            \n"

        // Copy EAX:EDX to retQW. As the stack pointer has been
        // restored it is now safe to access the local variable
        "leal  %1, %%ecx        \n"
        "movl  %%eax, 0(%%ecx)  \n"
        "movl  %%edx, 4(%%ecx)  \n"
        :                                   // output
        : "d" (a), "m" (retQW)              // input - pass pointer of args in edx, pass pointer of retQW in memory argument
        : "%eax", "%ecx"                    // clobber
        );
    #endif

    return retQW;
}

bool Script::RunPrepared()
{
    if( ScriptCall )
    {
        asIScriptContext* ctx = CurrentCtx;
        uint              tick = Timer::FastTick();

        if( GlobalCtxIndex == 1 )     // First context from stack, add timing
        {
            SCOPE_LOCK( ActiveGlobalCtxLocker );
            ActiveContexts.push_back( ActiveContext( GlobalCtx, tick ) );
        }

        int result = ctx->Execute();

        GlobalCtxIndex--;
        if( GlobalCtxIndex == 0 )     // All scripts execution complete, erase timing
        {
            SCOPE_LOCK( ActiveGlobalCtxLocker );
            auto it = std::find( ActiveContexts.begin(), ActiveContexts.end(), (asIScriptContext**)GlobalCtx );
            if( it != ActiveContexts.end() )
                ActiveContexts.erase( it );
        }

        uint            delta = Timer::FastTick() - tick;

        asEContextState state = ctx->GetState();
        if( state != asEXECUTION_FINISHED )
        {
            if( state == asEXECUTION_EXCEPTION )
                WriteLog( "Execution of script stopped due to exception.\n" );
            else if( state == asEXECUTION_SUSPENDED )
                WriteLog( "Execution of script stopped due to timeout<%u>.\n", RunTimeoutSuspend );
            else
                WriteLog( "Execution of script stopped due to %s.\n", ContextStatesStr[(int)state] );
            PrintContextCallstack( ctx );           // Name and state of context will be printed in this function
            ctx->Abort();
            EndExecution();
            return false;
        }
        else if( RunTimeoutMessage && delta >= RunTimeoutMessage )
        {
            WriteLog( "Script work time<%u> in context<%s>.\n", delta, ctx->GetUserData() );
        }

        if( result < 0 )
        {
            WriteLogF( _FUNC_, " - Context<%s> execute error<%s>, state<%s>.\n", ctx->GetUserData(), GetASReturnCode( result ), ContextStatesStr[(int)state] );
            EndExecution();
            return false;
        }

        CurrentCtx = ctx;
        ScriptCall = true;
    }
    else
    {
        *( (uint64*)NativeRetValue ) = CallCDeclFunction32( NativeArgs, CurrentArg * 4, NativeFuncAddr );
        ScriptCall = false;
    }

    EndExecution();
    return true;
}

uint Script::GetReturnedUInt()
{
    return ScriptCall ? CurrentCtx->GetReturnDWord() : (uint)NativeRetValue[0];
}

bool Script::GetReturnedBool()
{
    return ScriptCall ? (CurrentCtx->GetReturnByte() != 0) : ( (NativeRetValue[0] & 1) != 0 );
}

void* Script::GetReturnedObject()
{
    return ScriptCall ? CurrentCtx->GetReturnObject() : (void*)NativeRetValue[0];
}

float Script::GetReturnedFloat()
{
    float            f;
    #ifdef FO_MSVC
    __asm fstp dword ptr[f]
    #else // FO_GCC
    asm ("fstps %0 \n" : "=m" (f) );
    #endif
    return f;
}

double Script::GetReturnedDouble()
{
    double           d;
    #ifdef FO_MSVC
    __asm fstp qword ptr[d]
    #else // FO_GCC
    asm ("fstpl %0 \n" : "=m" (d) );
    #endif
    return d;
}

void* Script::GetReturnedRawAddress()
{
    return ScriptCall ? CurrentCtx->GetAddressOfReturnValue() : (void*)NativeRetValue;
}

bool Script::SynchronizeThread()
{
    #ifdef SCRIPT_MULTITHREADING
    if( !ConcurrentExecution )
        return true;

    SynchronizeThreadLocalLocker.Lock();     // Local lock

    if( !SynchronizeThreadId )               // Section is free
    {
        SynchronizeThreadId = Thread::GetCurrentId();
        SynchronizeThreadCounter = 1;
        SynchronizeThreadLocker.Disallow();         // Lock synchronization section
        SynchronizeThreadLocalLocker.Unlock();      // Local unlock

        SyncManager* sync_mngr = SyncManager::GetForCurThread();
        sync_mngr->PushPriority( 10 );
    }
    else if( SynchronizeThreadId == Thread::GetCurrentId() ) // Section busy by current thread
    {
        SynchronizeThreadCounter++;
        SynchronizeThreadLocalLocker.Unlock();               // Local unlock
    }
    else                                                     // Section busy by another thread
    {
        SynchronizeThreadLocalLocker.Unlock();               // Local unlock

        SyncManager* sync_mngr = SyncManager::GetForCurThread();
        sync_mngr->Suspend();                                // Allow other threads take objects
        SynchronizeThreadLocker.Wait();                      // Sleep until synchronization section locked
        sync_mngr->Resume();                                 // Relock busy objects
        return SynchronizeThread();                          // Try enter again
    }
    #endif
    return true;
}

bool Script::ResynchronizeThread()
{
    #ifdef SCRIPT_MULTITHREADING
    if( !ConcurrentExecution )
        return true;

    SynchronizeThreadLocalLocker.Lock();     // Local lock

    if( SynchronizeThreadId == Thread::GetCurrentId() )
    {
        if( --SynchronizeThreadCounter == 0 )
        {
            SynchronizeThreadId = 0;
            SynchronizeThreadLocker.Allow();             // Unlock synchronization section
            SynchronizeThreadLocalLocker.Unlock();       // Local unlock

            SyncManager* sync_mngr = SyncManager::GetForCurThread();
            sync_mngr->PopPriority();
        }
        else
        {
            SynchronizeThreadLocalLocker.Unlock();             // Local unlock
        }
    }
    else
    {
        // Invalid call
        SynchronizeThreadLocalLocker.Unlock();         // Local unlock
        return false;
    }
    #endif
    return true;
}

/************************************************************************/
/* Logging                                                              */
/************************************************************************/

bool Script::StartLog()
{
    if( EngineLogFile )
        return true;
    EngineLogFile = FileOpen( DIR_SLASH_SD "FOscript.log", true, true );
    if( !EngineLogFile )
        return false;
    LogA( "Start logging script system.\n" );
    return true;
}

void Script::EndLog()
{
    if( !EngineLogFile )
        return;
    LogA( "End logging script system.\n" );
    FileClose( EngineLogFile );
    EngineLogFile = NULL;
}

void Script::Log( const char* str )
{
    asIScriptContext* ctx = asGetActiveContext();
    if( !ctx )
    {
        LogA( str );
        return;
    }
    asIScriptFunction* func = ctx->GetFunction( 0 );
    if( !func )
    {
        LogA( str );
        return;
    }
    if( LogDebugInfo )
    {
        int line, column;
        line = ctx->GetLineNumber( 0, &column );
        LogA( Str::FormatBuf( "Script callback: %s : %s : %s : %d, %d : %s.\n", str, func->GetModuleName(), func->GetDeclaration( true ), line, column, ctx->GetUserData() ) );
    }
    else
        LogA( Str::FormatBuf( "%s : %s\n", func->GetModuleName(), str ) );
}

void Script::LogA( const char* str )
{
    WriteLog( "%s", str );
    if( !EngineLogFile )
        return;
    FileWrite( EngineLogFile, str, Str::Length( str ) );
}

void Script::LogError( const char* call_func, const char* error )
{
    asIScriptContext* ctx = asGetActiveContext();
    if( !ctx )
    {
        LogA( error );
        return;
    }
    asIScriptFunction* func = ctx->GetFunction( 0 );
    if( !func )
    {
        LogA( error );
        return;
    }
    int line, column;
    line = ctx->GetLineNumber( 0, &column );
    LogA( Str::FormatBuf( "%s : Script error: %s : %s : %s : %d, %d : %s.\n", call_func, error, func->GetModuleName(), func->GetDeclaration( true ), line, column, ctx->GetUserData() ) );
}

void Script::SetLogDebugInfo( bool enabled )
{
    LogDebugInfo = enabled;
}

void Script::CallbackMessage( const asSMessageInfo* msg, void* param )
{
    const char* type = "Error";
    if( msg->type == asMSGTYPE_WARNING )
        type = "Warning";
    else if( msg->type == asMSGTYPE_INFORMATION )
        type = "Info";
    LogA( Str::FormatBuf( "Script message: %s : %s : %s : %d, %d.\n", msg->section, type, msg->message, msg->row, msg->col ) );
}

void Script::CallbackException( asIScriptContext* ctx, void* param )
{
    int                line, column;
    line = ctx->GetExceptionLineNumber( &column );
    asIScriptFunction* func = ctx->GetExceptionFunction();
    if( !func )
    {
        LogA( Str::FormatBuf( "Script exception: %s : %s.\n", ctx->GetExceptionString(), ctx->GetUserData() ) );
        return;
    }
    LogA( Str::FormatBuf( "Script exception: %s : %s : %s : %d, %d : %s.\n", ctx->GetExceptionString(), func->GetModuleName(), func->GetDeclaration( true ), line, column, ctx->GetUserData() ) );
}

/************************************************************************/
/* Array                                                                */
/************************************************************************/

ScriptArray* Script::CreateArray( const char* type )
{
    return new ScriptArray( 0, Engine->GetObjectTypeById( Engine->GetTypeIdByDecl( type ) ) );
}

/************************************************************************/
/*                                                                      */
/************************************************************************/
