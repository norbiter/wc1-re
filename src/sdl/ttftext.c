/* Fork-only: --ttf-text draws the game's text in the Space Wing Leader
 * TrueType font at display resolution instead of the 320x200 bitmap fonts.
 *
 * The game draws text straight into the indexed frame, or into a full-screen
 * buffer that is later copied there, and then leaves it for whatever draws
 * next to overwrite.  So the text layer does not try to follow the game's
 * screens.  It watches pixels instead:
 *
 *   Recording.  Every glyph DrawFontGlyph draws is recorded with its cell in
 *   screen coordinates, which pixels are its ink, and the pixels of the cell
 *   before and after the draw.  A glyph redrawn over itself, or erased by
 *   redrawing it in the background colour, takes the earlier record's
 *   "before" pixels, so what it covers is still the original background.
 *   A glyph drawn in exactly the cell of an earlier one takes that record's
 *   slot, since the newer glyph would win there anyway; other new glyphs
 *   take a free slot, and only a full table gives up its oldest record.
 *   Text the game draws once, such as the cockpit's VDU labels, so keeps
 *   its record while readouts redraw themselves every tick.  A glyph whose
 *   ink mostly matches what was already under it does not show, so it is
 *   not recorded and drops the record it was drawn over: the game erases a
 *   message by drawing it again in the background colour, which over the
 *   cockpitless view's black space would otherwise hold indefinitely.
 *
 *   Verification.  When a frame is presented, a glyph counts only while every
 *   pixel it wrote is still on screen.  Anything that drew over it, cleared
 *   the screen or scrolled it away makes it fail, and the native pixels show
 *   as they are.  Newer glyphs win over older ones in the same place.
 *
 *   Replacement.  For each glyph that holds, its ink is taken out of the
 *   presented frame (the "before" pixels go back) and the TrueType glyph is
 *   drawn over the frame in the same palette entry, so it follows fades.
 *
 * Layout.  The TrueType font is about half as wide again as the game's fonts
 * at the same cap height, so it cannot use the original cap height and fit
 * the original line widths.  Each font is scaled so its caps match the game
 * font's, then condensed by up to a fifth to fit the game font's average
 * width; past that it is made smaller as well.  Each word (a run of glyphs
 * drawn side by side) is centred on the extent the game font gave it, and
 * condensed further if it still would not fit, so text never runs past
 * where the original's did and word wrap, alignment and boxes are unchanged.
 *
 * Only printable ASCII drawn in a font's single ink colour is replaced.  The
 * gradient title font, translated or accented glyphs and the FM Towns
 * Japanese text keep their bitmaps. */
#include "wc1.h"

#include <math.h>

#include "video_internal.h"

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/stb/stb_truetype.h"

#define TTF_TEXT_RECORD_CAPACITY 4096
#define TTF_TEXT_CELL_LIMIT 16
#define TTF_TEXT_FIRST 0x21
#define TTF_TEXT_LAST 0x7e
#define TTF_TEXT_GLYPH_COUNT (TTF_TEXT_LAST - TTF_TEXT_FIRST + 1)
/* A record that has not been on screen for this many presents is dropped. */
#define TTF_TEXT_MISS_LIMIT 600
/* Condensing the font by more than this makes it smaller instead. */
#define TTF_TEXT_MAX_CONDENSE 0.8f
#define TTF_TEXT_FACE_CAPACITY 8
#define TTF_TEXT_ATLAS_SIZE 2048
#define TTF_TEXT_ATLAS_PADDING 1

typedef struct TtfTextRecord {
    int used;
    unsigned int serial;
    short x;
    short y;
    short width;
    short height;
    unsigned char character;
    unsigned char colour;
    /* The game font, for grouping words: its address and height. */
    const unsigned char *font;
    /* The game font's cap band within the cell and the width it gives the
     * reference text, for fitting the TrueType font to it. */
    unsigned char capTop;
    unsigned char capRows;
    unsigned short sampleWidth;
    unsigned short missed;
    unsigned char ink[TTF_TEXT_CELL_LIMIT * TTF_TEXT_CELL_LIMIT];
    unsigned char before[TTF_TEXT_CELL_LIMIT * TTF_TEXT_CELL_LIMIT];
    unsigned char after[TTF_TEXT_CELL_LIMIT * TTF_TEXT_CELL_LIMIT];
} TtfTextRecord;

/* One rasterization of the font: a cap height and a horizontal scale, in
 * display pixels, with each glyph's place in the atlas. */
typedef struct TtfTextFace {
    float scaleX;
    float scaleY;
    struct {
        short atlasX;
        short atlasY;
        short width;
        short height;
        short offsetX;
        short offsetY;
    } glyphs[TTF_TEXT_GLYPH_COUNT];
} TtfTextFace;

/* The game's fonts are fitted to this text, a mix of both cases and
 * figures, so one long word does not set the size for the whole font. */
static const char g_szTtfTextSample[] =
    "The Kilrathi fleet: 12 Dralthi, 4 Krant. Launch Hornets, Colonel!";

extern const unsigned int cbTtfTextFont;
extern const unsigned char abTtfTextFont[];

static int g_bTtfTextEnabled;
static int g_bTtfTextFontReady;
static stbtt_fontinfo g_stTtfTextFont;
static int g_anTtfTextAdvance[TTF_TEXT_LAST + 1];
static int g_nTtfTextSampleAdvance;
static float g_fTtfTextCapUnits;

static TtfTextRecord g_astTtfTextRecords[TTF_TEXT_RECORD_CAPACITY];
/* Where the search for a free record starts. */
static int g_nTtfTextNextRecord;
static unsigned int g_uTtfTextSerial;
/* One more than the record most recently drawn at each screen position. */
static unsigned short g_awTtfTextLatestAt[SDL_PORT_FRAME_WIDTH *
                                         SDL_PORT_FRAME_HEIGHT];
/* The glyph DrawFontGlyph is drawing, until it is done. */
static TtfTextRecord g_stTtfTextPending;
static int g_bTtfTextPending;

static unsigned char g_abTtfTextClaimed[SDL_PORT_FRAME_WIDTH *
                                       SDL_PORT_FRAME_HEIGHT];
static unsigned short g_awTtfTextAccepted[TTF_TEXT_RECORD_CAPACITY];
/* The records in use, newest first, for the present being prepared. */
static unsigned short g_awTtfTextNewest[TTF_TEXT_RECORD_CAPACITY];
static SdlTtfTextQuad g_astTtfTextQuads[TTF_TEXT_RECORD_CAPACITY];
static int g_nTtfTextQuadCount;

static TtfTextFace g_astTtfTextFaces[TTF_TEXT_FACE_CAPACITY];
static int g_nTtfTextFaceCount;
static unsigned char *g_pTtfTextAtlas;
static int g_nTtfTextAtlasX;
static int g_nTtfTextAtlasY;
static int g_nTtfTextAtlasRowHeight;
static unsigned int g_uTtfTextAtlasGeneration;

void SdlTtfTextEnable(void)
{
    g_bTtfTextEnabled = 1;
}

static int LoadTtfTextFont(void)
{
    int x0;
    int y0;
    int x1;
    int y1;
    int character;
    const char *sample;

    if (g_bTtfTextFontReady)
        return g_bTtfTextFontReady > 0;
    g_bTtfTextFontReady = -1;
    if (!stbtt_InitFont(&g_stTtfTextFont, abTtfTextFont,
                        stbtt_GetFontOffsetForIndex(abTtfTextFont, 0))) {
        fprintf(stderr, "--ttf-text: the embedded font cannot be read.\n");
        return 0;
    }
    if (!stbtt_GetCodepointBox(&g_stTtfTextFont, 'H', &x0, &y0, &x1, &y1) ||
        y1 <= 0) {
        fprintf(stderr, "--ttf-text: the embedded font has no 'H'.\n");
        return 0;
    }
    g_fTtfTextCapUnits = (float)y1;
    for (character = ' '; character <= TTF_TEXT_LAST; character++) {
        int advance;
        int bearing;

        stbtt_GetCodepointHMetrics(&g_stTtfTextFont, character, &advance,
                                   &bearing);
        g_anTtfTextAdvance[character] = advance;
    }
    g_nTtfTextSampleAdvance = 0;
    for (sample = g_szTtfTextSample; *sample != 0; sample++)
        g_nTtfTextSampleAdvance += g_anTtfTextAdvance[(unsigned char)*sample];
    g_pTtfTextAtlas = calloc(TTF_TEXT_ATLAS_SIZE, TTF_TEXT_ATLAS_SIZE);
    if (g_pTtfTextAtlas == 0)
        return 0;
    g_bTtfTextFontReady = 1;
    return 1;
}

int SdlTtfTextEnabled(void)
{
    return g_bTtfTextEnabled && SdlUsingGlRenderer() &&
           !SdlEgaDitherEnabled() && LoadTtfTextFont();
}

/* The packed font layout DrawFontGlyph reads: the height at 0, the ink and
 * background entries at 2 and 3, widths from 4 and each glyph's offset split
 * over 0x104 (low byte) and 0x204 (high byte). */
static const unsigned char *TtfTextGlyphSource(const unsigned char *font,
                                               unsigned char character)
{
    return font + (((unsigned int)font[0x204 + character] << 8) +
                   font[0x104 + character]);
}

/* The rows of 'H' that hold ink, which is the font's cap band. */
static int MeasureTtfTextCaps(const unsigned char *font, int height,
                              unsigned char *capTop, unsigned char *capRows)
{
    const unsigned char *source = TtfTextGlyphSource(font, 'H');
    int width = font[4 + 'H'];
    int first = -1;
    int last = -1;
    int row;
    int column;

    for (row = 0; row < height; row++) {
        for (column = 0; column < width; column++) {
            unsigned char pixel = source[row * width + column];

            if (pixel != 0xff && pixel != font[3]) {
                if (first < 0)
                    first = row;
                last = row;
                break;
            }
        }
    }
    if (first < 0)
        return 0;
    *capTop = (unsigned char)first;
    *capRows = (unsigned char)(last - first + 1);
    return 1;
}

static unsigned char *TtfTextRow(const TextContext *context, int row)
{
    return context->viewport->pixels + context->viewport->rowOffsets[row] +
           context->cursorX;
}

/* Called with the arguments DrawFontGlyph is about to get. */
void SdlTtfTextBeginGlyph(const TextContext *context, unsigned char character,
                          int height, int width, int y)
{
    const unsigned char *font = context->font;
    const unsigned char *source;
    const char *sample;
    TtfTextRecord *record = &g_stTtfTextPending;
    const TtfTextRecord *earlier;
    unsigned short latest;
    int row;
    int column;
    int inked;
    int sampleWidth;

    g_bTtfTextPending = 0;
    if (!SdlTtfTextEnabled() || character < TTF_TEXT_FIRST ||
        character > TTF_TEXT_LAST || width <= 0 || height <= 0 ||
        width > TTF_TEXT_CELL_LIMIT || height > TTF_TEXT_CELL_LIMIT ||
        context->colour == 0xff || context->viewport == 0)
        return;
    /* Only cells that lie on the screen, in rows the viewport's table
     * addresses the ordinary way. */
    if (context->cursorX < 0 || y < 0 ||
        context->cursorX + width > SDL_PORT_FRAME_WIDTH ||
        y + height > SDL_PORT_FRAME_HEIGHT || y < context->viewport->top ||
        y + height - 1 > context->viewport->bottom + 1)
        return;
    for (row = y; row < y + height; row++) {
        if (context->viewport->rowOffsets[row] < 0)
            return;
    }
    memset(record->ink, 0, sizeof(record->ink));
    source = TtfTextGlyphSource(font, character);
    inked = 0;
    for (row = 0; row < height; row++) {
        for (column = 0; column < width; column++) {
            unsigned char pixel = source[row * width + column];

            if (pixel == 0xff || pixel == font[3])
                continue;
            /* A second ink colour is a shaded font: keep its bitmap. */
            if (pixel != font[2])
                return;
            record->ink[row * TTF_TEXT_CELL_LIMIT + column] = 1;
            inked = 1;
        }
    }
    if (!inked || !MeasureTtfTextCaps(font, height, &record->capTop,
                                      &record->capRows))
        return;
    sampleWidth = 0;
    for (sample = g_szTtfTextSample; *sample != 0; sample++)
        sampleWidth += font[4 + (unsigned char)*sample];
    if (sampleWidth <= 0)
        return;
    record->x = context->cursorX;
    record->y = (short)y;
    record->width = (short)width;
    record->height = (short)height;
    record->character = character;
    record->colour = context->colour;
    record->font = font;
    record->sampleWidth = (unsigned short)sampleWidth;
    record->missed = 0;
    for (row = 0; row < height; row++)
        memcpy(record->before + row * TTF_TEXT_CELL_LIMIT,
               TtfTextRow(context, y + row), (size_t)width);

    /* Drawn over an earlier glyph in the same cell: where that glyph's ink
     * is still untouched, what lies under it is that glyph's background. */
    latest = g_awTtfTextLatestAt[y * SDL_PORT_FRAME_WIDTH + context->cursorX];
    if (latest != 0) {
        earlier = &g_astTtfTextRecords[latest - 1];
        if (earlier->used && earlier->x == record->x &&
            earlier->y == record->y && earlier->width == width &&
            earlier->height == height) {
            for (row = 0; row < height; row++) {
                for (column = 0; column < width; column++) {
                    int pixel = row * TTF_TEXT_CELL_LIMIT + column;

                    if (earlier->ink[pixel] &&
                        record->before[pixel] == earlier->after[pixel])
                        record->before[pixel] = earlier->before[pixel];
                }
            }
        }
    }
    g_bTtfTextPending = 1;
}

/* A free record, or with none free the oldest one. */
static int TakeTtfTextSlot(void)
{
    int step;
    int index;
    int oldest;

    oldest = g_nTtfTextNextRecord;
    for (step = 0; step < TTF_TEXT_RECORD_CAPACITY; step++) {
        index = (g_nTtfTextNextRecord + step) % TTF_TEXT_RECORD_CAPACITY;
        if (!g_astTtfTextRecords[index].used) {
            oldest = index;
            break;
        }
        /* Serials only grow, so the smallest difference is the oldest. */
        if (g_uTtfTextSerial - g_astTtfTextRecords[index].serial >
            g_uTtfTextSerial - g_astTtfTextRecords[oldest].serial)
            oldest = index;
    }
    g_nTtfTextNextRecord = (oldest + 1) % TTF_TEXT_RECORD_CAPACITY;
    return oldest;
}

/* Called once DrawFontGlyph has drawn the glyph. */
void SdlTtfTextEndGlyph(const TextContext *context)
{
    TtfTextRecord *record = &g_stTtfTextPending;
    TtfTextRecord *slot;
    TtfTextRecord *earlier;
    unsigned short latest;
    int row;
    int index;
    int pixel;
    int inkCount;
    int shownCount;

    if (!g_bTtfTextPending)
        return;
    g_bTtfTextPending = 0;
    for (row = 0; row < record->height; row++)
        memcpy(record->after + row * TTF_TEXT_CELL_LIMIT,
               context->viewport->pixels +
                   context->viewport->rowOffsets[record->y + row] +
                   record->x,
               (size_t)record->width);
    inkCount = 0;
    shownCount = 0;
    for (pixel = 0; pixel < TTF_TEXT_CELL_LIMIT * TTF_TEXT_CELL_LIMIT;
         pixel++) {
        if (record->ink[pixel]) {
            inkCount++;
            if (record->after[pixel] != record->before[pixel])
                shownCount++;
        }
    }
    /* A glyph drawn exactly over an earlier one replaces its record: the
     * newer glyph wins that cell, and its "before" pixels already reach
     * under the earlier one. */
    index = -1;
    latest = g_awTtfTextLatestAt[record->y * SDL_PORT_FRAME_WIDTH + record->x];
    if (latest != 0) {
        earlier = &g_astTtfTextRecords[latest - 1];
        if (earlier->used && earlier->x == record->x &&
            earlier->y == record->y && earlier->width == record->width &&
            earlier->height == record->height)
            index = latest - 1;
    }
    /* Ink mostly in the colour already under it, such as a message erased
     * by drawing it again in the background colour, does not show, so it
     * is not recorded.  Over the cockpitless view's black space such a
     * glyph would otherwise hold for as long as space stays black. */
    if (shownCount * 2 < inkCount) {
        if (index >= 0) {
            g_astTtfTextRecords[index].used = 0;
            g_awTtfTextLatestAt[record->y * SDL_PORT_FRAME_WIDTH + record->x] =
                0;
        }
        return;
    }
    if (index < 0)
        index = TakeTtfTextSlot();
    slot = &g_astTtfTextRecords[index];
    /* Taking the slot drops whatever it held. */
    if (slot->used &&
        g_awTtfTextLatestAt[slot->y * SDL_PORT_FRAME_WIDTH + slot->x] ==
            index + 1)
        g_awTtfTextLatestAt[slot->y * SDL_PORT_FRAME_WIDTH + slot->x] = 0;
    *slot = *record;
    slot->used = 1;
    slot->serial = ++g_uTtfTextSerial;
    g_awTtfTextLatestAt[record->y * SDL_PORT_FRAME_WIDTH + record->x] =
        (unsigned short)(index + 1);
}

/* Whether every pixel the glyph wrote (its ink, and its background where it
 * filled one) is still on screen. */
static int TtfTextRecordHolds(const TtfTextRecord *record,
                              const unsigned char *screen)
{
    int row;
    int column;

    for (row = 0; row < record->height; row++) {
        const unsigned char *pixels =
            screen + (record->y + row) * SDL_PORT_FRAME_WIDTH + record->x;
        int base = row * TTF_TEXT_CELL_LIMIT;

        for (column = 0; column < record->width; column++) {
            if ((record->ink[base + column] ||
                 record->before[base + column] != record->after[base + column]) &&
                pixels[column] != record->after[base + column])
                return 0;
        }
    }
    return 1;
}

static int TtfTextCellClaimed(const TtfTextRecord *record)
{
    int row;
    int column;

    for (row = 0; row < record->height; row++) {
        const unsigned char *claimed = g_abTtfTextClaimed +
            (record->y + row) * SDL_PORT_FRAME_WIDTH + record->x;

        for (column = 0; column < record->width; column++) {
            if (claimed[column])
                return 1;
        }
    }
    return 0;
}

static void ClaimTtfTextCell(const TtfTextRecord *record)
{
    int row;

    for (row = 0; row < record->height; row++)
        memset(g_abTtfTextClaimed + (record->y + row) * SDL_PORT_FRAME_WIDTH +
                   record->x,
               1, (size_t)record->width);
}

static void ResetTtfTextAtlas(void)
{
    g_nTtfTextFaceCount = 0;
    g_nTtfTextAtlasX = 0;
    g_nTtfTextAtlasY = 0;
    g_nTtfTextAtlasRowHeight = 0;
    memset(g_pTtfTextAtlas, 0, (size_t)TTF_TEXT_ATLAS_SIZE * TTF_TEXT_ATLAS_SIZE);
    g_uTtfTextAtlasGeneration++;
}

static int ReserveTtfTextAtlas(int width, int height, short *x, short *y)
{
    width += TTF_TEXT_ATLAS_PADDING;
    height += TTF_TEXT_ATLAS_PADDING;
    if (width > TTF_TEXT_ATLAS_SIZE)
        return 0;
    if (g_nTtfTextAtlasX + width > TTF_TEXT_ATLAS_SIZE) {
        g_nTtfTextAtlasX = 0;
        g_nTtfTextAtlasY += g_nTtfTextAtlasRowHeight;
        g_nTtfTextAtlasRowHeight = 0;
    }
    if (g_nTtfTextAtlasY + height > TTF_TEXT_ATLAS_SIZE)
        return 0;
    *x = (short)g_nTtfTextAtlasX;
    *y = (short)g_nTtfTextAtlasY;
    g_nTtfTextAtlasX += width;
    if (height > g_nTtfTextAtlasRowHeight)
        g_nTtfTextAtlasRowHeight = height;
    return 1;
}

static int RasterizeTtfTextFace(TtfTextFace *face)
{
    int character;

    for (character = TTF_TEXT_FIRST; character <= TTF_TEXT_LAST; character++) {
        int x0;
        int y0;
        int x1;
        int y1;
        int index = character - TTF_TEXT_FIRST;

        stbtt_GetCodepointBitmapBox(&g_stTtfTextFont, character, face->scaleX,
                                    face->scaleY, &x0, &y0, &x1, &y1);
        face->glyphs[index].width = (short)(x1 - x0);
        face->glyphs[index].height = (short)(y1 - y0);
        face->glyphs[index].offsetX = (short)x0;
        face->glyphs[index].offsetY = (short)y0;
        if (x1 <= x0 || y1 <= y0)
            continue;
        if (!ReserveTtfTextAtlas(x1 - x0, y1 - y0, &face->glyphs[index].atlasX,
                                 &face->glyphs[index].atlasY))
            return 0;
        stbtt_MakeCodepointBitmap(
            &g_stTtfTextFont,
            g_pTtfTextAtlas + face->glyphs[index].atlasY * TTF_TEXT_ATLAS_SIZE +
                face->glyphs[index].atlasX,
            x1 - x0, y1 - y0, TTF_TEXT_ATLAS_SIZE, face->scaleX, face->scaleY,
            character);
    }
    return 1;
}

/* The face for a scale in display pixels per font unit, rasterized on
 * first use.  The atlas starts over when it is full. */
static const TtfTextFace *FindTtfTextFace(float scaleX, float scaleY)
{
    TtfTextFace *face;
    int index;

    for (index = 0; index < g_nTtfTextFaceCount; index++) {
        face = &g_astTtfTextFaces[index];
        if (fabsf(face->scaleX - scaleX) <= scaleX * 0.002f &&
            fabsf(face->scaleY - scaleY) <= scaleY * 0.002f)
            return face;
    }
    if (g_nTtfTextFaceCount == TTF_TEXT_FACE_CAPACITY)
        ResetTtfTextAtlas();
    face = &g_astTtfTextFaces[g_nTtfTextFaceCount];
    face->scaleX = scaleX;
    face->scaleY = scaleY;
    if (!RasterizeTtfTextFace(face)) {
        ResetTtfTextAtlas();
        face = &g_astTtfTextFaces[0];
        face->scaleX = scaleX;
        face->scaleY = scaleY;
        if (!RasterizeTtfTextFace(face))
            return 0;
    }
    g_nTtfTextFaceCount++;
    g_uTtfTextAtlasGeneration++;
    return face;
}

static int CompareTtfTextNewest(const void *left, const void *right)
{
    const TtfTextRecord *a =
        &g_astTtfTextRecords[*(const unsigned short *)left];
    const TtfTextRecord *b =
        &g_astTtfTextRecords[*(const unsigned short *)right];
    unsigned int ageA = g_uTtfTextSerial - a->serial;
    unsigned int ageB = g_uTtfTextSerial - b->serial;

    return ageA < ageB ? -1 : ageA > ageB;
}

static int CompareTtfTextAccepted(const void *left, const void *right)
{
    const TtfTextRecord *a =
        &g_astTtfTextRecords[*(const unsigned short *)left];
    const TtfTextRecord *b =
        &g_astTtfTextRecords[*(const unsigned short *)right];

    if (a->y != b->y)
        return a->y - b->y;
    return a->x - b->x;
}

/* Lays out one word, records first..end-1 in screen order, as quads. */
static void LayOutTtfTextWord(int first, int end, float scaleX, float scaleY)
{
    const TtfTextRecord *start = &g_astTtfTextRecords[g_awTtfTextAccepted[first]];
    const TtfTextFace *face;
    float rowsPerUnit;
    float fit;
    float condense;
    float shrink;
    float unitsX;
    float unitsY;
    float native;
    float natural;
    float pen;
    float baseline;
    float stretch;
    int advance;
    int index;

    /* Display pixels per font unit for caps as tall as the game font's. */
    rowsPerUnit = start->capRows / g_fTtfTextCapUnits;
    unitsY = rowsPerUnit * scaleY;
    /* The game font's width for the sample over the TrueType font's at that
     * height, both in frame columns. */
    fit = (float)start->sampleWidth /
          (g_nTtfTextSampleAdvance * unitsY / scaleX);
    condense = 1.0f;
    shrink = 1.0f;
    if (fit < 1.0f) {
        if (fit >= TTF_TEXT_MAX_CONDENSE) {
            condense = fit;
        } else {
            condense = TTF_TEXT_MAX_CONDENSE;
            shrink = fit / TTF_TEXT_MAX_CONDENSE;
        }
    }
    unitsY *= shrink;
    unitsX = unitsY * condense;
    face = FindTtfTextFace(unitsX, unitsY);
    if (face == 0)
        return;

    native = 0.0f;
    advance = 0;
    for (index = first; index < end; index++) {
        const TtfTextRecord *record =
            &g_astTtfTextRecords[g_awTtfTextAccepted[index]];

        native += record->width;
        advance += g_anTtfTextAdvance[record->character];
    }
    native *= scaleX;
    natural = advance * unitsX;
    /* Squeeze a word that would still not fit; the quads stretch the
     * rasterized glyphs, which keeps the number of faces small. */
    stretch = natural > native && natural > 0.0f ? native / natural : 1.0f;
    pen = floorf(start->x * scaleX + (native - natural * stretch) * 0.5f + 0.5f);
    baseline = floorf((start->y + start->capTop +
                       start->capRows * (1.0f + shrink) * 0.5f) * scaleY + 0.5f);
    for (index = first; index < end; index++) {
        const TtfTextRecord *record =
            &g_astTtfTextRecords[g_awTtfTextAccepted[index]];
        int glyph = record->character - TTF_TEXT_FIRST;
        SdlTtfTextQuad *quad;
        float left;

        if (face->glyphs[glyph].width > 0 && face->glyphs[glyph].height > 0 &&
            g_nTtfTextQuadCount < TTF_TEXT_RECORD_CAPACITY) {
            quad = &g_astTtfTextQuads[g_nTtfTextQuadCount++];
            left = floorf(pen + 0.5f) + face->glyphs[glyph].offsetX * stretch;
            quad->left = left / scaleX;
            quad->right = (left + face->glyphs[glyph].width * stretch) / scaleX;
            quad->top = (baseline + face->glyphs[glyph].offsetY) / scaleY;
            quad->bottom = (baseline + face->glyphs[glyph].offsetY +
                            face->glyphs[glyph].height) / scaleY;
            quad->u0 = (float)face->glyphs[glyph].atlasX / TTF_TEXT_ATLAS_SIZE;
            quad->v0 = (float)face->glyphs[glyph].atlasY / TTF_TEXT_ATLAS_SIZE;
            quad->u1 = (float)(face->glyphs[glyph].atlasX +
                               face->glyphs[glyph].width) / TTF_TEXT_ATLAS_SIZE;
            quad->v1 = (float)(face->glyphs[glyph].atlasY +
                               face->glyphs[glyph].height) / TTF_TEXT_ATLAS_SIZE;
            quad->colour = record->colour;
        }
        pen += g_anTtfTextAdvance[record->character] * unitsX * stretch;
    }
}

/* screen is the 320x200 frame as the game drew it and mouseFree the same
 * frame without the mouse pointer; erased (a copy of screen) loses the ink
 * of every glyph that is replaced.  scaleX and scaleY are display pixels per
 * frame pixel.  Returns the number of quads. */
int SdlTtfTextPrepare(const unsigned char *screen,
                      const unsigned char *mouseFree, unsigned char *erased,
                      float scaleX, float scaleY)
{
    int acceptedCount;
    int usedCount;
    int step;
    int index;
    int first;

    g_nTtfTextQuadCount = 0;
    if (!SdlTtfTextEnabled() || scaleX <= 0.0f || scaleY <= 0.0f)
        return 0;
    memset(g_abTtfTextClaimed, 0, sizeof(g_abTtfTextClaimed));
    /* Newest first, so a glyph drawn over an older one wins. */
    usedCount = 0;
    for (index = 0; index < TTF_TEXT_RECORD_CAPACITY; index++) {
        if (g_astTtfTextRecords[index].used)
            g_awTtfTextNewest[usedCount++] = (unsigned short)index;
    }
    qsort(g_awTtfTextNewest, (size_t)usedCount, sizeof(g_awTtfTextNewest[0]),
          CompareTtfTextNewest);
    acceptedCount = 0;
    for (step = 0; step < usedCount; step++) {
        TtfTextRecord *record;

        index = g_awTtfTextNewest[step];
        record = &g_astTtfTextRecords[index];
        if (!TtfTextRecordHolds(record, mouseFree)) {
            if (++record->missed > TTF_TEXT_MISS_LIMIT)
                record->used = 0;
            continue;
        }
        record->missed = 0;
        if (TtfTextCellClaimed(record))
            continue;
        ClaimTtfTextCell(record);
        g_awTtfTextAccepted[acceptedCount++] = (unsigned short)index;
    }
    if (acceptedCount == 0)
        return 0;

    for (index = 0; index < acceptedCount; index++) {
        const TtfTextRecord *record =
            &g_astTtfTextRecords[g_awTtfTextAccepted[index]];
        int row;
        int column;

        for (row = 0; row < record->height; row++) {
            int offset = (record->y + row) * SDL_PORT_FRAME_WIDTH + record->x;
            int base = row * TTF_TEXT_CELL_LIMIT;

            for (column = 0; column < record->width; column++) {
                /* Leave the mouse pointer where it covers the ink. */
                if (record->ink[base + column] &&
                    screen[offset + column] == record->after[base + column])
                    erased[offset + column] = record->before[base + column];
            }
        }
    }

    qsort(g_awTtfTextAccepted, (size_t)acceptedCount,
          sizeof(g_awTtfTextAccepted[0]), CompareTtfTextAccepted);
    first = 0;
    for (index = 1; index <= acceptedCount; index++) {
        const TtfTextRecord *previous =
            &g_astTtfTextRecords[g_awTtfTextAccepted[index - 1]];
        const TtfTextRecord *record = index < acceptedCount
            ? &g_astTtfTextRecords[g_awTtfTextAccepted[index]] : 0;

        if (record != 0 && record->y == previous->y &&
            record->x == previous->x + previous->width &&
            record->font == previous->font &&
            record->height == previous->height &&
            record->colour == previous->colour)
            continue;
        LayOutTtfTextWord(first, index, scaleX, scaleY);
        first = index;
    }
    return g_nTtfTextQuadCount;
}

const SdlTtfTextQuad *SdlTtfTextQuads(int *count)
{
    *count = g_nTtfTextQuadCount;
    return g_astTtfTextQuads;
}

/* The glyph atlas, one coverage byte per texel; the generation changes
 * whenever its texels do. */
const unsigned char *SdlTtfTextAtlas(int *size, unsigned int *generation)
{
    *size = TTF_TEXT_ATLAS_SIZE;
    *generation = g_uTtfTextAtlasGeneration;
    return g_pTtfTextAtlas;
}
