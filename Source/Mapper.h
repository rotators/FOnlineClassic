#ifndef __MAPPER__
#define __MAPPER__

#include "scriptarray.h"
#include "scriptstring.h"

#include "CritterData.h"
#include "FlexRect.h"
#include "HexManager.h"
#include "IniParser.h"
#include "Item.h"
#include "MsgFiles.h"
#include "ProtoMap.h"
#include "SpriteManager.h"
#include "Types.h"

// Fonts
#define FONT_FO                        (0)
#define FONT_NUM                       (1)
#define FONT_BIG_NUM                   (2)
#define FONT_SAND_NUM                  (3)
#define FONT_SPECIAL                   (4)
#define FONT_DEFAULT                   (5)
#define FONT_THIN                      (6)
#define FONT_FAT                       (7)
#define FONT_BIG                       (8)

typedef std::vector<CritData*> CritDataVec;

class FOMapper
{
public:
    static FOMapper* Self;
    HexManager       HexMngr;
    IniParser        IfaceIni;
    bool             IsMapperStarted;

    FOMapper();
    bool Init();
    int  InitIface();
    bool IfaceLoadRect( Rect& comp, const char* name );
    void Finish();
    void MainLoop();
    void RefreshTiles( int tab );

    void ParseKeyboard();
    void ParseMouse();

    #define DRAW_CR_INFO_MAX           (3)
    int DrawCrExtInfo;

    // Game color
    uint DayTime;
    void ChangeGameTime();

    // MSG File
    LanguagePack CurLang;
    FOMsg*       MsgText, * MsgDlg, * MsgItem, * MsgGame, * MsgGM, * MsgCombat, * MsgQuest, * MsgHolo, * MsgCraft;

    // Map text
    struct MapText
    {
        uint16      HexX, HexY;
        uint        StartTick, Tick;
        std::string Text;
        uint        Color;
        bool        Fade;
        Rect        Pos;
        Rect        EndPos;
        bool operator==( const MapText& r ) { return HexX == r.HexX && HexY == r.HexY; }
    };
    typedef std::vector<MapText> MapTextVec;
    MapTextVec GameMapTexts;

    // Animations
    struct IfaceAnim
    {
        AnyFrames* Frames;
        uint16     Flags;
        uint       CurSpr;
        uint       LastTick;
        int        ResType;

        IfaceAnim( AnyFrames* frm, int res_type );
    };
    typedef std::vector<IfaceAnim*> IfaceAnimVec;

    #define ANIMRUN_TO_END             (0x0001)
    #define ANIMRUN_FROM_END           (0x0002)
    #define ANIMRUN_CYCLE              (0x0004)
    #define ANIMRUN_STOP               (0x0008)
    #define ANIMRUN_SET_FRM( frm )     ( (uint( uint8( (frm) + 1 ) ) ) << 16 )

    IfaceAnimVec Animations;

    uint       AnimLoad( uint name_hash, uint8 dir, int res_type );
    uint       AnimLoad( const char* fname, int path_type, int res_type );
    uint       AnimGetCurSpr( uint anim_id );
    uint       AnimGetCurSprCnt( uint anim_id );
    uint       AnimGetSprCount( uint anim_id );
    AnyFrames* AnimGetFrames( uint anim_id );
    void       AnimRun( uint anim_id, uint flags );
    void       AnimProcess();
    void       AnimFree( int res_type );

    // Cursor
    int CurMode;
    #define CUR_MODE_DEFAULT           (0)
    #define CUR_MODE_MOVE_SELECTION    (1)
    #define CUR_MODE_PLACE_OBJECT      (2)

    AnyFrames* CurPDef, * CurPHand;

    void CurDraw();
    void CurRMouseUp();
    void CurMMouseDown();

    bool IsCurInRect( Rect& rect, int ax, int ay );
    bool IsCurInRect( Rect& rect );
    bool IsCurInRectNoTransp( uint spr_id, Rect& rect, int ax, int ay );
    bool IsCurInInterface();
    bool GetCurHex( uint16& hx, uint16& hy, bool ignore_interface );

    int IntMode;
    #define INT_MODE_CUSTOM0           (0)
    #define INT_MODE_CUSTOM1           (1)
    #define INT_MODE_CUSTOM2           (2)
    #define INT_MODE_CUSTOM3           (3)
    #define INT_MODE_CUSTOM4           (4)
    #define INT_MODE_CUSTOM5           (5)
    #define INT_MODE_CUSTOM6           (6)
    #define INT_MODE_CUSTOM7           (7)
    #define INT_MODE_CUSTOM8           (8)
    #define INT_MODE_CUSTOM9           (9)
    #define INT_MODE_ITEM              (10)
    #define INT_MODE_TILE              (11)
    #define INT_MODE_CRIT              (12)
    #define INT_MODE_FAST              (13)
    #define INT_MODE_IGNORE            (14)
    #define INT_MODE_INCONT            (15)
    #define INT_MODE_MESS              (16)
    #define INT_MODE_LIST              (17)
    #define INT_MODE_COUNT             (18)
    #define TAB_COUNT                  (15)

    int IntHold;
    #define INT_NONE                   (0)
    #define INT_BUTTON                 (1)
    #define INT_MAIN                   (2)
    #define INT_SELECT                 (3)
    #define INT_OBJECT                 (4)
    #define INT_SUB_TAB                (5)

    AnyFrames* IntMainPic, * IntPTab, * IntPSelect, * IntPShow;
    int        IntX, IntY;
    int        IntVectX, IntVectY;
    uint16     SelectHX1, SelectHY1, SelectHX2, SelectHY2;
    int        SelectX, SelectY;

    #define SELECT_TYPE_OLD            (0)
    #define SELECT_TYPE_NEW            (1)
    int  SelectType;

    bool IntVisible, IntFix;

    Rect IntWMain;
    Rect IntWWork, IntWHint;

    Rect IntBCust[10], IntBItem, IntBTile, IntBCrit, IntBFast, IntBIgnore, IntBInCont, IntBMess, IntBList,
         IntBScrBack, IntBScrBackFst, IntBScrFront, IntBScrFrontFst;

    Rect IntBShowItem, IntBShowScen, IntBShowWall, IntBShowCrit, IntBShowTile, IntBShowRoof, IntBShowFast;

    void IntDraw();
    void IntLMouseDown();
    void IntLMouseUp();
    void IntMouseMove();
    void IntSetMode( int mode );

    // Maps
    ProtoMapPtrVec LoadedProtoMaps;
    ProtoMap*      CurProtoMap;

    // Tabs
    #define DEFAULT_SUB_TAB            "000 - all"
    struct SubTab
    {
        ProtoItemVec ItemProtos;
        CritDataVec  NpcProtos;
        StrVec       TileNames;
        UIntVec      TileHashes;
        int          Index, Scroll;
        SubTab() : Index( 0 ), Scroll( 0 ) {}
    };
    typedef std::map<std::string, SubTab> SubTabMap;

    struct TileTab
    {
        StrVec  TileDirs;
        BoolVec TileSubDirs;
    };

    SubTabMap   Tabs[TAB_COUNT];
    SubTab*     TabsActive[TAB_COUNT];
    TileTab     TabsTiles[TAB_COUNT];
    std::string TabsName[INT_MODE_COUNT];
    int         TabsScroll[INT_MODE_COUNT];

    bool        SubTabsActive;
    int         SubTabsActiveTab;
    AnyFrames*  SubTabsPic;
    Rect        SubTabsRect;
    int         SubTabsX, SubTabsY;

    // Prototypes
    ProtoItemVec* CurItemProtos;
    UIntVec*      CurTileHashes;
    StrVec*       CurTileNames;
    CritDataVec*  CurNpcProtos;
    int           NpcDir;
    int*          CurProtoScroll;
    uint          ProtoWidth;
    uint          ProtosOnScreen;
    uint          TabIndex[INT_MODE_COUNT];
    int           InContScroll;
    int           ListScroll;
    MapObject*    InContObject;
    bool          DrawRoof;
    int           TileLayer;

    uint GetTabIndex();
    void SetTabIndex( uint index );
    void RefreshCurProtos();
    bool IsObjectMode() { return CurItemProtos && CurProtoScroll; }
    bool IsTileMode()   { return CurTileHashes && CurTileNames && CurProtoScroll; }
    bool IsCritMode()   { return CurNpcProtos && CurProtoScroll; }

    // Select
    Rect IntBSelectItem, IntBSelectScen, IntBSelectWall, IntBSelectCrit, IntBSelectTile, IntBSelectRoof;
    bool IsSelectItem, IsSelectScen, IsSelectWall, IsSelectCrit, IsSelectTile, IsSelectRoof;

    // Select Map Object
    struct SelMapObj
    {
        MapObject*      MapObj;
        ItemHex*        MapItem;
        CritterCl*      MapNpc;
        MapObjectPtrVec Childs;

        SelMapObj( MapObject* mobj, ItemHex* itm ) : MapObj( mobj ), MapItem( itm ), MapNpc( NULL ) {}
        SelMapObj( MapObject* mobj, CritterCl* npc ) : MapObj( mobj ), MapItem( NULL ), MapNpc( npc ) {}
        SelMapObj( const SelMapObj& r )
        {
            MapObj = r.MapObj;
            MapItem = r.MapItem;
            MapNpc = r.MapNpc;
            Childs = r.Childs;
        }
        SelMapObj& operator=( const SelMapObj& r )
        {
            MapObj = r.MapObj;
            MapItem = r.MapItem;
            MapNpc = r.MapNpc;
            Childs = r.Childs;
            return *this;
        }
        bool operator==( const MapObject* r ) { return MapObj == r; }
        bool IsItem()                         { return MapItem != NULL; }
        bool IsNpc()                          { return MapNpc != NULL; }
        bool IsContainer()                    { return IsNpc() || (IsItem() && MapItem->Proto->Type == ITEM_TYPE_CONTAINER); }
    };
    typedef std::vector<SelMapObj> SelMapProtoItemVec;
    SelMapProtoItemVec SelectedObj;

    // Select Tile, Roof
    struct SelMapTile
    {
        uint16 HexX, HexY;
        bool   IsRoof;

        SelMapTile( uint16 hx, uint16 hy, bool is_roof ) : HexX( hx ), HexY( hy ), IsRoof( is_roof ) {}
        SelMapTile( const SelMapTile& r ) { memcpy( this, &r, sizeof(SelMapTile) ); }
        SelMapTile& operator=( const SelMapTile& r )
        {
            memcpy( this, &r, sizeof(SelMapTile) );
            return *this;
        }
    };
    typedef std::vector<SelMapTile> SelMapTileVec;
    SelMapTileVec SelectedTile;

    // Select methods
    MapObject* FindMapObject( ProtoMap& pmap, uint16 hx, uint16 hy, uint8 mobj_type, uint16 pid, uint skip );
    void       FindMapObjects( ProtoMap& pmap, uint16 hx, uint16 hy, uint radius, uint8 mobj_type, uint16 pid, MapObjectPtrVec& objects );
    MapObject* FindMapObject( uint16 hx, uint16 hy, uint8 mobj_type, uint16 pid, bool skip_selected );
    void       UpdateMapObject( MapObject* mobj );
    void       MoveMapObject( MapObject* mobj, uint16 hx, uint16 hy );
    void       DeleteMapObject( MapObject* mobj );
    void       SelectClear();
    void       SelectAddItem( ItemHex* item );
    void       SelectAddCrit( CritterCl* npc );
    void       SelectAddTile( uint16 hx, uint16 hy, bool is_roof );
    void       SelectAdd( MapObject* mobj, bool select_childs = true );
    void       SelectErase( MapObject* mobj );
    void       SelectAll();
    void       SelectMove( int offs_hx, int offs_hy, int offs_x, int offs_y );
    void       SelectDelete();

    // Parse new
    uint AnyId;
    int  DefaultCritterParam[MAPOBJ_CRITTER_PARAMS];

    MapObject* ParseProto( uint16 pid, uint16 hx, uint16 hy, MapObject* owner, bool is_child = false );
    void       ParseTile( uint name_hash, uint16 hx, uint16 hy, short ox, short oy, uint8 layer, bool is_roof );
    void       ParseNpc( uint16 pid, uint16 hx, uint16 hy );
    MapObject* ParseMapObj( MapObject* mobj );

    // Buffer
    struct TileBuf
    {
        uint   NameHash;
        uint16 HexX, HexY;
        short  OffsX, OffsY;
        uint8  Layer;
        bool   IsRoof;
    };
    typedef std::vector<TileBuf> TileBufVec;

    MapObjectPtrVec MapObjBuffer;
    TileBufVec      TilesBuffer;

    void BufferCopy();
    void BufferCut();
    void BufferPaste( int hx, int hy );

    // Object
    #define DRAW_NEXT_HEIGHT          (12)

    AnyFrames* ObjWMainPic, * ObjPBToAllDn;
    Rect       ObjWMain, ObjWWork, ObjBToAll;
    int        ObjX, ObjY;
    int        ItemVectX, ItemVectY;
    int        ObjCurLine;
    bool       ObjVisible, ObjFix;
    bool       ObjToAll;

    void ObjDraw();
    void ObjKeyDown( uint8 dik, const char* dik_text );
    void ObjKeyDownA( MapObject* o, uint8 dik, const char* dik_text );

    // Console
    AnyFrames*               ConsolePic;
    int                      ConsolePicX, ConsolePicY, ConsoleTextX, ConsoleTextY;
    bool                     ConsoleEdit;
    std::string              ConsoleStr;
    uint                     ConsoleCur;

    std::vector<std::string> ConsoleHistory;
    int                      ConsoleHistoryCur;

    #define CONSOLE_KEY_TICK          (500)
    #define CONSOLE_MAX_ACCELERATE    (460)
    int         ConsoleLastKey;
    std::string ConsoleLastKeyText;
    uint        ConsoleKeyTick;
    int         ConsoleAccelerate;

    void ConsoleDraw();
    void ConsoleKeyDown( uint8 dik, const char* dik_text );
    void ConsoleKeyUp( uint8 dik );
    void ConsoleProcess();
    void ParseCommand( const char* cmd );

    // Mess box
    struct MessBoxMessage
    {
        int         Type;
        std::string Mess;
        std::string Time;

        MessBoxMessage( int type, const char* mess, const char* time ) : Type( type ), Mess( mess ), Time( time ) {}
        MessBoxMessage( const MessBoxMessage& r )
        {
            Type = r.Type;
            Mess = r.Mess;
            Time = r.Time;
        }
        MessBoxMessage& operator=( const MessBoxMessage& r )
        {
            Type = r.Type;
            Mess = r.Mess;
            Time = r.Time;
            return *this;
        }
    };
    typedef std::vector<MessBoxMessage> MessBoxMessageVec;

    MessBoxMessageVec MessBox;
    std::string       MessBoxCurText;
    int               MessBoxScroll;

    void MessBoxGenerate();
    void AddMess( const char* message_text );
    void AddMessFormat( const char* message_text, ... );
    void MessBoxDraw();
    bool SaveLogFile();

    // Extended slots
    struct SlotExt
    {
        uint8 Index;
        char* SlotName;
    };
    typedef std::map<int, SlotExt> SlotExtMap;
    SlotExtMap SlotsExt;

    // Scripts
    static bool SpritesCanDraw;
    void InitScriptSystem();
    void FinishScriptSystem();
    void RunStartScript();
    void DrawIfaceLayer( uint layer );

    struct SScriptFunc
    {
        static ScriptString* MapperObject_get_ScriptName( MapObject& mobj );
        static void          MapperObject_set_ScriptName( MapObject& mobj, ScriptString* str );
        static ScriptString* MapperObject_get_FuncName( MapObject& mobj );
        static void          MapperObject_set_FuncName( MapObject& mobj, ScriptString* str );
        static uint8         MapperObject_get_Critter_Cond( MapObject& mobj );
        static void          MapperObject_set_Critter_Cond( MapObject& mobj, uint8 value );
        static ScriptString* MapperObject_get_PicMap( MapObject& mobj );
        static void          MapperObject_set_PicMap( MapObject& mobj, ScriptString* str );
        static ScriptString* MapperObject_get_PicInv( MapObject& mobj );
        static void          MapperObject_set_PicInv( MapObject& mobj, ScriptString* str );
        static void          MapperObject_Update( MapObject& mobj );
        static MapObject*    MapperObject_AddChild( MapObject& mobj, uint16 pid );
        static uint          MapperObject_GetChilds( MapObject& mobj, ScriptArray* objects );
        static void          MapperObject_MoveToHex( MapObject& mobj, uint16 hx, uint16 hy );
        static void          MapperObject_MoveToHexOffset( MapObject& mobj, int x, int y );
        static void          MapperObject_MoveToDir( MapObject& mobj, uint8 dir );

        static MapObject*    MapperMap_AddObject( ProtoMap& pmap, uint16 hx, uint16 hy, int mobj_type, uint16 pid );
        static MapObject*    MapperMap_GetObject( ProtoMap& pmap, uint16 hx, uint16 hy, int mobj_type, uint16 pid, uint skip );
        static uint          MapperMap_GetObjects( ProtoMap& pmap, uint16 hx, uint16 hy, uint radius, int mobj_type, uint16 pid, ScriptArray* objects );
        static void          MapperMap_UpdateObjects( ProtoMap& pmap );
        static void          MapperMap_Resize( ProtoMap& pmap, uint16 width, uint16 height );
        static uint          MapperMap_GetTilesCount( ProtoMap& pmap, uint16 hx, uint16 hy, bool roof );
        static void          MapperMap_DeleteTile( ProtoMap& pmap, uint16 hx, uint16 hy, bool roof, uint index );
        static uint          MapperMap_GetTileHash( ProtoMap& pmap, uint16 hx, uint16 hy, bool roof, uint index );
        static void          MapperMap_AddTileHash( ProtoMap& pmap, uint16 hx, uint16 hy, int ox, int oy, int layer, bool roof, uint pic_hash );
        static ScriptString* MapperMap_GetTileName( ProtoMap& pmap, uint16 hx, uint16 hy, bool roof, uint index );
        static void          MapperMap_AddTileName( ProtoMap& pmap, uint16 hx, uint16 hy, int ox, int oy, int layer, bool roof, ScriptString* pic_name );
        static uint          MapperMap_GetDayTime( ProtoMap& pmap, uint day_part );
        static void          MapperMap_SetDayTime( ProtoMap& pmap, uint day_part, uint time );
        static void          MapperMap_GetDayColor( ProtoMap& pmap, uint day_part, uint8& r, uint8& g, uint8& b );
        static void          MapperMap_SetDayColor( ProtoMap& pmap, uint day_part, uint8 r, uint8 g, uint8 b );
        static ScriptString* MapperMap_get_ScriptModule( ProtoMap& pmap );
        static void          MapperMap_set_ScriptModule( ProtoMap& pmap, ScriptString* str );
        static ScriptString* MapperMap_get_ScriptFunc( ProtoMap& pmap );
        static void          MapperMap_set_ScriptFunc( ProtoMap& pmap, ScriptString* str );

        static void          Global_SetDefaultCritterParam( uint index, int param );
        static void          Global_AllowSlot( uint8 index, ScriptString& slot_name );
        static uint          Global_DecodeUTF8( ScriptString& text, uint& length );
        static ScriptString* Global_EncodeUTF8( uint ucs );
        static ProtoMap*     Global_LoadMap( ScriptString& file_name, int path_type );
        static void          Global_UnloadMap( ProtoMap* pmap );
        static bool          Global_SaveMap( ProtoMap* pmap, ScriptString& file_name, int path_type );
        static bool          Global_ShowMap( ProtoMap* pmap );
        static int           Global_GetLoadedMaps( ScriptArray* maps );
        static uint          Global_GetMapFileNames( ScriptString* dir, ScriptArray* names );
        static void          Global_DeleteObject( MapObject* mobj );
        static void          Global_DeleteObjects( ScriptArray& objects );
        static void          Global_SelectObject( MapObject* mobj, bool set );
        static void          Global_SelectObjects( ScriptArray& objects, bool set );
        static MapObject*    Global_GetSelectedObject();
        static uint          Global_GetSelectedObjects( ScriptArray* objects );

        static uint Global_TabGetTileDirs( int tab, ScriptArray* dir_names, ScriptArray* include_subdirs );
        static uint Global_TabGetItemPids( int tab, ScriptString* sub_tab, ScriptArray* item_pids );
        static uint Global_TabGetCritterPids( int tab, ScriptString* sub_tab, ScriptArray* critter_pids );
        static void Global_TabSetTileDirs( int tab, ScriptArray* dir_names, ScriptArray* include_subdirs );
        static void Global_TabSetItemPids( int tab, ScriptString* sub_tab, ScriptArray* item_pids );
        static void Global_TabSetCritterPids( int tab, ScriptString* sub_tab, ScriptArray* critter_pids );
        static void Global_TabDelete( int tab );
        static void Global_TabSelect( int tab, ScriptString* sub_tab, bool show );
        static void Global_TabSetName( int tab, ScriptString* tab_name );

        static ProtoItem*    Global_GetProtoItem( uint16 proto_id );
        static void          Global_MoveScreen( uint16 hx, uint16 hy, uint speed );
        static void          Global_MoveHexByDir( uint16& hx, uint16& hy, uint8 dir, uint steps );
        static ScriptString* Global_GetIfaceIniStr( ScriptString& key );
        static uint          Global_GetTick();
        static uint          Global_GetAngelScriptProperty( int property );
        static bool          Global_SetAngelScriptProperty( int property, uint value );
        static uint          Global_GetStrHash( ScriptString* str );
        static bool          Global_LoadDataFile( ScriptString& dat_name );
        static int           Global_GetConstantValue( int const_collection, ScriptString* name );
        static ScriptString* Global_GetConstantName( int const_collection, int value );
        static void          Global_AddConstant( int const_collection, ScriptString* name, int value );
        static bool          Global_LoadConstants( int const_collection, ScriptString* file_name, int path_type );
        static bool          Global_LoadFont( int font, ScriptString& font_fname );
        static void          Global_SetDefaultFont( int font, uint color );
        static void          Global_MouseClick( int x, int y, int button, int cursor );
        static void          Global_KeyboardPress( uint8 key1, uint8 key2, ScriptString* key1_text, ScriptString* key2_text );
        static void          Global_SetRainAnimation( ScriptString* fall_anim_name, ScriptString* drop_anim_name );
        static void          Global_SetZoom( float zoom );

        static ScriptString* Global_GetLastError();
        static void          Global_Log( ScriptString& text );
        static bool          Global_StrToInt( ScriptString* text, int& result );
        static bool          Global_StrToFloat( ScriptString* text, float& result );
        static void          Global_Message( ScriptString& msg );
        static void          Global_MessageMsg( int text_msg, uint str_num );
        static void          Global_MapMessage( ScriptString& text, uint16 hx, uint16 hy, uint ms, uint color, bool fade, int ox, int oy );
        static ScriptString* Global_GetMsgStr( int text_msg, uint str_num );
        static ScriptString* Global_GetMsgStrSkip( int text_msg, uint str_num, uint skip_count );
        static uint          Global_GetMsgStrNumUpper( int text_msg, uint str_num );
        static uint          Global_GetMsgStrNumLower( int text_msg, uint str_num );
        static uint          Global_GetMsgStrCount( int text_msg, uint str_num );
        static bool          Global_IsMsgStr( int text_msg, uint str_num );
        static ScriptString* Global_ReplaceTextStr( ScriptString& text, ScriptString& replace, ScriptString& str );
        static ScriptString* Global_ReplaceTextInt( ScriptString& text, ScriptString& replace, int i );

        static uint       Global_GetDistantion( uint16 hex_x1, uint16 hex_y1, uint16 hex_x2, uint16 hex_y2 );
        static uint8      Global_GetDirection( uint16 from_hx, uint16 from_hy, uint16 to_hx, uint16 to_hy );
        static uint8      Global_GetOffsetDir( uint16 from_hx, uint16 from_hy, uint16 to_hx, uint16 to_hy, float offset );
        static void       Global_GetHexInPath( uint16 from_hx, uint16 from_hy, uint16& to_hx, uint16& to_hy, float angle, uint dist );
        static uint       Global_GetPathLengthHex( uint16 from_hx, uint16 from_hy, uint16 to_hx, uint16 to_hy, uint cut );
        static bool       Global_GetHexPos( uint16 hx, uint16 hy, int& x, int& y );
        static bool       Global_GetMonitorHex( int x, int y, uint16& hx, uint16& hy, bool ignore_interface );
        static MapObject* Global_GetMonitorObject( int x, int y, bool ignore_interface );

        static uint Global_LoadSprite( ScriptString& spr_name, int path_index );
        static uint Global_LoadSpriteHash( uint name_hash, uint8 dir );
        static int  Global_GetSpriteWidth( uint spr_id, int spr_index );
        static int  Global_GetSpriteHeight( uint spr_id, int spr_index );
        static uint Global_GetSpriteCount( uint spr_id );
        static void Global_GetTextInfo( ScriptString& text, int w, int h, int font, int flags, int& tw, int& th, int& lines );
        static void Global_DrawSprite( uint spr_id, int spr_index, int x, int y, uint color );
        static void Global_DrawSpriteOffs( uint spr_id, int spr_index, int x, int y, uint color, bool offs );
        static void Global_DrawSpritePattern( uint spr_id, int spr_index, int x, int y, int w, int h, int spr_width, int spr_height, uint color );
        static void Global_DrawSpriteSize( uint spr_id, int spr_index, int x, int y, int w, int h, bool scratch, bool center, uint color );
        static void Global_DrawSpriteSizeOffs( uint spr_id, int spr_index, int x, int y, int w, int h, bool scratch, bool center, uint color, bool offs );
        static void Global_DrawText( ScriptString& text, int x, int y, int w, int h, uint color, int font, int flags );
        static void Global_DrawPrimitive( int primitive_type, ScriptArray& data );
        static void Global_DrawMapSprite( uint16 hx, uint16 hy, uint16 proto_id, uint spr_id, int spr_index, int ox, int oy );
        static void Global_DrawCritter2d( uint crtype, uint anim1, uint anim2, uint8 dir, int l, int t, int r, int b, bool scratch, bool center, uint color );
        static void Global_DrawCritter3d( uint instance, uint crtype, uint anim1, uint anim2, ScriptArray* layers, ScriptArray* position, uint color );

        static bool          Global_IsCritterCanWalk( uint cr_type );
        static bool          Global_IsCritterCanRun( uint cr_type );
        static bool          Global_IsCritterCanRotate( uint cr_type );
        static bool          Global_IsCritterCanAim( uint cr_type );
        static bool          Global_IsCritterCanArmor( uint cr_type );
        static bool          Global_IsCritterAnim1( uint cr_type, uint index );
        static int           Global_GetCritterAnimType( uint cr_type );
        static uint          Global_GetCritterAlias( uint cr_type );
        static ScriptString* Global_GetCritterTypeName( uint cr_type );
        static ScriptString* Global_GetCritterSoundName( uint cr_type );
    };
};

#endif // __MAPPER__
