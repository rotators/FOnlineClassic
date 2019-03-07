#ifndef __SPRITES__
#define __SPRITES__

#include "GraphicStructures.h"
#include "Types.h"

#define SPRITES_POOL_GROW_SIZE    (10000)
#define SPRITES_RESIZE_COUNT      (100)

class Sprite
{
public:
    int      DrawOrderType;
    uint     DrawOrderPos;
    uint     TreeIndex;
    uint     SprId;
    uint*    PSprId;
    int      HexX, HexY;
    int      ScrX, ScrY;
    short*   OffsX, * OffsY;
    int      CutType;
    Sprite*  Parent, * Child;
    float    CutX, CutW, CutTexL, CutTexR;
    uint8*   Alpha;
    uint8*   Light;
    int      EggType;
    int      ContourType;
    uint     ContourColor;
    uint     Color;
    uint     FlashMask;
    Effect** DrawEffect;
    bool*    ValidCallback;
    bool     Valid;

    #ifdef FOCLASSIC_MAPPER
    int CutOyL, CutOyR;
    #endif

    Sprite() { memzero( this, sizeof(Sprite) ); }
    void    Unvalidate();
    Sprite* GetIntersected( int ox, int oy );

    void SetEgg( int egg );
    void SetContour( int contour );
    void SetContour( int contour, uint color );
    void SetColor( uint color );
    void SetAlpha( uint8* alpha );
    void SetFlash( uint mask );
    void SetLight( uint8* light, int maxhx, int maxhy );
};
typedef std::vector<Sprite*> SpriteVec;

class Sprites
{
private:
    // Pool
    static SpriteVec spritesPool;
    static void GrowPool( uint size );
    static void ClearPool();

    // Data
    SpriteVec spritesTree;
    uint      spritesTreeSize;
    Sprite&   PutSprite( uint index, int draw_order, int hx, int hy, int cut, int x, int y, uint id, uint* id_ptr, short* ox, short* oy, uint8* alpha, Effect** effect, bool* callback );

public:
    Sprites() : spritesTreeSize( 0 ) {}
    ~Sprites() { Resize( 0 ); }
    auto    Begin()->SpriteVec::iterator { return spritesTree.begin(); }
    auto    End()->SpriteVec::iterator   { return spritesTree.begin() + spritesTreeSize; }
    uint    Size()                       { return spritesTreeSize; }
    Sprite& AddSprite( int draw_order, int hx, int hy, int cut, int x, int y, uint id, uint* id_ptr, short* ox, short* oy, uint8* alpha, Effect** effect, bool* callback );
    Sprite& InsertSprite( int draw_order, int hx, int hy, int cut, int x, int y, uint id, uint* id_ptr, short* ox, short* oy, uint8* alpha, Effect** effect, bool* callback );
    void    Resize( uint size );
    void    Clear() { Resize( 0 ); }
    void    Unvalidate();
    void    SortBySurfaces();
    void    SortByMapPos();
};

#endif // __SPRITES__
