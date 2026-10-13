/* See card_layout_art.h. Decode-once-and-cache + store-into-bank, the same
 * shape as star_icons.c's make()/store()/Stars_IconCell() trio -- the
 * difference is color depth: guardian star icons and glyphs are 4 bits a
 * texel (packed four to a VRAM word, tpage depth field 0), this asset is
 * CardArt_IndexedImage's own 8-bit-a-texel output (up to 255 colors plus
 * transparent 0), packed two to a word, tpage depth field 1 (getTPage's own
 * encoding, psyq/libgpu.h) -- there is no existing 8bpp bank user in this
 * codebase to copy, so that packing and tpage math are derived here from
 * the PS1's own hardware layout, not guessed. */
#include "card_layout_art.h"
#include "card_layout.h"
#include "art.h"
#include "pc/render/soft_gpu.h"
#include "pc/render/texture_pack.h"
#include "pc/debug/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_BANK 13   /* 1-12 the 3D Monsters mod, 14 star icons, 15 glyphs, 0 is real VRAM */

/* PS1 hardware never actually draws a textured quad's own far-right or
 * far-bottom texel: gl_picture.c's own texel_left_out says so directly
 * ("the rasterizer stops short of the right and bottom edges... where the
 * texels grow towards them, the last"), and tools/pc/hd_assets_pack.py's
 * own first_twice exists to compensate for the same limit in retail's own
 * assets ("a run of texels is drawn one pixel longer than it is, its first
 * texel twice"). Every draw site that reads one of this file's cells
 * compensates the same way, reading one texel past the cell's own w/h
 * (u + w, not u + w - 1) -- so the bank needs that one extra row and
 * column too, each a duplicate of the real edge, not whatever this bank
 * slot held before (found live: every one of this file's cells read short
 * on their own right and bottom edge without this). bank_x/bank_y are in
 * this cell's own texel space (its own u=0, v=0 at (0, 0)), not raw
 * halfword offsets -- every cell here already keeps a *_X_WORD constant
 * for the halfword base a texel column adds onto. */
static unsigned char bank_texel_get(const uint16_t *bank, int bank_x_word, int bank_y, int y, int x)
{
    uint16_t word = bank[(bank_y + y) * SOFT_GPU_WIDTH + bank_x_word + x / 2];
    return (unsigned char)(x & 1 ? (word >> 8) : word);
}

static void bank_texel_set(uint16_t *bank, int bank_x_word, int bank_y, int y, int x, unsigned char value)
{
    uint16_t *word = &bank[(bank_y + y) * SOFT_GPU_WIDTH + bank_x_word + x / 2];
    *word = (uint16_t)(x & 1 ? ((*word & 0x00FF) | ((unsigned)value << 8)) : ((*word & 0xFF00) | value));
}

static void bank_pad_edge(uint16_t *bank, int bank_x_word, int bank_y, int w, int h)
{
    int x, y;
    for (y = 0; y < h; y++)
        bank_texel_set(bank, bank_x_word, bank_y, y, w, bank_texel_get(bank, bank_x_word, bank_y, y, w - 1));
    for (x = 0; x <= w; x++)
        bank_texel_set(bank, bank_x_word, bank_y, h, x, bank_texel_get(bank, bank_x_word, bank_y, h - 1, x));
}
/* A layout mod's own frame PNG is stretched to FRAME_W x FRAME_H texels
 * (CardArt_IndexedImage's own rule, as a "title" mod's image is), not the
 * on-screen draw size (card_layout.h's CardLayout_Get(CARD_LAYOUT_FRAME) w/h,
 * a mod's own choice) but the VRAM texture's own resolution.
 *
 * POLY_GT4's UVs are u8, so one quad can read at most 255 texels an axis.
 * The frame is therefore kept as FRAME_COLS x FRAME_ROWS tiles of
 * FRAME_TILE_W x FRAME_TILE_H texels, each on its own texture page, and drawn
 * as that many abutting quads (func_80028B08.c's CardLayout_DrawFrame). One
 * 177x254 texture (the old budget) put a 919x1319 frame through a ~27x area
 * reduction and, drawn at 560x784 on screen (Internal 4x), a 3.2x upscale: the
 * stat boxes' borders and the marbling went blocky. 3x3 tiles are 531x762,
 * about one texel to a screen pixel at 4x. All tiles share one 255-color
 * palette (a median cut of the whole image), kept under the first tile.
 *
 * Tile i sits at page (2 * (i % 8), i / 8): a 8bpp page is 256 texels (128
 * halfwords) wide, so the pages across are two apart, and a bank is 1024 x
 * 512 halfwords, two pages down. The palette row, FRAME_TILE_H, is free in
 * every page (a tile is FRAME_TILE_H tall). Keep FRAME_TEXELS in
 * tools/pc/card_frame_window.py in sync with FRAME_W/FRAME_H. */
#define FRAME_COLS CARD_LAYOUT_FRAME_COLS   /* card_layout_art.h: the one place the grid is set */
#define FRAME_ROWS CARD_LAYOUT_FRAME_ROWS
#define FRAME_TILE_W 177
#define FRAME_TILE_H 254
#define FRAME_W (FRAME_COLS * FRAME_TILE_W)
#define FRAME_H (FRAME_ROWS * FRAME_TILE_H)
/* The tiles take pages 0 to FRAME_COLS * FRAME_ROWS - 1 of the bank; the digit strip is on page 9. */
typedef char frame_tiles_fit_before_the_digits[FRAME_COLS * FRAME_ROWS <= 9 ? 1 : -1];
#define FRAME_CLUT_Y FRAME_TILE_H   /* right after the first tile's last pixel row, same bank, no overlap */

/* The first frame tile uses 89 of its page's 128 halfwords per row. The
 * remaining right edge fits the 52x60 hand frame. Row 255 is otherwise
 * unused (the full frame's palette is on 254), so it holds its 256-color
 * palette without touching either image. */
#define HAND_W 52
#define HAND_H 60
#define HAND_U 180
#define HAND_X_WORD (HAND_U / 2)
#define HAND_CLUT_Y 255
typedef char hand_frame_fits_tile[(HAND_X_WORD + HAND_W / 2 <= 128) ? 1 : -1];
static char hand_path[1024];
static char hand_bank_path[1024];
static unsigned char *hand_indices;
static unsigned short hand_clut[256];
static int hand_png_w, hand_png_h;

static void hand_build(const char *path)
{
    char why[128];

    if (!strcmp(hand_path, path)) return;
    free(hand_indices);
    hand_indices = NULL;
    hand_png_w = hand_png_h = 0;
    hand_bank_path[0] = 0;
    snprintf(hand_path, sizeof(hand_path), "%s", path);
    CardArt_ImageSize(path, &hand_png_w, &hand_png_h);
    hand_indices = malloc(HAND_W * HAND_H);
    if (hand_indices && !CardArt_IndexedImage(path, HAND_W, HAND_H, hand_indices, hand_clut, why, sizeof(why))) {
        LOG(LOG_CARD_LAYOUT, "hand frame: %s: %s", path, why);
        free(hand_indices);
        hand_indices = NULL;
    }
    if (hand_indices && hand_png_w > 0) TexturePack_BankImagePreload(path);
}

int CardLayoutArt_HandFrameCell(int card_id, int *tpage, int *u, int *v, int *clut, int *w, int *h)
{
    const char *path = CardLayout_HandFramePath(card_id);
    uint16_t *bank;
    int x, y;

    if (!path || !*path || !(bank = SoftGpu_Bank(FRAME_BANK))) return 0;
    hand_build(path);
    if (!hand_indices) return 0;
    if (strcmp(hand_bank_path, path)) {
        for (y = 0; y < HAND_H; y++) {
            for (x = 0; x < HAND_W; x += 2) {
                bank[y * SOFT_GPU_WIDTH + HAND_X_WORD + x / 2] =
                    (uint16_t)(hand_indices[y * HAND_W + x] | (hand_indices[y * HAND_W + x + 1] << 8));
            }
        }
        bank_pad_edge(bank, HAND_X_WORD, 0, HAND_W, HAND_H);
        memcpy(&bank[HAND_CLUT_Y * SOFT_GPU_WIDTH], hand_clut, sizeof(hand_clut));
        snprintf(hand_bank_path, sizeof(hand_bank_path), "%s", path);
        if (hand_png_w > 0 && hand_png_h > 0) {
            TexturePack_BankSpritesUseOwner(TEXTURE_BANK_OWNER_LAYOUT_FRAME);
            TexturePack_AddBankSprite(FRAME_BANK, 0, 0, 1, 0, HAND_CLUT_Y, HAND_U, 0, HAND_W, HAND_H, path);
        }
    }
    *tpage = 0x80 | (FRAME_BANK << 11);  /* 8bpp, page 0 */
    *u = HAND_U;
    *v = 0;
    *clut = HAND_CLUT_Y << 6;
    *w = HAND_W;
    *h = HAND_H;
    return 1;
}

/* Slot 12, not 11: slot 10's own CLUT row (MENU_CLUT_Y below) and slot 10's
 * LIBRARY_CLUT_Y both memcpy a full 256-halfword palette starting at their
 * page's own column (MENU_PAGE_X/LIBRARY_PAGE_X = 256) -- 256 halfwords
 * spills a whole slot-width past slot 10's own 128, into slot 11's columns
 * at those two rows. Slot 11 sits in the same page-row band (PAGE_Y 256) as
 * slot 10 and FIELD_H (60) covers both of slot 10's clut rows, so slot 11's
 * own pixels there would be overwritten (found live, not guessed: this used
 * the original slot 11 and desynced rows 272 and 302 of its image from a
 * live menu icon/library icon build). Slot 12 starts at column 512, past
 * where that 256-wide spillover reaches (511), so it is actually clear. */
#define FIELD_W 52
#define FIELD_H 60
#define FIELD_SLOT 12
#define FIELD_PAGE_X ((FIELD_SLOT % 8) * 128)
#define FIELD_PAGE_Y ((FIELD_SLOT / 8) * 256)
#define FIELD_CLUT_Y (FIELD_PAGE_Y + FIELD_H + 1)  /* +1: row FIELD_PAGE_Y + FIELD_H is the padded bottom edge */
static char field_path[1024];
static char field_bank_path[1024];
static unsigned char *field_indices;
static unsigned short field_clut[256];
static int field_png_w, field_png_h;

static void field_build(const char *path)
{
    char why[128];

    if (!strcmp(field_path, path)) return;
    free(field_indices);
    field_indices = NULL;
    field_png_w = field_png_h = 0;
    field_bank_path[0] = 0;
    snprintf(field_path, sizeof(field_path), "%s", path);
    CardArt_ImageSize(path, &field_png_w, &field_png_h);
    field_indices = malloc(FIELD_W * FIELD_H);
    if (field_indices && !CardArt_IndexedImage(path, FIELD_W, FIELD_H, field_indices, field_clut, why, sizeof(why))) {
        LOG(LOG_CARD_LAYOUT, "field frame: %s: %s", path, why);
        free(field_indices);
        field_indices = NULL;
    }
    if (field_indices && field_png_w > 0) TexturePack_BankImagePreload(path);
}

int CardLayoutArt_FieldFrameCell(int card_id, int *tpage, int *u, int *v, int *clut, int *w, int *h)
{
    const char *path = CardLayout_FieldFramePath(card_id);
    uint16_t *bank;
    int x, y;

    if (!path || !*path || !(bank = SoftGpu_Bank(FRAME_BANK))) return 0;
    field_build(path);
    if (!field_indices) return 0;
    if (strcmp(field_bank_path, path)) {
        for (y = 0; y < FIELD_H; y++) {
            for (x = 0; x < FIELD_W; x += 2) {
                bank[(FIELD_PAGE_Y + y) * SOFT_GPU_WIDTH + FIELD_PAGE_X + x / 2] =
                    (uint16_t)(field_indices[y * FIELD_W + x] | (field_indices[y * FIELD_W + x + 1] << 8));
            }
        }
        bank_pad_edge(bank, FIELD_PAGE_X, FIELD_PAGE_Y, FIELD_W, FIELD_H);
        memcpy(&bank[FIELD_CLUT_Y * SOFT_GPU_WIDTH + FIELD_PAGE_X], field_clut, sizeof(field_clut));
        snprintf(field_bank_path, sizeof(field_bank_path), "%s", path);
        if (field_png_w > 0 && field_png_h > 0) {
            TexturePack_BankSpritesUseOwner(TEXTURE_BANK_OWNER_LAYOUT_FRAME);
            TexturePack_AddBankSprite(FRAME_BANK, FIELD_PAGE_X, FIELD_PAGE_Y, 1, FIELD_PAGE_X, FIELD_CLUT_Y,
                                      0, 0, FIELD_W, FIELD_H, path);
        }
    }
    *tpage = 0x80 | (FRAME_BANK << 11) | ((FIELD_SLOT % 8) * 2) | ((FIELD_SLOT / 8) << 4);
    *u = 0;
    *v = 0;
    *clut = (FIELD_CLUT_Y << 6) | (FIELD_PAGE_X / 16);
    *w = FIELD_W;
    *h = FIELD_H;
    return 1;
}

/* Slot 13: same reasoning as FIELD_SLOT above -- clear of slot 10's two
 * 256-wide CLUT spillovers (they stop at column 511; slot 13 starts at
 * 640). */
#define FIELD_BACK_W 52
#define FIELD_BACK_H 60
#define FIELD_BACK_SLOT 13
#define FIELD_BACK_PAGE_X ((FIELD_BACK_SLOT % 8) * 128)
#define FIELD_BACK_PAGE_Y ((FIELD_BACK_SLOT / 8) * 256)
#define FIELD_BACK_CLUT_Y (FIELD_BACK_PAGE_Y + FIELD_BACK_H + 1)  /* +1: row FIELD_BACK_PAGE_Y + FIELD_BACK_H is the padded bottom edge */
static char field_back_path[1024];
static char field_back_bank_path[1024];
static unsigned char *field_back_indices;
static unsigned short field_back_clut[256];
static int field_back_png_w, field_back_png_h;

static void field_back_build(const char *path)
{
    char why[128];

    if (!strcmp(field_back_path, path)) return;
    free(field_back_indices);
    field_back_indices = NULL;
    field_back_png_w = field_back_png_h = 0;
    field_back_bank_path[0] = 0;
    snprintf(field_back_path, sizeof(field_back_path), "%s", path);
    CardArt_ImageSize(path, &field_back_png_w, &field_back_png_h);
    field_back_indices = malloc(FIELD_BACK_W * FIELD_BACK_H);
    if (field_back_indices &&
        !CardArt_IndexedImage(path, FIELD_BACK_W, FIELD_BACK_H, field_back_indices, field_back_clut, why,
                              sizeof(why))) {
        LOG(LOG_CARD_LAYOUT, "field back: %s: %s", path, why);
        free(field_back_indices);
        field_back_indices = NULL;
    }
    if (field_back_indices && field_back_png_w > 0) TexturePack_BankImagePreload(path);
}

int CardLayoutArt_FieldBackCell(int card_id, int *tpage, int *u, int *v, int *clut, int *w, int *h)
{
    const char *path = CardLayout_FieldBackPath(card_id);
    uint16_t *bank;
    int x, y;

    if (!path || !*path || !(bank = SoftGpu_Bank(FRAME_BANK))) return 0;
    field_back_build(path);
    if (!field_back_indices) return 0;
    if (strcmp(field_back_bank_path, path)) {
        for (y = 0; y < FIELD_BACK_H; y++) {
            for (x = 0; x < FIELD_BACK_W; x += 2) {
                bank[(FIELD_BACK_PAGE_Y + y) * SOFT_GPU_WIDTH + FIELD_BACK_PAGE_X + x / 2] =
                    (uint16_t)(field_back_indices[y * FIELD_BACK_W + x] |
                               (field_back_indices[y * FIELD_BACK_W + x + 1] << 8));
            }
        }
        bank_pad_edge(bank, FIELD_BACK_PAGE_X, FIELD_BACK_PAGE_Y, FIELD_BACK_W, FIELD_BACK_H);
        memcpy(&bank[FIELD_BACK_CLUT_Y * SOFT_GPU_WIDTH + FIELD_BACK_PAGE_X], field_back_clut,
               sizeof(field_back_clut));
        snprintf(field_back_bank_path, sizeof(field_back_bank_path), "%s", path);
        if (field_back_png_w > 0 && field_back_png_h > 0) {
            TexturePack_BankSpritesUseOwner(TEXTURE_BANK_OWNER_LAYOUT_FRAME);
            TexturePack_AddBankSprite(FRAME_BANK, FIELD_BACK_PAGE_X, FIELD_BACK_PAGE_Y, 1, FIELD_BACK_PAGE_X,
                                      FIELD_BACK_CLUT_Y, 0, 0, FIELD_BACK_W, FIELD_BACK_H, path);
        }
    }
    *tpage = 0x80 | (FRAME_BANK << 11) | ((FIELD_BACK_SLOT % 8) * 2) | ((FIELD_BACK_SLOT / 8) << 4);
    *u = 0;
    *v = 0;
    *clut = (FIELD_BACK_CLUT_Y << 6) | (FIELD_BACK_PAGE_X / 16);
    *w = FIELD_BACK_W;
    *h = FIELD_BACK_H;
    return 1;
}

/* Slot 14: same clear-of-slot-10 reasoning. Unlike every other cell here,
 * this one's own texel size is not fixed -- back_image stretches to the
 * style's own "width" x "height" (same box "image" fills), so the cached
 * build is keyed on size as well as path: a path alone is not enough to
 * know the bank still holds what a *later* call, from a different style
 * sharing that same image, would need redrawn at a different size. 255 is
 * POLY_FT4's own u8 UV span limit (v + h - 1 must fit a byte), comfortably
 * past the 140x196 default and any ordinary style box; 128 halfwords wide
 * (256 texels at 8bpp) and 256 rows is this one slot's own ceiling, wider
 * than the UV limit already allows. 254, not 255: the padded edge
 * (bank_pad_edge) needs one more row and column past bw/bh, and the CLUT
 * sits one past that again, so the last row/column actually used is
 * bh + 1 (256 rows means valid relative rows 0-255; bh + 1 <= 255 needs
 * bh <= 254, and the same bound covers bw since both share this ceiling). */
#define BACK_SLOT 14
#define BACK_PAGE_X ((BACK_SLOT % 8) * 128)
#define BACK_PAGE_Y ((BACK_SLOT / 8) * 256)
#define BACK_MAX_SIDE 254
static char back_path[1040];
static char back_bank_path[1040];
static unsigned char *back_indices;
static unsigned short back_clut[256];
static int back_png_w, back_png_h;
static int back_built_w, back_built_h;

static void back_build(const char *key, const char *path, int bw, int bh)
{
    char why[128];

    if (!strcmp(back_path, key)) return;
    free(back_indices);
    back_indices = NULL;
    back_png_w = back_png_h = 0;
    back_bank_path[0] = 0;
    snprintf(back_path, sizeof(back_path), "%s", key);
    CardArt_ImageSize(path, &back_png_w, &back_png_h);
    back_indices = malloc((size_t)bw * bh);
    if (back_indices && !CardArt_IndexedImage(path, bw, bh, back_indices, back_clut, why, sizeof(why))) {
        LOG(LOG_CARD_LAYOUT, "card-viewer back: %s: %s", path, why);
        free(back_indices);
        back_indices = NULL;
    }
    if (back_indices && back_png_w > 0) TexturePack_BankImagePreload(path);
}

int CardLayoutArt_BackCell(int card_id, int *tpage, int *u, int *v, int *clut, int *w, int *h)
{
    const char *path = CardLayout_BackImagePath(card_id);
    CardLayoutStyle style;
    uint16_t *bank;
    char key[1040];
    int x, y, bw, bh;

    if (!path || !*path || !(bank = SoftGpu_Bank(FRAME_BANK))) return 0;
    if (!CardLayout_StyleOf(card_id, &style)) return 0;
    bw = style.width;
    bh = style.height;
    if (bw <= 0 || bh <= 0 || bw > BACK_MAX_SIDE || bh > BACK_MAX_SIDE) {
        LOG(LOG_CARD_LAYOUT, "card-viewer back: %s: style box %dx%d is out of range (1-%d each)", path, bw, bh,
            BACK_MAX_SIDE);
        return 0;
    }
    snprintf(key, sizeof(key), "%dx%d:%s", bw, bh, path);
    back_build(key, path, bw, bh);
    if (!back_indices) return 0;
    if (strcmp(back_bank_path, key)) {
        for (y = 0; y < bh; y++) {
            for (x = 0; x < bw; x += 2) {
                bank[(BACK_PAGE_Y + y) * SOFT_GPU_WIDTH + BACK_PAGE_X + x / 2] =
                    (uint16_t)(back_indices[y * bw + x] | ((x + 1 < bw ? back_indices[y * bw + x + 1] : 0) << 8));
            }
        }
        bank_pad_edge(bank, BACK_PAGE_X, BACK_PAGE_Y, bw, bh);
        memcpy(&bank[(BACK_PAGE_Y + bh + 1) * SOFT_GPU_WIDTH + BACK_PAGE_X], back_clut, sizeof(back_clut));
        snprintf(back_bank_path, sizeof(back_bank_path), "%s", key);
        back_built_w = bw;
        back_built_h = bh;
        if (back_png_w > 0 && back_png_h > 0) {
            TexturePack_BankSpritesUseOwner(TEXTURE_BANK_OWNER_LAYOUT_FRAME);
            TexturePack_AddBankSprite(FRAME_BANK, BACK_PAGE_X, BACK_PAGE_Y, 1, BACK_PAGE_X, BACK_PAGE_Y + bh + 1, 0, 0,
                                      bw, bh, path);
        }
    }
    *tpage = 0x80 | (FRAME_BANK << 11) | ((BACK_SLOT % 8) * 2) | ((BACK_SLOT / 8) << 4);
    *u = 0;
    *v = 0;
    *clut = ((BACK_PAGE_Y + back_built_h + 1) << 6) | (BACK_PAGE_X / 16);
    *w = back_built_w;
    *h = back_built_h;
    return 1;
}

/* Page 10 is unused by the 3x3 full frame and its digit strip.  Keep the
 * menu icon there, with its own palette, so a style's hand frame and icon
 * need not share colors. */
#define MENU_SIDE 16
#define MENU_SLOT 10
#define MENU_PAGE_X ((MENU_SLOT % 8) * 128)
#define MENU_PAGE_Y ((MENU_SLOT / 8) * 256)
#define MENU_CLUT_Y (MENU_PAGE_Y + MENU_SIDE + 1)  /* +1: row MENU_PAGE_Y + MENU_SIDE is the padded bottom edge */
static char menu_path[1024];
static char menu_bank_path[1024];
static unsigned char *menu_indices;
static unsigned short menu_clut[256];
static int menu_png_w, menu_png_h;

static void menu_build(const char *path)
{
    char why[128];

    if (!strcmp(menu_path, path)) return;
    free(menu_indices);
    menu_indices = NULL;
    menu_png_w = menu_png_h = 0;
    menu_bank_path[0] = 0;
    snprintf(menu_path, sizeof(menu_path), "%s", path);
    CardArt_ImageSize(path, &menu_png_w, &menu_png_h);
    menu_indices = malloc(MENU_SIDE * MENU_SIDE);
    if (menu_indices && !CardArt_IndexedImage(path, MENU_SIDE, MENU_SIDE, menu_indices, menu_clut, why, sizeof(why))) {
        LOG(LOG_CARD_LAYOUT, "menu icon: %s: %s", path, why);
        free(menu_indices);
        menu_indices = NULL;
    }
    if (menu_indices && menu_png_w > 0) TexturePack_BankImagePreload(path);
}

int CardLayoutArt_MenuIconCell(int card_id, int *tpage, int *u, int *v, int *clut, int *w, int *h)
{
    const char *path = CardLayout_MenuIconPath(card_id);
    uint16_t *bank;
    int x, y;

    if (!path || !*path || !(bank = SoftGpu_Bank(FRAME_BANK))) return 0;
    menu_build(path);
    if (!menu_indices) return 0;
    if (strcmp(menu_bank_path, path)) {
        for (y = 0; y < MENU_SIDE; y++) {
            for (x = 0; x < MENU_SIDE; x += 2) {
                bank[(MENU_PAGE_Y + y) * SOFT_GPU_WIDTH + MENU_PAGE_X + x / 2] =
                    (uint16_t)(menu_indices[y * MENU_SIDE + x] | (menu_indices[y * MENU_SIDE + x + 1] << 8));
            }
        }
        bank_pad_edge(bank, MENU_PAGE_X, MENU_PAGE_Y, MENU_SIDE, MENU_SIDE);
        memcpy(&bank[MENU_CLUT_Y * SOFT_GPU_WIDTH + MENU_PAGE_X], menu_clut, sizeof(menu_clut));
        snprintf(menu_bank_path, sizeof(menu_bank_path), "%s", path);
        if (menu_png_w > 0 && menu_png_h > 0) {
            TexturePack_BankSpritesUseOwner(TEXTURE_BANK_OWNER_LAYOUT_FRAME);
            TexturePack_AddBankSprite(FRAME_BANK, MENU_PAGE_X, MENU_PAGE_Y, 1, MENU_PAGE_X, MENU_CLUT_Y,
                                      0, 0, MENU_SIDE, MENU_SIDE, path);
        }
    }
    *tpage = 0x80 | (FRAME_BANK << 11) | ((MENU_SLOT % 8) * 2) | ((MENU_SLOT / 8) << 4);
    *u = *v = 0;
    *clut = (MENU_CLUT_Y << 6) | (MENU_PAGE_X / 16);
    *w = *h = MENU_SIDE;
    return 1;
}

#define LIBRARY_W 12
#define LIBRARY_H 14
#define LIBRARY_SLOT MENU_SLOT
#define LIBRARY_PAGE_X MENU_PAGE_X
#define LIBRARY_PAGE_Y MENU_PAGE_Y
#define LIBRARY_U 32
#define LIBRARY_V 32
#define LIBRARY_X_WORD (LIBRARY_PAGE_X + LIBRARY_U / 2)
#define LIBRARY_Y (LIBRARY_PAGE_Y + LIBRARY_V)
#define LIBRARY_CLUT_Y (LIBRARY_Y + LIBRARY_H + 1)  /* +1: row LIBRARY_Y + LIBRARY_H is the padded bottom edge */
static char library_path[1024];
static char library_bank_path[1024];
/* Keep the useful card-001 Library diagnostic to one line per launch.  The
 * grid is rebuilt every frame, so an ordinary trace here would be noisy. */
static int library_card_one_noted;
static unsigned char *library_indices;
static unsigned short library_clut[256];
static int library_png_w, library_png_h;

static void library_build(const char *path)
{
    char why[128];

    if (!strcmp(library_path, path)) return;
    free(library_indices);
    library_indices = NULL;
    library_png_w = library_png_h = 0;
    library_bank_path[0] = 0;
    snprintf(library_path, sizeof(library_path), "%s", path);
    CardArt_ImageSize(path, &library_png_w, &library_png_h);
    library_indices = malloc(LIBRARY_W * LIBRARY_H);
    if (library_indices && !CardArt_IndexedImage(path, LIBRARY_W, LIBRARY_H, library_indices, library_clut, why, sizeof(why))) {
        LOG(LOG_CARD_LAYOUT, "library icon: %s: %s", path, why);
        free(library_indices);
        library_indices = NULL;
    }
    if (library_indices && library_png_w > 0) TexturePack_BankImagePreload(path);
}

int CardLayoutArt_LibraryIconCell(int card_id, int *tpage, int *u, int *v, int *clut, int *w, int *h)
{
    const char *path = CardLayout_LibraryIconPath(card_id);
    uint16_t *bank;
    int x, y;

    if (card_id == 1 && !library_card_one_noted) {
        LOG(LOG_CARD_LAYOUT, "library icon card 001: %s", path && *path ? path : "no style/image resolved");
        library_card_one_noted = 1;
    }
    if (!path || !*path || !(bank = SoftGpu_Bank(FRAME_BANK))) return 0;
    library_build(path);
    if (!library_indices) return 0;
    if (strcmp(library_bank_path, path)) {
        int opaque = 0, k;
        for (k = 0; k < LIBRARY_W * LIBRARY_H; k++) if (library_indices[k]) opaque++;
        for (y = 0; y < LIBRARY_H; y++) {
            for (x = 0; x < LIBRARY_W; x += 2) {
                bank[(LIBRARY_Y + y) * SOFT_GPU_WIDTH + LIBRARY_X_WORD + x / 2] =
                    (uint16_t)(library_indices[y * LIBRARY_W + x] | (library_indices[y * LIBRARY_W + x + 1] << 8));
            }
        }
        bank_pad_edge(bank, LIBRARY_X_WORD, LIBRARY_Y, LIBRARY_W, LIBRARY_H);
        memcpy(&bank[LIBRARY_CLUT_Y * SOFT_GPU_WIDTH + LIBRARY_PAGE_X], library_clut, sizeof(library_clut));
        snprintf(library_bank_path, sizeof(library_bank_path), "%s", path);
        if (card_id == 1) {
            uint16_t stored_word = bank[LIBRARY_Y * SOFT_GPU_WIDTH + LIBRARY_X_WORD];
            uint16_t stored_clut0 = bank[LIBRARY_CLUT_Y * SOFT_GPU_WIDTH + LIBRARY_PAGE_X];
            LOG(LOG_CARD_LAYOUT, "library icon card 001: stored %s, %d/%d opaque, png=%dx%d, "
                "row0word0=0x%04x (indices %d,%d) clut[0]=0x%04x bank=%p",
                path, opaque, LIBRARY_W * LIBRARY_H, library_png_w, library_png_h, stored_word,
                library_indices[0], library_indices[1], stored_clut0, (void *)bank);
        }
        if (library_png_w > 0 && library_png_h > 0) {
            TexturePack_BankSpritesUseOwner(TEXTURE_BANK_OWNER_LAYOUT_FRAME);
            TexturePack_AddBankSprite(FRAME_BANK, LIBRARY_PAGE_X, LIBRARY_PAGE_Y, 1,
                                      LIBRARY_PAGE_X, LIBRARY_CLUT_Y, LIBRARY_U, LIBRARY_V,
                                      LIBRARY_W, LIBRARY_H, path);
        }
    }
    *tpage = 0x80 | (FRAME_BANK << 11) | ((LIBRARY_SLOT % 8) * 2) | ((LIBRARY_SLOT / 8) << 4);
    *u = LIBRARY_U;
    *v = LIBRARY_V;
    *clut = (LIBRARY_CLUT_Y << 6) | (LIBRARY_PAGE_X / 16);
    *w = LIBRARY_W;
    *h = LIBRARY_H;
    if (card_id == 1) {
        static int logged_cell;
        if (!logged_cell) {
            LOG(LOG_CARD_LAYOUT, "library icon card 001: cell tpage=0x%04x u=%d v=%d clut=0x%04x w=%d h=%d",
                *tpage, *u, *v, *clut, *w, *h);
            logged_cell = 1;
        }
    }
    return 1;
}

/* Each frame image's built texels (FRAME_W x FRAME_H palette indices and its
 * palette), kept so that a card of another kind -- another frame image -- or
 * one drawn again later costs a copy into the bank, not a decode and a median
 * cut. The bank holds one frame at a time (bank_path). */
#define FRAME_CACHE 8
typedef struct {
    char path[1024];
    unsigned char *indices;   /* NULL: built and failed */
    unsigned short clut[256];
    int png_w, png_h;
    int used;
} FrameImage;
static FrameImage images[FRAME_CACHE];
static char bank_path[1024];    /* the path whose texels the bank holds now */

static FrameImage *image_for(const char *path, int build)
{
    FrameImage *slot = NULL;
    int i;
    char why[128];

    for (i = 0; i < FRAME_CACHE; i++) {
        if (images[i].used && !strcmp(images[i].path, path)) return &images[i];
    }
    if (!build) return NULL;
    for (i = 0; i < FRAME_CACHE && !slot; i++) {
        if (!images[i].used) slot = &images[i];
    }
    if (!slot) {   /* full: the first goes (a layout has at most one image per kind) */
        slot = &images[0];
        free(slot->indices);
        if (!strcmp(bank_path, slot->path)) bank_path[0] = 0;
    }
    memset(slot, 0, sizeof(*slot));
    snprintf(slot->path, sizeof(slot->path), "%s", path);
    slot->used = 1;
    CardArt_ImageSize(path, &slot->png_w, &slot->png_h);
    slot->indices = malloc((size_t)FRAME_W * FRAME_H);
    if (slot->indices &&
        !CardArt_IndexedImage(path, FRAME_W, FRAME_H, slot->indices, slot->clut, why, sizeof(why))) {
        LOG(LOG_CARD_LAYOUT, "frame: %s: %s", path, why);
        free(slot->indices);
        slot->indices = NULL;
    }
    /* The PNG itself for above 1x (TexturePack_AddBankSprite), read with
     * the rest of the build rather than when a tile is first drawn. */
    if (slot->indices && slot->png_w > 0) TexturePack_BankImagePreload(path);
    return slot;
}

/* The image's texels into the bank, tile by tile (see the layout above). */
static void store(uint16_t *bank, const FrameImage *image)
{
    int tile, x, y;

    for (tile = 0; tile < FRAME_COLS * FRAME_ROWS; tile++) {
        int col = tile % FRAME_COLS, row = tile / FRAME_COLS;
        uint16_t *page = bank + (tile / 8 * 256) * SOFT_GPU_WIDTH + tile % 8 * 128;
        for (y = 0; y < FRAME_TILE_H; y++) {
            const unsigned char *line = image->indices + (row * FRAME_TILE_H + y) * FRAME_W + col * FRAME_TILE_W;
            for (x = 0; x < FRAME_TILE_W; x += 2) {
                unsigned char lo = line[x];
                unsigned char hi = x + 1 < FRAME_TILE_W ? line[x + 1] : 0;   /* the odd width's last pair */
                page[y * SOFT_GPU_WIDTH + x / 2] = (uint16_t)(lo | (hi << 8));
            }
        }
    }
    memcpy(&bank[FRAME_CLUT_Y * SOFT_GPU_WIDTH], image->clut, 256 * sizeof(uint16_t));
}

int CardLayoutArt_FrameTile(int col, int row, int *tpage, int *clut, int *w, int *h)
{
    uint16_t *bank;
    const char *path = CardLayout_FramePath();
    const FrameImage *image;
    int tile = row * FRAME_COLS + col;
    int sx, sy, sx1, sy1;
    if (col < 0 || col >= FRAME_COLS || row < 0 || row >= FRAME_ROWS) return 0;
    if (!path || !*path) { LOG(LOG_CARD_LAYOUT, "FrameTile: no path"); return 0; }
    if (!(bank = SoftGpu_Bank(FRAME_BANK))) { LOG(LOG_CARD_LAYOUT, "FrameTile: no bank"); return 0; }
    image = image_for(path, 1);
    if (!image || !image->indices) {
        LOG(LOG_CARD_LAYOUT, "FrameTile: %s: no indices (image_for gave up)", path);
        return 0;
    }
    if (strcmp(bank_path, path)) {
        int opaque = 0, k;
        for (k = 0; k < FRAME_W * FRAME_H; k++) if (image->indices[k]) opaque++;
        store(bank, image);
        snprintf(bank_path, sizeof(bank_path), "%s", path);
        LOG(LOG_CARD_LAYOUT, "FrameTile: stored %s, %d/%d texels opaque, clut[1]=0x%04x bank=%p "
            "bank[tile0 row0 word0]=0x%04x after store", path, opaque, FRAME_W * FRAME_H, image->clut[1],
            (void *)bank, bank[0]);
    }
    /* getTPage(1, 0, x, y) | bank << 11: 8bpp, this tile's page in the bank (store()'s layout) */
    *tpage = 0x80 | (FRAME_BANK << 11) | (tile % 8 * 2) | (tile / 8 << 4);
    *clut = (FRAME_CLUT_Y << 6) | 0;   /* getClut(0, FRAME_CLUT_Y) */
    *w = FRAME_TILE_W;
    *h = FRAME_TILE_H;
    if (tile == 0) {
        LOG(LOG_CARD_LAYOUT, "FrameTile: tile0 tpage=0x%04x clut=0x%04x w=%d h=%d bank_now=%p",
            *tpage, *clut, *w, *h, (void *)SoftGpu_Bank(FRAME_BANK));
    }
    if (image->png_w > 0 && image->png_h > 0) {
        int added, found;
        TexturePack_BankSpritesUseOwner(TEXTURE_BANK_OWNER_LAYOUT_FRAME);
        sx = col * FRAME_TILE_W * image->png_w / FRAME_W;
        sx1 = (col + 1) * FRAME_TILE_W * image->png_w / FRAME_W;
        sy = row * FRAME_TILE_H * image->png_h / FRAME_H;
        sy1 = (row + 1) * FRAME_TILE_H * image->png_h / FRAME_H;
        added = TexturePack_AddBankSpriteCrop(FRAME_BANK, (tile % 8 * 2) * 64, tile / 8 * 256, 1, 0, FRAME_CLUT_Y, 0, 0,
                                              FRAME_TILE_W, FRAME_TILE_H, path, sx, sy, sx1 - sx, sy1 - sy);
        if (tile == 0) {
            found = TexturePack_BankEntryFor(FRAME_BANK, (tile % 8 * 2) * 64, tile / 8 * 256, 1, 0, FRAME_CLUT_Y,
                                             FRAME_TILE_W / 2, FRAME_TILE_H / 2);
            LOG(LOG_CARD_LAYOUT, "FrameTile: tile0 AddBankSpriteCrop=%d (src %d,%d %dx%d of %dx%d) "
                "BankEntryFor(mid)=%d", added, sx, sy, sx1 - sx, sy1 - sy, image->png_w, image->png_h, found);
        }
    }
    return 1;
}

/* The digit strip: one image, one page of the frame's bank. The frame fills
 * the first tile row's pages and the first of the second row's (tile 8);
 * the digits are on the second row's next page, tile slot 9, their palette
 * right under them. */
#define DIGITS_SLOT 9
#define DIGITS_W (CARD_LAYOUT_DIGIT_COLS * CARD_LAYOUT_DIGIT_CELL_W)
#define DIGITS_H (CARD_LAYOUT_DIGIT_ROWS * CARD_LAYOUT_DIGIT_CELL_H)
static char digits_path[1024];          /* the strip built (or failed) */
static unsigned char *digits_indices;
static unsigned short digits_clut[256];
static int digits_png_w, digits_png_h;
static char digits_bank_path[1024];     /* the strip the bank holds now */

static void digits_build(const char *path)
{
    char why[128];

    if (!strcmp(digits_path, path)) return;
    free(digits_indices);
    snprintf(digits_path, sizeof(digits_path), "%s", path);
    digits_png_w = digits_png_h = 0;
    CardArt_ImageSize(path, &digits_png_w, &digits_png_h);
    digits_bank_path[0] = 0;
    digits_indices = malloc((size_t)DIGITS_W * DIGITS_H);
    if (digits_indices &&
        !CardArt_IndexedImage(path, DIGITS_W, DIGITS_H, digits_indices, digits_clut, why, sizeof(why))) {
        LOG(LOG_CARD_LAYOUT, "digits: %s: %s", path, why);
        free(digits_indices);
        digits_indices = NULL;
    }
    if (digits_indices && digits_png_w > 0) TexturePack_BankImagePreload(path);
}

int CardLayoutArt_DigitCell(int digit, int dim, int *tpage, int *u, int *v, int *clut, int *w, int *h)
{
    char path[1024];
    int dw, dh, step, x, y, sx, sy, sx1, sy1;
    uint16_t *bank, *page;

    if (digit < 0 || digit > 9) return 0;
    if (!CardLayout_Digits(path, sizeof(path), &dw, &dh, &step)) return 0;
    if (!(bank = SoftGpu_Bank(FRAME_BANK))) return 0;
    digits_build(path);
    if (!digits_indices) return 0;
    page = bank + (DIGITS_SLOT / 8 * 256) * SOFT_GPU_WIDTH + DIGITS_SLOT % 8 * 128;
    if (strcmp(digits_bank_path, path)) {
        for (y = 0; y < DIGITS_H; y++) {
            for (x = 0; x < DIGITS_W; x += 2) {
                page[y * SOFT_GPU_WIDTH + x / 2] =
                    (uint16_t)(digits_indices[y * DIGITS_W + x] | (digits_indices[y * DIGITS_W + x + 1] << 8));
            }
        }
        memcpy(&page[DIGITS_H * SOFT_GPU_WIDTH], digits_clut, 256 * sizeof(uint16_t));
        snprintf(digits_bank_path, sizeof(digits_bank_path), "%s", path);
    }
    *tpage = 0x80 | (FRAME_BANK << 11) | (DIGITS_SLOT % 8 * 2) | (DIGITS_SLOT / 8 << 4);
    *clut = ((DIGITS_SLOT / 8 * 256 + DIGITS_H) << 6) | (DIGITS_SLOT % 8 * 128 / 16);
    *u = digit % CARD_LAYOUT_DIGIT_COLS * CARD_LAYOUT_DIGIT_CELL_W;
    *v = (digit / CARD_LAYOUT_DIGIT_COLS + (dim ? 2 : 0)) * CARD_LAYOUT_DIGIT_CELL_H;
    *w = CARD_LAYOUT_DIGIT_CELL_W;
    *h = CARD_LAYOUT_DIGIT_CELL_H;
    if (digits_png_w > 0 && digits_png_h > 0) {
        TexturePack_BankSpritesUseOwner(TEXTURE_BANK_OWNER_LAYOUT_DIGITS);
        sx = *u * digits_png_w / DIGITS_W;
        sx1 = (*u + *w) * digits_png_w / DIGITS_W;
        sy = *v * digits_png_h / DIGITS_H;
        sy1 = (*v + *h) * digits_png_h / DIGITS_H;
        TexturePack_AddBankSpriteCrop(FRAME_BANK, (DIGITS_SLOT % 8 * 2) * 64, DIGITS_SLOT / 8 * 256, 1,
                                      DIGITS_SLOT % 8 * 128, DIGITS_SLOT / 8 * 256 + DIGITS_H, *u, *v, *w, *h, path,
                                      sx, sy, sx1 - sx, sy1 - sy);
    }
    return 1;
}

/* Two independent pages keep ATK and DFD labels from needing compatible
 * palettes. A label is resampled into one 128x64 8bpp cell; its manifest's
 * width/height decide its on-card size, not this texture budget. */
#define STAT_LABEL_W 128
#define STAT_LABEL_H 64
#define STAT_LABEL_SLOT_ATK 11
#define STAT_LABEL_SLOT_DFD 12
typedef struct {
    char path[1024], bank_path[1024];
    unsigned char *indices;
    unsigned short clut[256];
    int png_w, png_h;
} StatLabelImage;
static StatLabelImage stat_labels[2];

int CardLayoutArt_StatLabelCell(int dfd, int *tpage, int *u, int *v, int *clut, int *w, int *h)
{
    char path[1024], why[128];
    uint16_t *bank, *page;
    StatLabelImage *image;
    int slot = dfd ? STAT_LABEL_SLOT_DFD : STAT_LABEL_SLOT_ATK;
    int page_x = (slot % 8) * 128, page_y = (slot / 8) * 256;
    int clut_y = page_y + STAT_LABEL_H, x, y;

    if (!CardLayout_StatLabel(dfd, path, sizeof(path), &x, &y, w, h) || !(bank = SoftGpu_Bank(FRAME_BANK))) return 0;
    image = &stat_labels[dfd != 0];
    if (strcmp(image->path, path)) {
        free(image->indices);
        memset(image, 0, sizeof(*image));
        snprintf(image->path, sizeof(image->path), "%s", path);
        CardArt_ImageSize(path, &image->png_w, &image->png_h);
        image->indices = malloc(STAT_LABEL_W * STAT_LABEL_H);
        if (image->indices && !CardArt_IndexedImage(path, STAT_LABEL_W, STAT_LABEL_H, image->indices, image->clut, why, sizeof(why))) {
            LOG(LOG_CARD_LAYOUT, "stat label: %s: %s", path, why);
            free(image->indices);
            image->indices = NULL;
        }
        if (image->indices && image->png_w > 0) TexturePack_BankImagePreload(path);
    }
    if (!image->indices) return 0;
    if (strcmp(image->bank_path, path)) {
        page = bank + page_y * SOFT_GPU_WIDTH + page_x;
        for (y = 0; y < STAT_LABEL_H; y++)
            for (x = 0; x < STAT_LABEL_W; x += 2)
                page[y * SOFT_GPU_WIDTH + x / 2] = (uint16_t)(image->indices[y * STAT_LABEL_W + x] |
                                                               (image->indices[y * STAT_LABEL_W + x + 1] << 8));
        memcpy(&bank[clut_y * SOFT_GPU_WIDTH + page_x], image->clut, sizeof(image->clut));
        snprintf(image->bank_path, sizeof(image->bank_path), "%s", path);
        if (image->png_w > 0 && image->png_h > 0) {
            TexturePack_BankSpritesUseOwner(TEXTURE_BANK_OWNER_LAYOUT_FRAME);
            TexturePack_AddBankSprite(FRAME_BANK, page_x, page_y, 1, page_x, clut_y,
                                      0, 0, STAT_LABEL_W, STAT_LABEL_H, path);
        }
    }
    *tpage = 0x80 | (FRAME_BANK << 11) | ((slot % 8) * 2) | ((slot / 8) << 4);
    *u = *v = 0;
    *clut = (clut_y << 6) | (page_x / 16);
    /* The caller's earlier CardLayout_StatLabel call has the on-card draw
     * dimensions. These outputs describe the full source cell for its UVs;
     * returning the manifest's 18x8 here crops the transparent top-left of
     * a centred label image and makes the text disappear. */
    *w = STAT_LABEL_W;
    *h = STAT_LABEL_H;
    return 1;
}

/* The retail name is a black indexed sprite. Copy its non-transparent texels
 * as a one-colour 8bpp mask so vertex RGB can colour it (multiplying the
 * original black pixels could never make them white). */
#define TITLE_SLOT 13
#define TITLE_W 96
#define TITLE_H 14
int CardLayoutArt_TitleCell(int source_tpage, int source_u, int source_v, int source_w, int source_h,
                            int *tpage, int *u, int *v, int *clut, int *w, int *h)
{
    uint16_t *bank = SoftGpu_Bank(FRAME_BANK);
    const uint16_t *vram = SoftGpu_Vram();
    int page_x = (TITLE_SLOT % 8) * 128, page_y = (TITLE_SLOT / 8) * 256;
    int clut_y = page_y + TITLE_H, depth = (source_tpage >> 7) & 3;
    int src_x = (source_tpage & 0xF) * 64, src_y = ((source_tpage >> 4) & 1) * 256;
    int x, y;

    if (!bank || !vram || source_w != TITLE_W || source_h != TITLE_H || depth == 3) {
        LOG(LOG_CARD_LAYOUT, "title mask: unavailable (bank=%p vram=%p tpage=0x%x depth=%d uv=%d,%d size=%d,%d)",
            (void *)bank, (const void *)vram, source_tpage, depth, source_u, source_v, source_w, source_h);
        return 0;
    }
    for (y = 0; y < TITLE_H; y++) {
        for (x = 0; x < TITLE_W; x += 2) {
            int j, bits[2] = {0, 0};
            for (j = 0; j < 2; j++) {
                int tx = source_u + x + j, ty = source_v + y;
                uint16_t word = vram[(src_y + ty) * SOFT_GPU_WIDTH + src_x +
                                     (depth == 0 ? tx / 4 : depth == 1 ? tx / 2 : tx)];
                /* A 15-bit opaque black texel is 0x8000: it is a visible
                 * black glyph, not transparent.  Test the whole word there
                 * so such a title becomes a white mask too. */
                int pixel = depth == 0 ? (word >> ((tx & 3) * 4)) & 15 :
                            depth == 1 ? (word >> ((tx & 1) * 8)) & 255 : word;
                bits[j] = pixel != 0;
            }
            bank[(page_y + y) * SOFT_GPU_WIDTH + page_x + x / 2] = (uint16_t)(bits[0] | bits[1] << 8);
        }
    }
    memset(&bank[clut_y * SOFT_GPU_WIDTH + page_x], 0, 256 * sizeof(uint16_t));
    bank[clut_y * SOFT_GPU_WIDTH + page_x + 1] = 0x7FFF;
    *tpage = 0x80 | (FRAME_BANK << 11) | ((TITLE_SLOT % 8) * 2) | ((TITLE_SLOT / 8) << 4);
    *u = *v = 0;
    *clut = (clut_y << 6) | (page_x / 16);
    *w = TITLE_W;
    *h = TITLE_H;
    LOG(LOG_CARD_LAYOUT, "title mask: bank=%d tpage=0x%x clut=0x%x from tpage=0x%x depth=%d uv=%d,%d",
        FRAME_BANK, *tpage, *clut, source_tpage, depth, source_u, source_v);
    return 1;
}

void CardLayoutArt_Prewarm(void)
{
    static unsigned int frame;
    char paths[FRAME_CACHE][1024];
    char digits[1024] = "";
    int i, count, dw, dh, dstep;

    if (++frame % 30) return;   /* the layout changes with a setting, not a frame */
    if (CardLayout_Digits(digits, sizeof(digits), &dw, &dh, &dstep) && strcmp(digits_path, digits)) {
        digits_build(digits);
        return;
    } else if (!digits[0]) TexturePack_BankSpritesClear(TEXTURE_BANK_OWNER_LAYOUT_DIGITS);
    count = CardLayout_FramePaths(paths, FRAME_CACHE);
    if (!count) TexturePack_BankSpritesClear(TEXTURE_BANK_OWNER_LAYOUT_FRAME);
    for (i = 0; i < count; i++) {
        if (!image_for(paths[i], 0)) {
            image_for(paths[i], 1);
            return;   /* one a call: each is a hitch of its own */
        }
    }
}
