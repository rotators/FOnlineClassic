#include "Core.h"

#include "Crypt.h"
#include "FileManager.h"
#include "FileSystem.h"
#include "GameOptions.h"
#include "Log.h"
#include "SpriteManager.h"
#include "Types.h"

using namespace std;

#ifdef FO_D3D
# define SURF_POINT( lr, x, y )    (*( (uint*)( (uint8*)lr.pBits + lr.Pitch * (y) + (x) * 4 ) ) )
#endif

#define FONT_BUF_LEN               (0x5000)
#define FONT_MAX_LINES             (1000)
#define FORMAT_TYPE_DRAW           (0)
#define FORMAT_TYPE_SPLIT          (1)
#define FORMAT_TYPE_LCOUNT         (2)

struct Letter
{
    short PosX;
    short PosY;
    short W;
    short H;
    short OffsX;
    short OffsY;
    short XAdvance;
    RectF TexUV;
    RectF TexBorderedUV;

    Letter() : PosX( 0 ), PosY( 0 ), W( 0 ), H( 0 ), OffsX( 0 ), OffsY( 0 ), XAdvance( 0 ) {};
};
typedef map<uint, Letter>           LetterMap;
typedef map<uint, Letter>::iterator LetterMapIt;

struct FontData
{
    Texture*  FontTex;
    Texture*  FontTexBordered;

    LetterMap Letters;
    int       SpaceWidth;
    int       LineHeight;
    int       YAdvance;
    Effect*   DrawEffect;

    FontData()
    {
        FontTex = NULL;
        FontTexBordered = NULL;
        SpaceWidth = 0;
        LineHeight = 0;
        YAdvance = 0;
        DrawEffect = Effect::Font;
    }
};
typedef vector<FontData*> FontDataVec;

FontDataVec Fonts;

int         DefFontIndex = -1;
uint        DefFontColor = 0;

FontData* GetFont( int num )
{
    if( num < 0 )
        num = DefFontIndex;
    if( num < 0 || num >= (int)Fonts.size() )
        return NULL;
    return Fonts[num];
}

struct FontFormatInfo
{
    FontData* CurFont;
    uint      Flags;
    Rect      Region;
    char      Str[FONT_BUF_LEN];
    char*     PStr;
    uint      LinesAll;
    uint      LinesInRect;
    int       CurX, CurY, MaxCurX;
    uint      ColorDots[FONT_BUF_LEN];
    short     LineWidth[FONT_MAX_LINES];
    uint16    LineSpaceWidth[FONT_MAX_LINES];
    uint      OffsColDots;
    uint      DefColor;
    StrVec*   StrLines;
    bool      IsError;

    void      Init( FontData* font, uint flags, const Rect& region, const char* str_in )
    {
        CurFont = font;
        Flags = flags;
        LinesAll = 1;
        LinesInRect = 0;
        IsError = false;
        CurX = CurY = MaxCurX = 0;
        Region = region;
        memzero( ColorDots, sizeof(ColorDots) );
        memzero( LineWidth, sizeof(LineWidth) );
        memzero( LineSpaceWidth, sizeof(LineSpaceWidth) );
        OffsColDots = 0;
        Str::Copy( Str, str_in );
        PStr = Str;
        DefColor = COLOR_TEXT;
        StrLines = NULL;
    }

    FontFormatInfo& operator=( const FontFormatInfo& _fi )
    {
        CurFont = _fi.CurFont;
        Flags = _fi.Flags;
        LinesAll = _fi.LinesAll;
        LinesInRect = _fi.LinesInRect;
        IsError = _fi.IsError;
        CurX = _fi.CurX;
        CurY = _fi.CurY;
        MaxCurX = _fi.MaxCurX;
        Region = _fi.Region;
        memcpy( Str, _fi.Str, sizeof(Str) );
        memcpy( ColorDots, _fi.ColorDots, sizeof(ColorDots) );
        memcpy( LineWidth, _fi.LineWidth, sizeof(LineWidth) );
        memcpy( LineSpaceWidth, _fi.LineSpaceWidth, sizeof(LineSpaceWidth) );
        OffsColDots = _fi.OffsColDots;
        DefColor = _fi.DefColor;
        PStr = Str;
        StrLines = _fi.StrLines;
        return *this;
    }
};

void SpriteManager::SetDefaultFont( int index, uint color )
{
    DefFontIndex = index;
    DefFontColor = color;
}

void SpriteManager::SetFontEffect( int index, Effect* effect )
{
    FontData* font = GetFont( index );
    if( font )
        font->DrawEffect = (effect ? effect : Effect::Font);
}

bool SpriteManager::BuildFont( int index, void* pfont, const char* image_name, bool make_gray )
{
    FontData& font = *(FontData*)pfont;

    // Load image
    AnyFrames* image = LoadAnimation( image_name, PATH_FONTS );
    if( !image )
    {
        WriteLogF( _FUNC_, " - Image file<%s> not found.\n", image_name );
        return false;
    }

    // Fix texture coordinates
    SpriteInfo* si = SprMngr.GetSpriteInfo( image->GetSprId( 0 ) );
    float       tex_w = (float)si->Surf->Width;
    float       tex_h = (float)si->Surf->Height;
    float       image_x = tex_w * si->SprRect.L;
    float       image_y = tex_h * si->SprRect.T;
    int         max_h = 0;
    for( LetterMapIt it = font.Letters.begin(), end = font.Letters.end(); it != end; ++it )
    {
        Letter& l = (*it).second;
        float   x = (float)l.PosX;
        float   y = (float)l.PosY;
        float   w = (float)l.W;
        float   h = (float)l.H;
        l.TexUV[0] = (image_x + x - 1.0f) / tex_w;
        l.TexUV[1] = (image_y + y - 1.0f) / tex_h;
        l.TexUV[2] = (image_x + x + w + 1.0f) / tex_w;
        l.TexUV[3] = (image_y + y + h + 1.0f) / tex_h;
        if( l.H > max_h )
            max_h = l.H;
    }

    // Fill data
    font.FontTex = si->Surf->TextureOwner;
    if( font.LineHeight == 0 )
        font.LineHeight = max_h;
    if( font.Letters.count( ' ' ) )
        font.SpaceWidth = font.Letters[' '].XAdvance;

    // Create bordered instance
    AnyFrames* image_bordered = LoadAnimation( image_name, PATH_FONTS );
    if( !image_bordered )
    {
        WriteLogF( _FUNC_, " - Can't load twice file<%s>.\n", image_name );
        return false;
    }
    SpriteInfo* si_bordered = SprMngr.GetSpriteInfo( image_bordered->GetSprId( 0 ) );
    font.FontTexBordered = si_bordered->Surf->TextureOwner;

    // Normalize color to gray
    #ifndef FO_D3D
    uint normal_ox = (uint)(tex_w * si->SprRect.L);
    uint normal_oy = (uint)(tex_h * si->SprRect.T);
    uint bordered_ox = (uint)( (float)si_bordered->Surf->Width * si_bordered->SprRect.L );
    uint bordered_oy = (uint)( (float)si_bordered->Surf->Height * si_bordered->SprRect.T );
    if( make_gray )
    {
        for( uint y = 0; y < (uint)si->Height; y++ )
        {
            for( uint x = 0; x < (uint)si->Width; x++ )
            {
                uint8 a = ( (uint8*)&si->Surf->TextureOwner->Pixel( normal_ox + x, normal_oy + y ) )[3];
                if( a )
                {
                    si->Surf->TextureOwner->Pixel( normal_ox + x, normal_oy + y ) = COLOR_ARGB( a, 128, 128, 128 );
                    si_bordered->Surf->TextureOwner->Pixel( bordered_ox + x, bordered_oy + y ) = COLOR_ARGB( a, 128, 128, 128 );
                }
                else
                {
                    si->Surf->TextureOwner->Pixel( normal_ox + x, normal_oy + y ) = COLOR_ARGB( 0, 0, 0, 0 );
                    si_bordered->Surf->TextureOwner->Pixel( bordered_ox + x, bordered_oy + y ) = COLOR_ARGB( 0, 0, 0, 0 );
                }
            }
        }
        Rect r = Rect( normal_ox, normal_oy, normal_ox + si->Width - 1, normal_oy + si->Height - 1 );
        si->Surf->TextureOwner->Update( r );
    }
    #endif

    // Fill border
    #ifdef FO_D3D
    int                x1 = (int)( (float)si->Surf->Width * si->SprRect.L );
    int                y1 = (int)( (float)si->Surf->Height * si->SprRect.T );
    int                x2 = (int)( (float)si_bordered->Surf->Width * si_bordered->SprRect.L );
    int                y2 = (int)( (float)si_bordered->Surf->Height * si_bordered->SprRect.T );
    RECT               to_lock1 = { x1, y1, x1 + si->Width, y1 + si->Height };
    RECT               to_lock2 = { x2, y2, x2 + si_bordered->Width, y2 + si_bordered->Height };
    LPDIRECT3DTEXTURE9 tex;
    uint               bw = si_bordered->Width;
    uint               bh = si_bordered->Height;
    D3D_HR( D3DXCreateTexture( d3dDevice, bw, bh, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex ) );
    LockRect_          lr, lrb;
    D3D_HR( font.FontTex->Instance->LockRect( 0, &lr, &to_lock1, 0 ) );
    D3D_HR( tex->LockRect( 0, &lrb, NULL, 0 ) );
    for( uint i = 0; i < bh; i++ )
        memcpy( (uint8*)lrb.pBits + lrb.Pitch * i, (uint8*)lr.pBits + lr.Pitch * i, bw * 4 );
    for( uint y = 0; y < bh; y++ )
        for( uint x = 0; x < bw; x++ )
            if( SURF_POINT( lr, x, y ) )
                for( int xx = -1; xx <= 1; xx++ )
                    for( int yy = -1; yy <= 1; yy++ )
                        if( !SURF_POINT( lrb, x + xx, y + yy ) )
                            SURF_POINT( lrb, x + xx, y + yy ) = 0xFF000000;
    D3D_HR( font.FontTex->Instance->UnlockRect( 0 ) );
    D3D_HR( font.FontTexBordered->Instance->LockRect( 0, &lr, &to_lock2, 0 ) );
    for( uint i = 0; i < bh; i++ )
        memcpy( (uint8*)lr.pBits + lr.Pitch * i, (uint8*)lrb.pBits + lrb.Pitch * i, bw * 4 );
    D3D_HR( font.FontTexBordered->Instance->UnlockRect( 0 ) );
    D3D_HR( tex->UnlockRect( 0 ) );
    tex->Release();
    #else
    for( uint y = 0; y < (uint)si_bordered->Height; y++ )
    {
        for( uint x = 0; x < (uint)si_bordered->Width; x++ )
        {
            if( si->Surf->TextureOwner->Pixel( normal_ox + x, normal_oy + y ) )
            {
                for( int xx = -1; xx <= 1; xx++ )
                {
                    for( int yy = -1; yy <= 1; yy++ )
                    {
                        uint ox = bordered_ox + x + xx;
                        uint oy = bordered_oy + y + yy;
                        if( !si_bordered->Surf->TextureOwner->Pixel( ox, oy ) )
                            si_bordered->Surf->TextureOwner->Pixel( ox, oy ) = COLOR_XRGB( 0, 0, 0 );
                    }
                }
            }
        }
    }
    Rect r_bordered = Rect( bordered_ox, bordered_oy, bordered_ox + si_bordered->Width - 1, bordered_oy + si_bordered->Height - 1 );
    si_bordered->Surf->TextureOwner->Update( r_bordered );
    #endif

    // Fix texture coordinates on bordered texture
    tex_w = (float)si_bordered->Surf->Width;
    tex_h = (float)si_bordered->Surf->Height;
    image_x = tex_w * si_bordered->SprRect.L;
    image_y = tex_h * si_bordered->SprRect.T;
    for( LetterMapIt it = font.Letters.begin(), end = font.Letters.end(); it != end; ++it )
    {
        Letter& l = (*it).second;
        float   x = (float)l.PosX;
        float   y = (float)l.PosY;
        float   w = (float)l.W;
        float   h = (float)l.H;
        l.TexBorderedUV[0] = (image_x + x - 1.0f) / tex_w;
        l.TexBorderedUV[1] = (image_y + y - 1.0f) / tex_h;
        l.TexBorderedUV[2] = (image_x + x + w + 1.0f) / tex_w;
        l.TexBorderedUV[3] = (image_y + y + h + 1.0f) / tex_h;
    }

    // Register
    if( index >= (int)Fonts.size() )
        Fonts.resize( index + 1 );
    SAFEDEL( Fonts[index] );
    Fonts[index] = new FontData( font );

    return true;
}

bool SpriteManager::LoadFontFO( int index, const char* font_name )
{
    // Load font data
    char        fname[MAX_FOPATH];
    Str::Format( fname, "%s.fofnt", font_name );
    FileManager fm;
    if( !fm.LoadFile( fname, PATH_FONTS ) )
    {
        WriteLogF( _FUNC_, " - File<%s> not found.\n", fname );
        return false;
    }

    FontData font;
    char     image_name[MAX_FOPATH] = { 0 };

    uint     font_cache_len;
    uint8*   font_cache_init = Crypt.GetCache( fname, font_cache_len );
    uint8*   font_cache = font_cache_init;
    uint64   write_time;
    fm.GetTime( NULL, NULL, &write_time );
    if( !font_cache || write_time > *(uint64*)font_cache )
    {
        SAFEDELA( font_cache_init );

        // Parse data
        istrstream str( (char*)fm.GetBuf() );
        char       key[MAX_FOTEXT];
        char       letter_buf[MAX_FOTEXT];
        Letter*    cur_letter = NULL;
        int        version = -1;
        while( !str.eof() && !str.fail() )
        {
            // Get key
            str >> key;

            // Cut off comments
            char* comment = Str::Substring( key, "#" );
            if( comment )
                *comment = 0;
            comment = Str::Substring( key, ";" );
            if( comment )
                *comment = 0;

            // Check version
            if( version == -1 )
            {
                if( !Str::CompareCase( key, "Version" ) )
                {
                    WriteLogF( _FUNC_, " - Font<%s> 'Version' signature not found (used deprecated format of 'fofnt').\n", fname );
                    return false;
                }
                str >> version;
                if( version > 2 )
                {
                    WriteLogF( _FUNC_, " - Font<%s> version<%d> not supported (try update client).\n", fname, version );
                    return false;
                }
                continue;
            }

            // Get value
            if( Str::CompareCase( key, "Image" ) )
            {
                str >> image_name;
            }
            else if( Str::CompareCase( key, "LineHeight" ) )
            {
                str >> font.LineHeight;
            }
            else if( Str::CompareCase( key, "YAdvance" ) )
            {
                str >> font.YAdvance;
            }
            else if( Str::CompareCase( key, "End" ) )
            {
                break;
            }
            else if( Str::CompareCase( key, "Letter" ) )
            {
                str.getline( letter_buf, sizeof(letter_buf) );
                char* utf8_letter_begin = Str::Substring( letter_buf, "'" );
                if( utf8_letter_begin == NULL )
                {
                    WriteLogF( _FUNC_, " - Font<%s> invalid letter specification.\n", fname );
                    return false;
                }
                utf8_letter_begin++;

                uint letter_len;
                uint letter = Str::DecodeUTF8( utf8_letter_begin, &letter_len );
                if( !Str::IsValidUTF8( letter ) )
                {
                    WriteLogF( _FUNC_, " - Font<%s> invalid UTF8 letter at <%s>.\n", fname, letter_buf );
                    return false;
                }

                cur_letter = &font.Letters[letter];
            }

            if( !cur_letter )
                continue;

            if( Str::CompareCase( key, "PositionX" ) )
                str >> cur_letter->PosX;
            else if( Str::CompareCase( key, "PositionY" ) )
                str >> cur_letter->PosY;
            else if( Str::CompareCase( key, "Width" ) )
                str >> cur_letter->W;
            else if( Str::CompareCase( key, "Height" ) )
                str >> cur_letter->H;
            else if( Str::CompareCase( key, "OffsetX" ) )
                str >> cur_letter->OffsX;
            else if( Str::CompareCase( key, "OffsetY" ) )
                str >> cur_letter->OffsY;
            else if( Str::CompareCase( key, "XAdvance" ) )
                str >> cur_letter->XAdvance;
        }

        // Save cache
        uint image_name_len = Str::Length( image_name );
        uint font_cache_len = sizeof(uint64) + sizeof(uint) + image_name_len + sizeof(int) * 2 +
                              sizeof(uint) + (sizeof(uint) + sizeof(short) * 7) * font.Letters.size();
        font_cache_init = new uint8[font_cache_len];
        font_cache = font_cache_init;
        *(uint64*)font_cache = write_time;
        font_cache += sizeof(uint64);
        *(uint*)font_cache = image_name_len;
        font_cache += sizeof(uint);
        memcpy( font_cache, image_name, image_name_len );
        font_cache += image_name_len;
        *(int*)font_cache = font.LineHeight;
        font_cache += sizeof(int);
        *(int*)font_cache = font.YAdvance;
        font_cache += sizeof(int);
        *(uint*)font_cache = (uint)font.Letters.size();
        font_cache += sizeof(uint);
        for( LetterMapIt it = font.Letters.begin(), end = font.Letters.end(); it != end; ++it )
        {
            Letter& l = (*it).second;
            *(int*)font_cache = (*it).first;
            font_cache += sizeof(uint);
            *(short*)font_cache = l.PosX;
            font_cache += sizeof(short);
            *(short*)font_cache = l.PosY;
            font_cache += sizeof(short);
            *(short*)font_cache = l.W;
            font_cache += sizeof(short);
            *(short*)font_cache = l.H;
            font_cache += sizeof(short);
            *(short*)font_cache = l.OffsX;
            font_cache += sizeof(short);
            *(short*)font_cache = l.OffsY;
            font_cache += sizeof(short);
            *(short*)font_cache = l.XAdvance;
            font_cache += sizeof(short);
        }
        Crypt.SetCache( fname, font_cache_init, font_cache_len );
        SAFEDELA( font_cache_init );
    }
    else
    {
        // Load from cache
        font_cache += sizeof(uint64);
        uint image_name_len = *(uint*)font_cache;
        font_cache += sizeof(uint);
        memcpy( image_name, font_cache, image_name_len );
        font_cache += image_name_len;
        image_name[image_name_len] = 0;
        font.LineHeight = *(int*)font_cache;
        font_cache += sizeof(int);
        font.YAdvance = *(int*)font_cache;
        font_cache += sizeof(int);
        uint letters_count = *(uint*)font_cache;
        font_cache += sizeof(uint);
        while( letters_count-- )
        {
            uint    letter = *(uint*)font_cache;
            font_cache += sizeof(uint);
            Letter& l = font.Letters[letter];
            l.PosX = *(short*)font_cache;
            font_cache += sizeof(short);
            l.PosY = *(short*)font_cache;
            font_cache += sizeof(short);
            l.W = *(short*)font_cache;
            font_cache += sizeof(short);
            l.H = *(short*)font_cache;
            font_cache += sizeof(short);
            l.OffsX = *(short*)font_cache;
            font_cache += sizeof(short);
            l.OffsY = *(short*)font_cache;
            font_cache += sizeof(short);
            l.XAdvance = *(short*)font_cache;
            font_cache += sizeof(short);
        }
        SAFEDELA( font_cache_init );
    }

    bool make_gray = false;
    if( image_name[Str::Length( image_name ) - 1] == '*' )
    {
        make_gray = true;
        image_name[Str::Length( image_name ) - 1] = 0;
    }

    return BuildFont( index, &font, image_name, make_gray );
}

bool SpriteManager::LoadFontBMF( int index, const char* font_name )
{
    if( index < 0 )
    {
        WriteLogF( _FUNC_, " - Invalid index.\n" );
        return false;
    }

    FontData    font;
    FileManager fm;
    FileManager fm_tex;

    if( !fm.LoadFile( Str::FormatBuf( "%s.fnt", font_name ), PATH_FONTS ) )
    {
        WriteLogF( _FUNC_, " - Font file<%s> not found.\n", Str::FormatBuf( "%s.fnt", font_name ) );
        return false;
    }

    uint signature = fm.GetLEUInt();
    if( signature != MAKEUINT( 'B', 'M', 'F', 3 ) )
    {
        WriteLogF( _FUNC_, " - Invalid signature of font<%s>.\n", font_name );
        return false;
    }

    // Info
    fm.GetUChar();
    uint block_len = fm.GetLEUInt();
    uint next_block = block_len + fm.GetCurPos() + 1;

    fm.GoForward( 7 );
    if( fm.GetUChar() != 1 || fm.GetUChar() != 1 || fm.GetUChar() != 1 || fm.GetUChar() != 1 )
    {
        WriteLogF( _FUNC_, " - Wrong padding in font<%s>.\n", font_name );
        return false;
    }

    // Common
    fm.SetCurPos( next_block );
    block_len = fm.GetLEUInt();
    next_block = block_len + fm.GetCurPos() + 1;

    int line_height = fm.GetLEUShort();
    int base_height = fm.GetLEUShort();
    fm.GetLEUShort(); // Texture width
    fm.GetLEUShort(); // Texture height

    if( fm.GetLEUShort() != 1 )
    {
        WriteLogF( _FUNC_, " - Texture for font<%s> must be single.\n", font_name );
        return false;
    }

    // Pages
    fm.SetCurPos( next_block );
    block_len = fm.GetLEUInt();
    next_block = block_len + fm.GetCurPos() + 1;

    // Image name
    char image_name[MAX_FOPATH] = { 0 };
    fm.GetStr( image_name );

    // Chars
    fm.SetCurPos( next_block );
    int count = fm.GetLEUInt() / 20;
    for( int i = 0; i < count; i++ )
    {
        // Read data
        uint id = fm.GetLEUInt();
        int  x = fm.GetLEUShort();
        int  y = fm.GetLEUShort();
        int  w = fm.GetLEUShort();
        int  h = fm.GetLEUShort();
        int  ox = fm.GetLEUShort();
        int  oy = fm.GetLEUShort();
        int  xa = fm.GetLEUShort();
        fm.GoForward( 2 );

        // Fill data
        Letter& let = font.Letters[id];
        let.PosX = x + 1;
        let.PosY = y + 1;
        let.W = w - 2;
        let.H = h - 2;
        let.OffsX = -ox;
        let.OffsY = -oy + (line_height - base_height);
        let.XAdvance = xa + 1;

        // BMF to FOFNT convertation
        if( false && index == 11111 )
        {
            if( Fonts[5]->Letters.count( id ) )
                continue;
            char buf[5];
            memzero( buf, sizeof(buf) );
            Str::EncodeUTF8( id, buf );
            void* f = NULL;
            if( !FileExist( "./export.txt" ) )
                f = FileOpen( "./export.txt", true );
            else
                f = FileOpenForAppend( "./export.txt" );
            const char* s = Str::FormatBuf( "Letter '%s'\n", buf );
            FileWrite( f, s, strlen( s ) );
            s = Str::FormatBuf( "  PositionX %d\n", 256 + let.PosX  );
            FileWrite( f, s, strlen( s ) );
            s = Str::FormatBuf( "  PositionY %d\n", let.PosY );
            FileWrite( f, s, strlen( s ) );
            s = Str::FormatBuf( "  Width     %d\n", let.W );
            FileWrite( f, s, strlen( s ) );
            s = Str::FormatBuf( "  Height    %d\n", let.H );
            FileWrite( f, s, strlen( s ) );
            if( let.OffsX )
            {
                s = Str::FormatBuf( "  OffsetX   %d\n", let.OffsX );
                FileWrite( f, s, strlen( s ) );
            }
            if( let.OffsY )
            {
                s = Str::FormatBuf( "  OffsetY   %d\n", let.OffsY );
                FileWrite( f, s, strlen( s ) );
            }
            if( let.XAdvance )
            {
                s = Str::FormatBuf( "  XAdvance  %d\n", let.XAdvance );
                FileWrite( f, s, strlen( s ) );
            }
            s = Str::FormatBuf( "\n" );
            FileWrite( f, s, strlen( s ) );
            FileClose( f );
        }
    }

    font.YAdvance = 1;
    font.LineHeight = base_height;

    return BuildFont( index, &font, image_name, true );
}

void FormatText( FontFormatInfo& fi, int fmt_type )
{
    char*     str = fi.PStr;
    uint      flags = fi.Flags;
    FontData* font = fi.CurFont;
    Rect&     r = fi.Region;
    int&      curx = fi.CurX;
    int&      cury = fi.CurY;
    uint&     offs_col = fi.OffsColDots;

    if( fmt_type != FORMAT_TYPE_DRAW && fmt_type != FORMAT_TYPE_LCOUNT && fmt_type != FORMAT_TYPE_SPLIT )
    {
        WriteLog( "error1\n" );
        fi.IsError = true;
        return;
    }

    if( fmt_type == FORMAT_TYPE_SPLIT && !fi.StrLines )
    {
        WriteLog( "error2\n" );
        fi.IsError = true;
        return;
    }

    // Colorize
    uint* dots = NULL;
    uint  d_offs = 0;
    char* str_ = str;
    char* big_buf = Str::GetBigBuf();
    big_buf[0] = 0;
    if( fmt_type == FORMAT_TYPE_DRAW && !FLAG( flags, FONT_FLAG_NO_COLORIZE ) )
        dots = fi.ColorDots;

    while( *str_ )
    {
        char* s0 = str_;
        Str::GoTo( str_, '|' );
        char* s1 = str_;
        Str::GoTo( str_, ' ' );
        char* s2 = str_;

        // TODO: optimize
        // if(!_str[0] && !*s1) break;
        if( dots )
        {
            uint d_len = (uint)s2 - (uint)s1 + 1;
            uint d = strtoul( s1 + 1, NULL, 0 );

            dots[(uint)s1 - (uint)str - d_offs] = d;
            d_offs += d_len;
        }

        *s1 = 0;
        Str::Append( big_buf, 0x10000, s0 );

        if( !*str_ )
            break;
        str_++;
    }

    Str::Copy( str, FONT_BUF_LEN, big_buf );

    // Skip lines
    uint skip_line = (FLAG( flags, FONT_FLAG_SKIPLINES( 0 ) ) ? flags >> 16 : 0);
    uint skip_line_end = (FLAG( flags, FONT_FLAG_SKIPLINES_END( 0 ) ) ? flags >> 16 : 0);

    // Format
    curx = r.L;
    cury = r.T;
    for( int i = 0, i_advance = 1; str[i]; i += i_advance )
    {
        uint letter_len;
        uint letter = Str::DecodeUTF8( &str[i], &letter_len );
        i_advance = letter_len;

        int x_advance;
        switch( letter )
        {
            case '\r':
                continue;
            case ' ':
                x_advance = font->SpaceWidth;
                break;
            case '\t':
                x_advance = font->SpaceWidth * 4;
                break;
            default:
                LetterMapIt it = font->Letters.find( letter );
                if( it != font->Letters.end() )
                    x_advance = (*it).second.XAdvance;
                else
                    x_advance = 0;
                break;
        }

        if( curx + x_advance > r.R )
        {
            if( curx - r.L > fi.MaxCurX )
                fi.MaxCurX = curx - r.L;

            if( fmt_type == FORMAT_TYPE_DRAW && FLAG( flags, FONT_FLAG_NOBREAK ) )
            {
                str[i] = '\0';
                break;
            }
            else if( FLAG( flags, FONT_FLAG_NOBREAK_LINE ) )
            {
                int j = i;
                for( ; str[j]; j++ )
                    if( str[j] == '\n' )
                        break;

                Str::EraseInterval( &str[i], j - i );
                letter = str[i];
                i_advance = 1;
                if( fmt_type == FORMAT_TYPE_DRAW )
                    for( int k = i, l = MAX_FOTEXT - (j - i); k < l; k++ )
                        fi.ColorDots[k] = fi.ColorDots[k + (j - i)];
            }
            else if( str[i] != '\n' )
            {
                int j = i;
                for( ; j >= 0; j-- )
                {
                    if( str[j] == ' ' || str[j] == '\t' )
                    {
                        str[j] = '\n';
                        i = j;
                        letter = '\n';
                        i_advance = 1;
                        break;
                    }
                    else if( str[j] == '\n' )
                    {
                        j = -1;
                        break;
                    }
                }

                if( j < 0 )
                {
                    letter = '\n';
                    i_advance = 1;
                    Str::Insert( &str[i], "\n" );
                    if( fmt_type == FORMAT_TYPE_DRAW )
                        for( int k = MAX_FOTEXT - 1; k > i; k-- )
                            fi.ColorDots[k] = fi.ColorDots[k - 1];
                }

                if( FLAG( flags, FONT_FLAG_ALIGN ) && !skip_line )
                {
                    fi.LineSpaceWidth[fi.LinesAll - 1] = 1;
                    // Erase next first spaces
                    int ii = i + i_advance;
                    for( j = ii; ; j++ )
                        if( str[j] != ' ' )
                            break;
                    if( j > ii )
                    {
                        Str::EraseInterval( &str[ii], j - ii );
                        if( fmt_type == FORMAT_TYPE_DRAW )
                            for( int k = ii, l = MAX_FOTEXT - (j - ii); k < l; k++ )
                                fi.ColorDots[k] = fi.ColorDots[k + (j - ii)];
                    }
                }
            }
        }

        switch( letter )
        {
            case '\n':
                if( !skip_line )
                {
                    cury += font->LineHeight + font->YAdvance;
                    if( cury + font->LineHeight > r.B && !fi.LinesInRect )
                        fi.LinesInRect = fi.LinesAll;

                    if( fmt_type == FORMAT_TYPE_DRAW )
                    {
                        if( fi.LinesInRect && !FLAG( flags, FONT_FLAG_UPPER ) )
                        {
                            // fi.LinesAll++;
                            str[i] = '\0';
                            break;
                        }
                    }
                    else if( fmt_type == FORMAT_TYPE_SPLIT )
                    {
                        if( fi.LinesInRect && !(fi.LinesAll % fi.LinesInRect) )
                        {
                            str[i] = '\0';
                            (*fi.StrLines).push_back( str );
                            str = &str[i + i_advance];
                            i = -i_advance;
                        }
                    }

                    if( str[i + i_advance] )
                        fi.LinesAll++;
                }
                else
                {
                    skip_line--;
                    Str::EraseInterval( str, i + i_advance );
                    offs_col += i + i_advance;
                    //	if(fmt_type==FORMAT_TYPE_DRAW)
                    //		for(int k=0,l=MAX_FOTEXT-(i+1);k<l;k++) fi.ColorDots[k]=fi.ColorDots[k+i+1];
                    i = 0;
                    i_advance = 0;
                }

                curx = r.L;
                continue;
            case '\0':
                break;
            default:
                curx += x_advance;
                continue;
        }

        if( !str[i] )
            break;
    }
    if( curx - r.L > fi.MaxCurX )
        fi.MaxCurX = curx - r.L;

    if( skip_line_end )
    {
        int len = (int)Str::Length( str );
        for( int i = len - 2; i >= 0; i-- )
        {
            if( str[i] == '\n' )
            {
                str[i] = 0;
                fi.LinesAll--;
                if( !--skip_line_end )
                    break;
            }
        }

        if( skip_line_end )
        {
            WriteLog( "error3\n" );
            fi.IsError = true;
            return;
        }
    }

    if( skip_line )
    {
        WriteLog( "error4\n" );
        fi.IsError = true;
        return;
    }

    if( !fi.LinesInRect )
        fi.LinesInRect = fi.LinesAll;

    if( fi.LinesAll > FONT_MAX_LINES )
    {
        WriteLog( "error5 %u\n", fi.LinesAll );
        fi.IsError = true;
        return;
    }

    if( fmt_type == FORMAT_TYPE_SPLIT )
    {
        (*fi.StrLines).push_back( string( str ) );
        return;
    }
    else if( fmt_type == FORMAT_TYPE_LCOUNT )
    {
        return;
    }

    // Up text
    if( FLAG( flags, FONT_FLAG_UPPER ) && fi.LinesAll > fi.LinesInRect )
    {
        uint j = 0;
        uint line_cur = 0;
        uint last_col = 0;
        for( ; str[j]; ++j )
        {
            if( str[j] == '\n' )
            {
                line_cur++;
                if( line_cur >= (fi.LinesAll - fi.LinesInRect) )
                    break;
            }

            if( fi.ColorDots[j] )
                last_col = fi.ColorDots[j];
        }

        if( !FLAG( flags, FONT_FLAG_NO_COLORIZE ) )
        {
            offs_col += j + 1;
            if( last_col && !fi.ColorDots[j + 1] )
                fi.ColorDots[j + 1] = last_col;
        }

        str = &str[j + 1];
        fi.PStr = str;

        fi.LinesAll = fi.LinesInRect;
    }

    // Align
    curx = r.L;
    cury = r.T;

    for( uint i = 0; i < fi.LinesAll; i++ )
        fi.LineWidth[i] = curx;

    bool can_count = false;
    int  spaces = 0;
    int  curstr = 0;
    for( int i = 0, i_advance = 1; ; i += i_advance )
    {
        uint letter_len;
        uint letter = Str::DecodeUTF8( &str[i], &letter_len );
        i_advance = letter_len;

        switch( letter )
        {
            case ' ':
                curx += font->SpaceWidth;
                if( can_count )
                    spaces++;
                break;
            case '\t':
                curx += font->SpaceWidth * 4;
                break;
            case '\0':
            case '\n':
                fi.LineWidth[curstr] = curx;
                cury += font->LineHeight + font->YAdvance;
                curx = r.L;

                // Erase last spaces
                /*for(int j=i-1;spaces>0 && j>=0;j--)
                   {
                   if(str[j]==' ')
                   {
                   spaces--;
                   str[j]='\r';
                   }
                   else if(str[j]!='\r') break;
                   }*/

                // Align
                if( fi.LineSpaceWidth[curstr] == 1 && spaces > 0 )
                    fi.LineSpaceWidth[curstr] = font->SpaceWidth + (r.R - fi.LineWidth[curstr]) / spaces;
                else
                    fi.LineSpaceWidth[curstr] = font->SpaceWidth;

                curstr++;
                can_count = false;
                spaces = 0;
                break;
            case '\r':
                break;
            default:
                LetterMapIt it = font->Letters.find( letter );
                if( it != font->Letters.end() )
                    curx += (*it).second.XAdvance;
                // if(curx>fi.LineWidth[curstr]) fi.LineWidth[curstr]=curx;
                can_count = true;
                break;
        }

        if( !str[i] )
            break;
    }

    curx = r.L;
    cury = r.T;

    // Align X
    if( FLAG( flags, FONT_FLAG_CENTERX ) )
        curx += (r.R - fi.LineWidth[0]) / 2;
    else if( FLAG( flags, FONT_FLAG_CENTERR ) )
        curx += r.R - fi.LineWidth[0];
    // Align Y
    if( FLAG( flags, FONT_FLAG_CENTERY ) )
        cury = r.T + (r.H() - fi.LinesAll * font->LineHeight - (fi.LinesAll - 1) * font->YAdvance) / 2 + 1;
    else if( FLAG( flags, FONT_FLAG_BOTTOM ) )
        cury = r.B - (fi.LinesAll * font->LineHeight + (fi.LinesAll - 1) * font->YAdvance);
}

bool SpriteManager::DrawStr( const Rect& r, const char* str, uint flags, uint color /* = 0 */, int num_font /* = -1 */ )
{
    // Check
    if( !str || !str[0] )
        return false;

    // Get font
    FontData* font = GetFont( num_font );
    if( !font )
        return false;

    // FormatBuf
    if( !color && DefFontColor )
        color = DefFontColor;

    static FontFormatInfo fi;
    fi.Init( font, flags, r, str );
    fi.DefColor = color;
    FormatText( fi, FORMAT_TYPE_DRAW );
    if( fi.IsError )
        return false;

    char*    str_ = fi.PStr;
    uint     offs_col = fi.OffsColDots;
    int      curx = fi.CurX;
    int      cury = fi.CurY;
    int      curstr = 0;
    Texture* texture = (FLAG( flags, FONT_FLAG_BORDERED ) ? font->FontTexBordered : font->FontTex);

    if( curSprCnt )
        Flush();

    if( !FLAG( flags, FONT_FLAG_NO_COLORIZE ) )
    {
        for( int i = offs_col; i >= 0; i-- )
        {
            if( fi.ColorDots[i] )
            {
                if( fi.ColorDots[i] & 0xFF000000 )
                    color = fi.ColorDots[i];                                              // With alpha
                else
                    color = (color & 0xFF000000) | (fi.ColorDots[i] & 0x00FFFFFF);        // Still old alpha
                break;
            }
        }
    }

    bool variable_space = false;
    for( int i = 0, i_advance = 1; str_[i]; i += i_advance )
    {
        if( !FLAG( flags, FONT_FLAG_NO_COLORIZE ) )
        {
            uint new_color = fi.ColorDots[i + offs_col];
            if( new_color )
            {
                if( new_color & 0xFF000000 )
                    color = new_color;                                            // With alpha
                else
                    color = (color & 0xFF000000) | (new_color & 0x00FFFFFF);      // Still old alpha
            }
        }

        uint letter_len;
        uint letter = Str::DecodeUTF8( &str_[i], &letter_len );
        i_advance = letter_len;

        switch( letter )
        {
            case ' ':
                curx += (variable_space ? fi.LineSpaceWidth[curstr] : font->SpaceWidth);
                continue;
            case '\t':
                curx += font->SpaceWidth * 4;
                continue;
            case '\n':
                cury += font->LineHeight + font->YAdvance;
                curx = r.L;
                curstr++;
                variable_space = false;
                if( FLAG( flags, FONT_FLAG_CENTERX ) )
                    curx += (r.R - fi.LineWidth[curstr]) / 2;
                else if( FLAG( flags, FONT_FLAG_CENTERR ) )
                    curx += r.R - fi.LineWidth[curstr];
                continue;
            case '\r':
                continue;
            default:
                LetterMapIt it = font->Letters.find( letter );
                if( it == font->Letters.end() )
                    continue;

                Letter& l = (*it).second;

                int     mulpos = curSprCnt * 4;
                int     x = curx - l.OffsX - 1;
                int     y = cury - l.OffsY - 1;
                int     w = l.W + 2;
                int     h = l.H + 2;

                RectF&  texture_uv = (FLAG( flags, FONT_FLAG_BORDERED ) ? l.TexBorderedUV : l.TexUV);
                float   x1 = texture_uv[0];
                float   y1 = texture_uv[1];
                float   x2 = texture_uv[2];
                float   y2 = texture_uv[3];

                vBuffer[mulpos].x = (float)x;
                vBuffer[mulpos].y = (float)y + h;
                vBuffer[mulpos].tu = x1;
                vBuffer[mulpos].tv = y2;
                vBuffer[mulpos++].diffuse = color;

                vBuffer[mulpos].x = (float)x;
                vBuffer[mulpos].y = (float)y;
                vBuffer[mulpos].tu = x1;
                vBuffer[mulpos].tv = y1;
                vBuffer[mulpos++].diffuse = color;

                vBuffer[mulpos].x = (float)x + w;
                vBuffer[mulpos].y = (float)y;
                vBuffer[mulpos].tu = x2;
                vBuffer[mulpos].tv = y1;
                vBuffer[mulpos++].diffuse = color;

                vBuffer[mulpos].x = (float)x + w;
                vBuffer[mulpos].y = (float)y + h;
                vBuffer[mulpos].tu = x2;
                vBuffer[mulpos].tv = y2;
                vBuffer[mulpos].diffuse = color;

                if( ++curSprCnt == flushSprCnt )
                {
                    dipQueue.push_back( DipData( texture, font->DrawEffect ) );
                    dipQueue.back().SpritesCount = curSprCnt;
                    Flush();
                }

                curx += l.XAdvance;
                variable_space = true;
        }
    }

    if( curSprCnt )
    {
        dipQueue.push_back( DipData( texture, font->DrawEffect ) );
        dipQueue.back().SpritesCount = curSprCnt;
        Flush();
    }

    return true;
}

int SpriteManager::GetLinesCount( int width, int height, const char* str, int num_font /* = -1 */ )
{
    FontData* font = GetFont( num_font );
    if( !font )
        return 0;

    if( !str )
        return height / (font->LineHeight + font->YAdvance);

    static FontFormatInfo fi;
    fi.Init( font, 0, Rect( 0, 0, width ? width : modeWidth, height ? height : modeHeight ), str );
    FormatText( fi, FORMAT_TYPE_LCOUNT );
    if( fi.IsError )
        return 0;
    return fi.LinesInRect;
}

int SpriteManager::GetLinesHeight( int width, int height, const char* str, int num_font /* = -1 */ )
{
    FontData* font = GetFont( num_font );
    if( !font )
        return 0;
    int cnt = GetLinesCount( width, height, str, num_font );
    if( cnt <= 0 )
        return 0;
    return cnt * font->LineHeight + (cnt - 1) * font->YAdvance;
}

int SpriteManager::GetLineHeight( int num_font /* = -1 */ )
{
    FontData* font = GetFont( num_font );
    if( !font )
        return 0;
    return font->LineHeight;
}

void SpriteManager::GetTextInfo( int width, int height, const char* str, int num_font, int flags, int& tw, int& th, int& lines )
{
    tw = th = lines = 0;
    if( width <= 0 )
        width = GameOpt.ScreenWidth;
    if( height <= 0 )
        height = GameOpt.ScreenHeight;

    FontData* font = GetFont( num_font );
    if( !font )
        return;

    static FontFormatInfo fi;
    fi.Init( font, flags, Rect( 0, 0, width, height ), str );
    FormatText( fi, FORMAT_TYPE_LCOUNT );
    if( fi.IsError )
        return;

    lines = fi.LinesInRect;
    th = fi.LinesInRect * font->LineHeight + (fi.LinesInRect - 1) * font->YAdvance;
    tw = fi.MaxCurX;
}

int SpriteManager::SplitLines( const Rect& r, const char* cstr, int num_font, StrVec& str_vec )
{
    // Check & Prepare
    str_vec.clear();
    if( !cstr || !cstr[0] )
        return 0;

    // Get font
    FontData* font = GetFont( num_font );
    if( !font )
        return 0;
    static FontFormatInfo fi;
    fi.Init( font, 0, r, cstr );
    fi.StrLines = &str_vec;
    FormatText( fi, FORMAT_TYPE_SPLIT );
    if( fi.IsError )
        return 0;
    return (int)str_vec.size();
}

bool SpriteManager::HaveLetter( int num_font, const char* letter )
{
    FontData* font = GetFont( num_font );
    if( !font )
        return false;
    return font->Letters.count( Str::DecodeUTF8( letter, NULL ) ) > 0;
}
