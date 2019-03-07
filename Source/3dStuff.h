#ifndef __3D_STUFF__
#define __3D_STUFF__

#include "FlexRect.h"
#include "GraphicStructures.h"
#include "Types.h"

#define LAYERS3D_COUNT              (30)

#define ANIMATION_STAY              (0x01)
#define ANIMATION_ONE_TIME          (0x02)
#define ANIMATION_PERIOD( proc )    (0x04 | ( (proc) << 16 ) )
#define ANIMATION_NO_SMOOTH         (0x08)
#define ANIMATION_INIT              (0x10)

class AnimController;
class AnimSet;
class Animation3d;
class Animation3dEntity;
class Animation3dXFile;

typedef std::vector<Animation3d*>       Animation3dVec;
typedef std::vector<Animation3dEntity*> Animation3dEntityVec;
typedef std::vector<Animation3dXFile*>  Animation3dXFileVec;

struct AnimParams
{
    uint            Id;
    int             Layer;
    int             LayerValue;
    char*           LinkBone;
    char*           ChildFName;
    float           RotX, RotY, RotZ;
    float           MoveX, MoveY, MoveZ;
    float           ScaleX, ScaleY, ScaleZ;
    float           SpeedAjust;
    int*            DisabledLayers;
    uint            DisabledLayersCount;
    int*            DisabledSubsets;
    int*            DisabledSubsetsMesh;
    uint            DisabledSubsetsCount;
    char**          TextureName;
    int*            TextureMesh;
    int*            TextureSubset;
    int*            TextureNum;
    uint            TextureCount;
    EffectInstance* EffectInst;
    int*            EffectInstMesh;
    int*            EffectInstSubset;
    uint            EffectInstCount;
};
typedef std::vector<AnimParams> AnimParamsVec;

struct MeshOptions
{
    Frame*    FramePtr;
    uint      SubsetsCount;
    bool*     DisabledSubsets;
    Texture** TexSubsets;
    Texture** DefaultTexSubsets;
    Effect**  EffectSubsets;
    Effect**  DefaultEffectSubsets;
};
typedef std::vector<MeshOptions> MeshOptionsVec;

class Animation3d
{
private:
    friend class Animation3dEntity;
    friend class Animation3dXFile;
    static Animation3dVec generalAnimations;

    Animation3dEntity*    animEntity;
    AnimController*       animController;
    int                   currentLayers[LAYERS3D_COUNT + 1];       // +1 for actions
    uint                  numAnimationSets;
    uint                  currentTrack;
    uint                  lastTick;
    uint                  endTick;
    Matrix                matRot, matScale;
    Matrix                matScaleBase, matRotBase, matTransBase;
    float                 speedAdjustBase, speedAdjustCur, speedAdjustLink;
    bool                  shadowDisabled;
    float                 dirAngle;
    uint                  sprId;
    Point                 drawXY, bordersXY;
    float                 drawScale;
    Vector                groundPos;
    bool                  bordersDisabled;
    Rect                  baseBorders, fullBorders;
    uint                  calcBordersTick;
    VectorVec             bordersResult;
    bool                  noDraw;
    MeshOptionsVec        meshOpt;
    bool                  useGameTimer;
    float                 animPosProc, animPosTime, animPosPeriod;

    // Derived animations
    Animation3dVec childAnimations;
    Animation3d*   parentAnimation;
    Frame*         parentFrame;
    Matrix         parentMatrix;
    FrameVec       linkFrames;
    MatrixVec      linkMatricles;
    AnimParams     animLink;
    bool           childChecker;

    bool         FrameMove( float elapsed, int x, int y, float scale, bool transform );
    void         UpdateFrameMatrices( Frame* frame, const Matrix* parent_matrix );
    bool         DrawFrame( Frame* frame, bool shadow );
    bool         DrawMeshEffect( MeshSubset* mesh, uint subset, Effect* effect, Texture** textures, EffectValue_ technique );
    bool         IsIntersectFrame( Frame* frame, const Vector& ray_origin, const Vector& ray_dir, float x, float y );
    bool         SetupBordersFrame( Frame* frame, RectF& borders );
    void         ProcessBorders();
    float        GetSpeed();
    uint         GetTick();
    MeshOptions* GetMeshOptions( Frame* frame );
    static void  SetAnimData( Animation3d* anim3d, AnimParams& data, bool clear );

public:
    Animation3d();
    ~Animation3d();

    void SetAnimation( uint anim1, uint anim2, int* layers, int flags );
    bool IsAnimation( uint anim1, uint anim2 );
    bool CheckAnimation( uint& anim1, uint& anim2 );
    int  GetAnim1();
    int  GetAnim2();
    void SetDir( int dir );
    void SetDirAngle( int dir_angle );
    void SetRotation( float rx, float ry, float rz );
    #ifdef SHADOW_MAP
    void SetPitch( float angle );
    #endif
    void SetScale( float sx, float sy, float sz );
    void SetSpeed( float speed );
    void SetTimer( bool use_game_timer );
    void EnableShadow( bool enabled ) { shadowDisabled = !enabled; }
    bool Draw( int x, int y, float scale, RectF* stencil, uint color );
    void SetDrawPos( int x, int y );
    bool IsAnimationPlaying();
    bool IsIntersect( int x, int y );
    void SetSprId( uint value )             { sprId = value; }
    uint GetSprId()                         { return sprId; }
    void EnableSetupBorders( bool enabled ) { bordersDisabled = !enabled; }
    void SetupBorders();
    Rect GetBaseBorders( Point* pivot = NULL );
    Rect GetFullBorders( Point* pivot = NULL );
    Rect GetExtraBorders( Point* pivot = NULL );
    void GetRenderFramesData( float& period, int& proc_from, int& proc_to );

    static bool         StartUp( Device_ device );
    static bool         SetScreenSize( int width, int height );
    static void         Finish();
    static void         BeginScene();
    static void         PreRestore();
    static Animation3d* GetAnimation( const char* name, int path_type, bool is_child );
    static Animation3d* GetAnimation( const char* name, bool is_child );
    static void         AnimateFaster();
    static void         AnimateSlower();
    static PointF       Convert2dTo3d( int x, int y );
    static Point        Convert3dTo2d( float x, float y );
    static bool         Is2dEmulation();
};

class Animation3dEntity
{
private:
    friend class Animation3d;
    friend class Animation3dXFile;
    static Animation3dEntityVec allEntities;

    std::string                 fileName;
    std::string                 pathName;
    Animation3dXFile*           xFile;
    AnimController*             animController;
    uint                        numAnimationSets;
    IntMap                      anim1Equals, anim2Equals;
    IntMap                      animIndexes;
    IntFloatMap                 animSpeed;
    AnimParams                  animDataDefault;
    AnimParamsVec               animData;
    int                         renderAnim;
    int                         renderAnimProcFrom, renderAnimProcTo;
    bool                        shadowDisabled;
    bool                        calcualteTangetSpace;

    void ProcessTemplateDefines( char* str, StrVec& def );
    int  GetAnimationIndex( const char* anim_name );
    int  GetAnimationIndex( uint& anim1, uint& anim2, float* speed );
    int  GetAnimationIndexEx( uint anim1, uint anim2, float* speed );

    bool         Load( const char* name );
    Animation3d* CloneAnimation();

public:
    Animation3dEntity();
    ~Animation3dEntity();
    static Animation3dEntity* GetEntity( const char* name );
};

class Animation3dXFile
{
private:
    friend class Animation3d;
    friend class Animation3dEntity;
    static Animation3dXFileVec xFiles;

    std::string                fileName;
    Frame*                     frameRoot;
    FrameVec                   allFrames;
    FrameVec                   allDrawFrames;

    static Animation3dXFile* GetXFile( const char* xname );
    static bool              SetupFrames( Animation3dXFile* xfile, Frame* frame, Frame* frame_root );
    static void              SetupAnimationOutput( Frame* frame, AnimController* anim_controller );

    Texture* GetTexture( const char* tex_name );
    Effect*  GetEffect( EffectInstance* effect_inst );

public:
    Animation3dXFile();
    ~Animation3dXFile();
};

#endif // __3D_STUFF__
