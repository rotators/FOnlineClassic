#ifndef __SPRITE_MANAGER__
#define __SPRITE_MANAGER__

#include "3dStuff.h"
#include "GraphicStructures.h"
#include "Sprites.h"
#include "Types.h"

// Animation loading
#define ANIM_DIR( d )                ( (d) & 0xFF )
#define ANIM_USE_DUMMY               (0x100)
#define ANIM_FRM_ANIM_PIX            (0x200)

#define SPRITE_CUT_CUSTOM            (3)               // Todo

// Egg types
#define EGG_ALWAYS                   (1)
#define EGG_X                        (2)
#define EGG_Y                        (3)
#define EGG_X_AND_Y                  (4)
#define EGG_X_OR_Y                   (5)

// Colors
#define COLOR_IFACE                  SpriteManager::PackColor( ( (COLOR_IFACE_FIX >> 16) & 0xFF ) + GameOpt.Light, ( (COLOR_IFACE_FIX >> 8) & 0xFF ) + GameOpt.Light, (COLOR_IFACE_FIX & 0xFF) + GameOpt.Light )
#define COLOR_GAME_RGB( r, g, b )    SpriteManager::PackColor( (r) + GameOpt.Light, (g) + GameOpt.Light, (b) + GameOpt.Light )
#define COLOR_IFACE_A( a )           ( (COLOR_IFACE ^ 0xFF000000) | ( (a) << 24 ) )
#define COLOR_IFACE_RED              (COLOR_IFACE | (0xFF << 16) )
#define COLOR_IFACE_GREEN            (COLOR_IFACE | (0xFF << 8) )

struct Surface
{
    int      Type;
    Texture* TextureOwner;
    uint     Width, Height;           // Texture size
    uint     BusyH;                   // Height point position
    uint     FreeX, FreeY;            // Busy positions on current surface

    Surface() : Type( 0 ), TextureOwner( NULL ), Width( 0 ), Height( 0 ), BusyH( 0 ), FreeX( 0 ), FreeY( 0 ) {}
    ~Surface() { SAFEDEL( TextureOwner ); }
};
typedef std::vector<Surface*> SurfaceVec;

struct Vertex
{
    #ifdef FO_D3D
    float x, y, z, rhw;
    uint  diffuse;
    float tu, tv;
    float tu2, tv2;
    Vertex() : x( 0 ), y( 0 ), z( 0 ), rhw( 1 ), tu( 0 ), tv( 0 ), tu2( 0 ), tv2( 0 ), diffuse( 0 ) {}
    # define D3DFVF_MYVERTEX         (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX2)
    #else
    float x, y;
    uint  diffuse;
    float tu, tv;
    float tu2, tv2;
    float padding;
    Vertex() : x( 0 ), y( 0 ), diffuse( 0 ), tu( 0 ), tv( 0 ), tu2( 0 ), tv2( 0 ) {}
    #endif
};
typedef std::vector<Vertex> VertexVec;

struct MYVERTEX_PRIMITIVE
{
    float x, y, z, rhw;
    uint  Diffuse;

    MYVERTEX_PRIMITIVE() : x( 0 ), y( 0 ), z( 0 ), rhw( 1 ), Diffuse( 0 ) {}
};
#define D3DFVF_MYVERTEX_PRIMITIVE    (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

struct SpriteInfo
{
    Surface*     Surf;
    RectF        SprRect;
    short        Width;
    short        Height;
    short        OffsX;
    short        OffsY;
    Effect*      DrawEffect;
    Animation3d* Anim3d;
    SpriteInfo() : Surf( NULL ), Width( 0 ), Height( 0 ), OffsX( 0 ), OffsY( 0 ), DrawEffect( NULL ), Anim3d( NULL ) {}
};
typedef std::vector<SpriteInfo*> SprInfoVec;

struct DipData
{
    Texture* SourceTexture;
    Effect*  SourceEffect;
    uint     SpritesCount;
    #ifndef FO_D3D
    RectF    SpriteBorder;
    #endif
    DipData( Texture* tex, Effect* effect ) : SourceTexture( tex ), SourceEffect( effect ), SpritesCount( 1 ) {}
};
typedef std::vector<DipData> DipDataVec;

struct AnyFrames
{
    uint*  Ind;    // Sprite Ids
    short* NextX;  // Offsets
    short* NextY;  // Offsets
    uint   CntFrm; // Frames count
    uint   Ticks;  // Time of playing animation
    uint   Anim1;
    uint   Anim2;

    AnyFrames();
    ~AnyFrames();

    uint  GetSprId( uint num_frm );
    short GetNextX( uint num_frm );
    short GetNextY( uint num_frm );
    uint  GetCnt();
    uint  GetCurSprId();
    uint  GetCurSprIndex();
};
typedef std::map<uint, AnyFrames*, std::less<uint>> AnimMap;
typedef std::vector<AnyFrames*>                     AnimVec;

struct PrepPoint
{
    short  PointX;
    short  PointY;
    short* PointOffsX;
    short* PointOffsY;
    uint   PointColor;

    PrepPoint() : PointX( 0 ), PointY( 0 ), PointColor( 0 ), PointOffsX( NULL ), PointOffsY( NULL ) {}
    PrepPoint( short x, short y, uint color, short* ox = NULL, short* oy = NULL ) : PointX( x ), PointY( y ), PointColor( color ), PointOffsX( ox ), PointOffsY( oy ) {}
};
typedef std::vector<PrepPoint> PointVec;
typedef std::vector<PointVec>  PointVecVec;

struct SpriteMngrParams
{
    void ( * PreRestoreFunc )();
    void ( * PostRestoreFunc )();
};

#ifndef FO_D3D
struct RenderTarget
{
    uint        Id;
    GLuint      FBO;
    Texture*    TargetTexture;
    GLuint      DepthStencilBuffer;
    Effect*     DrawEffect;

    # ifdef FO_WINDOWS
    HPBUFFERARB PBuffer;
    HDC         PBufferDC;
    HGLRC       PBufferGLC;
    # endif
};
typedef std::vector<RenderTarget*> RenderTargetVec;
#endif

class SpriteManager
{
private:
    bool             isInit;
    SpriteMngrParams mngrParams;
    #ifdef FO_D3D
    LPDIRECT3D9      direct3D;
    #else
    float            projectionMatrix[16];
    #endif
    Device_          d3dDevice;
    PresentParams_   presentParams;
    Caps_            deviceCaps;
    int              modeWidth, modeHeight;
    bool             sceneBeginned;
    #ifndef FO_D3D
    RenderTarget     rtMain;
    RenderTarget     rtContours;
    RenderTarget     rtContoursMid;
    RenderTarget     rtPrimitive;
    RenderTarget     rt3D, rt3DMS;
    RenderTarget     rt3DSprite, rt3DMSSprite;
    RenderTargetVec  rtStack;
    # ifdef FO_WINDOWS
    HDC              deviceContext;
    HGLRC            glContext;
    # endif
    #endif

public:
    static AnyFrames* DummyAnimation;

    SpriteManager();
    bool    Init( SpriteMngrParams& params );
    bool    InitBuffers();
    bool    InitRenderStates();
    bool    IsInit() { return isInit; }
    void    Finish();
    Device_ GetDevice() { return d3dDevice; }
    bool    BeginScene( uint clear_color );
    void    EndScene();
    bool    Restore();
    void ( * PreRestore )();
    void ( * PostRestore )();
    #ifndef FO_D3D
    bool CreateRenderTarget( RenderTarget& rt, bool depth_stencil, bool multisampling = false, uint width = 0, uint height = 0, bool tex_linear = false );
    void DeleteRenderTarget( RenderTarget& rt );
    void PushRenderTarget( RenderTarget& rt );
    void PopRenderTarget();
    void DrawRenderTarget( RenderTarget& rt, bool alpha_blend, const Rect* region_from = NULL, const Rect* region_to = NULL );
    void ClearCurrentRenderTarget( uint color );
    void ClearCurrentRenderTargetDS( bool depth, bool stencil );
    void RefreshViewPort();
    #endif

    // Surfaces
public:
    int  SurfType;
    bool SurfFilterNearest;

    void FreeSurfaces( int surf_type );
    void SaveSufaces();
    #ifndef FO_D3D
    void SaveTexture( Texture* tex, const char* fname, bool flip ); // tex == NULL is back buffer
    #endif

private:
    int        baseTextureSize;
    SurfaceVec surfList;

    Surface* CreateNewSurface( int w, int h );
    Surface* FindSurfacePlace( SpriteInfo* si, int& x, int& y );
    uint     FillSurfaceFromMemory( SpriteInfo* si, uint8* data, uint size );

    // Load sprites
public:
    AnyFrames*   LoadAnimation( const char* fname, int path_type, int flags = 0 );
    AnyFrames*   ReloadAnimation( AnyFrames* anim, const char* fname, int path_type );
    Animation3d* LoadPure3dAnimation( const char* fname, int path_type );
    void         FreePure3dAnimation( Animation3d* anim3d );

private:
    SprInfoVec sprData;
    #ifdef FO_D3D
    Surface_   spr3dRT, spr3dRTEx, spr3dDS, spr3dRTData;
    int        spr3dSurfWidth, spr3dSurfHeight;
    #endif

    AnyFrames* CreateAnimation( uint frames, uint ticks );
    AnyFrames* LoadAnimationFrm( const char* fname, int path_type, int dir, bool anim_pix );
    AnyFrames* LoadAnimationRix( const char* fname, int path_type );
    AnyFrames* LoadAnimationFofrm( const char* fname, int path_type, int dir );
    AnyFrames* LoadAnimation3d( const char* fname, int path_type, int dir );
    AnyFrames* LoadAnimationArt( const char* fname, int path_type, int dir );
    AnyFrames* LoadAnimationSpr( const char* fname, int path_type, int dir );
    AnyFrames* LoadAnimationZar( const char* fname, int path_type );
    AnyFrames* LoadAnimationTil( const char* fname, int path_type );
    AnyFrames* LoadAnimationMos( const char* fname, int path_type );
    AnyFrames* LoadAnimationBam( const char* fname, int path_type );
    AnyFrames* LoadAnimationOther( const char* fname, int path_type );
    bool       Render3d( int x, int y, float scale, Animation3d* anim3d, RectF* stencil, uint color );
    bool       Render3dSize( RectF rect, bool stretch_up, bool center, Animation3d* anim3d, RectF* stencil, uint color );
    uint       Render3dSprite( Animation3d* anim3d, int dir, int time_proc );

    // Draw
public:
    static uint PackColor( int r, int g, int b );
    void        SetSpritesColor( uint c ) { baseColor = c; }
    uint        GetSpritesColor()         { return baseColor; }
    SprInfoVec& GetSpritesInfo()          { return sprData; }
    SpriteInfo* GetSpriteInfo( uint id )  { return sprData[id]; }
    void        GetDrawRect( Sprite* prep, Rect& rect );
    uint        GetPixColor( uint spr_id, int offs_x, int offs_y, bool with_zoom = true );
    bool        IsPixNoTransp( uint spr_id, int offs_x, int offs_y, bool with_zoom = true );
    bool        IsEggTransp( int pix_x, int pix_y );

    void PrepareSquare( PointVec& points, Rect r, uint color );
    void PrepareSquare( PointVec& points, Point lt, Point rt, Point lb, Point rb, uint color );
    bool PrepareBuffer( Sprites& dtree, Surface_ surf, int ox, int oy, uint8 alpha );
    bool Flush();

    bool DrawSprite( uint id, int x, int y, uint color = 0 );
    bool DrawSpritePattern( uint id, int x, int y, int w, int h, int spr_width = 0, int spr_height = 0, uint color = 0 );
    bool DrawSpriteSize( uint id, int x, int y, float w, float h, bool stretch_up, bool center, uint color = 0 );
    bool DrawSprites( Sprites& dtree, bool collect_contours, bool use_egg, int draw_oder_from, int draw_oder_to );
    bool DrawPoints( PointVec& points, int prim, float* zoom = NULL, RectF* stencil = NULL, PointF* offset = NULL, Effect* effect = NULL );
    bool Draw3d( int x, int y, float scale, Animation3d* anim3d, RectF* stencil, uint color );
    bool Draw3dSize( RectF rect, bool stretch_up, bool center, Animation3d* anim3d, RectF* stencil, uint color );

    inline bool DrawSprite( AnyFrames* frames, int x, int y, uint color = 0 )
    {
        if( frames && frames != DummyAnimation ) return DrawSprite( frames->GetCurSprId(), x, y, color );
        return false;
    }
    inline bool DrawSpriteSize( AnyFrames* frames, int x, int y, float w, float h, bool stretch_up, bool center, uint color = 0 )
    {
        if( frames && frames != DummyAnimation ) return DrawSpriteSize( frames->GetCurSprId(), x, y, w, h, stretch_up, center, color );
        return false;
    }

private:
    #ifndef FO_D3D
    GLuint        vaMain;
    #endif
    VertexBuffer_ vbMain;
    IndexBuffer_  ibMain;
    IndexBuffer_  ibDirect;
    VertexVec     vBuffer;
    DipDataVec    dipQueue;
    uint          baseColor;
    int           flushSprCnt;           // Max sprites to flush
    int           curSprCnt;             // Current sprites to flush
    #ifdef FO_D3D
    VertexBuffer_ vbPoints;
    int           vbPointsSize;
    #endif

    #ifndef FO_D3D
    void EnableVertexArray( GLuint ib, uint count );
    void DisableVertexArray();
    void EnableStencil( RectF& stencil );
    void DisableStencil( bool clear_stencil );
    #endif

    // Contours
public:
    bool DrawContours();
    void ClearSpriteContours() { createdSpriteContours.clear(); }

private:
    Texture*        contoursTexture;
    Surface_        contoursTextureSurf;
    Texture*        contoursMidTexture;
    Surface_        contoursMidTextureSurf;
    Surface_        contours3dRT;
    PixelShader_*   contoursPS;
    ConstantTable_* contoursCT;
    EffectValue_    contoursConstWidthStep, contoursConstHeightStep,
                    contoursConstSpriteBorders, contoursConstSpriteBordersHeight,
                    contoursConstContourColor, contoursConstContourColorOffs;
    bool    contoursAdded;
    UIntMap createdSpriteContours;
    Sprites spriteContours;

    bool CollectContour( int x, int y, SpriteInfo* si, Sprite* spr );
    #ifdef FO_D3D
    uint GetSpriteContour( SpriteInfo* si, Sprite* spr );
    #endif

    #ifdef FO_D3D
    void WriteContour4( uint* buf, uint buf_w, LockRect_& r, uint w, uint h, uint color );
    void WriteContour8( uint* buf, uint buf_w, LockRect_& r, uint w, uint h, uint color );
    #endif

    // Transparent egg
private:
    bool        eggValid;
    uint16      eggHx, eggHy;
    int         eggX, eggY;
    short*      eggOX, * eggOY;
    SpriteInfo* sprEgg;
    int         eggSprWidth, eggSprHeight;
    float       eggSurfWidth, eggSurfHeight;

public:
    bool CompareHexEgg( uint16 hx, uint16 hy, int egg_type );
    void SetEgg( uint16 hx, uint16 hy, Sprite* spr );
    void EggNotValid() { eggValid = false; }

    // Fonts
private:
    bool BuildFont( int index, void* pfont, const char* image_name, bool make_gray );

public:
    void SetDefaultFont( int index, uint color );
    void SetFontEffect( int index, Effect* effect );
    bool LoadFontFO( int index, const char* font_name );
    bool LoadFontBMF( int index, const char* font_name );
    bool DrawStr( const Rect& r, const char* str, uint flags, uint color = 0, int num_font = -1 );
    int  GetLinesCount( int width, int height, const char* str, int num_font = -1 );
    int  GetLinesHeight( int width, int height, const char* str, int num_font = -1 );
    int  GetLineHeight( int num_font = -1 );
    void GetTextInfo( int width, int height, const char* str, int num_font, int flags, int& tw, int& th, int& lines );
    int  SplitLines( const Rect& r, const char* cstr, int num_font, StrVec& str_vec );
    bool HaveLetter( int num_font, const char* letter );
};

extern SpriteManager SprMngr;

#endif // __SPRITE_MANAGER__
