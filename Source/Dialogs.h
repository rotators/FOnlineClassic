#ifndef __DIALOGS__
#define __DIALOGS__

#include "Defines.h"
#include "MsgFiles.h"
#include "Types.h"

// Misc
#define DIALOG_FILE_EXT            ".fodlg"

// Special script
#define NOT_ANSWER_CLOSE_DIALOG    (0)
#define NOT_ANSWER_BEGIN_BATTLE    (1)

// Dialog flags
#define DIALOG_FLAG_NO_SHUFFLE     (1)

// Answers
#define DIALOG_END                 (0)
#define DIALOG_BACK                (0xFFE1)
#define DIALOG_BARTER              (0xFFE2)
#define DIALOG_ATTACK              (0xFFE3)

// Types
#define DR_NONE                    (0)
#define DR_PARAM                   (1)
#define DR_ITEM                    (2)
#define DR_VAR                     (3)
#define DR_SCRIPT                  (4)
#define DR_LOCK                    (5)
#define DR_NO_RECHECK              (6)
#define DR_OR                      (7)

class DemandResult
{
public:
    char   Type;                // Type of demand or result
    char   Who;                 // Direction ('p' - player, 'n' - npc)
    uint16 ParamId;             // Parameter Id
    bool   NoRecheck;           // Disable demand rechecking
    bool   RetValue;            // Reserved
    char   Op;                  // Operation
    char   ValuesCount;         // Script values count

    #ifdef FONLINE_NPCEDITOR
    std::string ValueStr;       // Main value string
    std::string ParamName;      // Parameter Name
    std::string ValuesNames[5]; // Values names
    #else
    int         Value;          // Main value
    int         ValueExt[5];    // Extra value
    #endif

    DemandResult();
    #ifndef FONLINE_NPCEDITOR
    DemandResult( const DemandResult& r );
    ~DemandResult();
    #endif
};
typedef std::vector<DemandResult> DemandResultVec;

class DialogAnswer
{
public:
    uint            Link;
    uint            TextId;
    DemandResultVec Demands;
    DemandResultVec Results;

    DialogAnswer();
    #ifndef FONLINE_NPCEDITOR
    DialogAnswer( const DialogAnswer& r );
    ~DialogAnswer();
    #endif
};
typedef std::vector<DialogAnswer> AnswersVec;

class Dialog
{
public:
    uint       Id;
    uint       TextId;
    AnswersVec Answers;
    uint       Flags;
    bool       RetVal;

    #ifdef FONLINE_NPCEDITOR
    std::string DlgScript;
    #else
    int         DlgScript;
    #endif

    bool IsNoShuffle() { return Flags & DIALOG_FLAG_NO_SHUFFLE; }

    Dialog() : Id( 0 ), TextId( 0 ), Flags( 0 ), RetVal( false )
        #ifdef FONLINE_NPCEDITOR
            { DlgScript = "None";
            }
        #else
    {
        DlgScript = NOT_ANSWER_CLOSE_DIALOG;
        MEMORY_PROCESS( MEMORY_DIALOG, sizeof(Dialog) );
    }
    Dialog( const Dialog& r )
    {
        *this = r;
        MEMORY_PROCESS( MEMORY_DIALOG, sizeof(Dialog) );
    }
    ~Dialog() { MEMORY_PROCESS( MEMORY_DIALOG, -(int)sizeof(Dialog) ); }
        #endif
        bool operator==( const uint& r ) { return Id == r; }
};
typedef std::vector<Dialog> DialogsVec;

class DialogPack
{
public:
    uint        PackId;
    std::string PackName;
    DialogsVec  Dialogs;
    StrVec      TextsLang;
    FOMsgVec    Texts;
    std::string Comment;

    DialogPack( uint id, std::string name ) : PackId( id ), PackName( name ) {}
};
typedef std::map<uint, DialogPack*> DialogPackMap;

struct Talking
{
    int         TalkType;
    #define TALK_NONE              (0)
    #define TALK_WITH_NPC          (1)
    #define TALK_WITH_HEX          (2)
    uint        TalkNpc;
    uint        TalkHexMap;
    uint16      TalkHexX, TalkHexY;

    uint        DialogPackId;
    Dialog      CurDialog;
    uint        LastDialogId;
    uint        StartTick;
    uint        TalkTime;
    bool        Barter;
    bool        IgnoreDistance;
    std::string Lexems;
    bool        Locked;

    void        Clear()
    {
        TalkType = TALK_NONE;
        TalkNpc = 0;
        TalkHexMap = 0;
        TalkHexX = 0;
        TalkHexY = 0;
        DialogPackId = 0;
        LastDialogId = 0;
        StartTick = 0;
        TalkTime = 0;
        Barter = false;
        IgnoreDistance = false;
        Lexems = "";
        Locked = false;
    }
};

class DialogManager
{
public:
    DialogPackMap DialogsPacks;
    StrUIntMap    DlgPacksNames;
    std::string   LastErrors;

    bool LoadDialogs( const char* list_name );
    // void SaveList( const char* path, const char* list_name );
    void Finish();

    bool AddDialogs( DialogPack* pack );

    DialogPack* GetDialogPack( uint num_pack );
    DialogsVec* GetDialogs( uint num_pack );

    void EraseDialogs( uint num_pack );
    void EraseDialogs( std::string name_pack );

    DialogPack* ParseDialog( const char* name, uint id, const char* data );
    uint16      GetTempVarId( const char* str );

private:
    DemandResult* LoadDemandResult( std::istrstream& input, bool is_demand );
    bool          CheckLockTime( int time );
    int           GetNotAnswerAction( const char* str, bool& ret_val );
    int           GetDRType( const char* str, bool& deprecated );
    bool          CheckOper( char oper );
    bool          CheckWho( char who );
    void          AddError( const char* fmt, ... );
};

extern DialogManager DlgMngr;

#endif // __DIALOGS__
