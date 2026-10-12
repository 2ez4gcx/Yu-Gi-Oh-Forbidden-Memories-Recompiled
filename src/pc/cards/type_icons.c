#include "type_icons.h"
#include "cards.h"
#include "art.h"
#include "pc/render/soft_gpu.h"
#include "pc/render/texture_pack.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TYPE_LIMIT 256
#define BANK 13
#define PAGE 0
#define SIDE CARD_ICON_SIDE
#define CLUT_Y 256

static unsigned char made[TYPE_LIMIT];
/* A bad PNG must not be decoded and logged on every icon draw.  This is a
 * request clock rather than wall time because this path has no clock source;
 * TypeIcons_Clear resets it on a mod reload, which retries immediately. */
#define RETRY_INTERVAL 60
static unsigned retry_clock, retry_at[TYPE_LIMIT];
static unsigned pending;

/* func_80037DA4 marks the icon it is about to add with the card's real
 * type, the way Stars_MarkIcon does for a guardian star; DuelEffect_AppendEntry
 * takes the mark into the entry's otherwise-unused code as TYPE_ICON_CODE |
 * (type - 24), or 0 where the disc's own icon stands (type_icons.h). */
void TypeIcons_MarkIcon(int type)
{
    pending = (type > CARD_TYPE_EQUIP && Cards_TypeIcon(type)) ? TYPE_ICON_CODE | (unsigned)(type - CARD_TYPE_EQUIP - 1) : 0;
}

unsigned TypeIcons_TakeIconMark(void)
{
    unsigned code = pending;
    pending = 0;
    return code;
}

int TypeIcons_IconCell(unsigned code, int *tpage, int *u, int *v, int *clut_x, int *clut_y)
{
    int type = (int)(code & 0xFFu) + CARD_TYPE_EQUIP + 1;
    int clut;
    if ((code & 0xFF00u) != TYPE_ICON_CODE || !TypeIcons_Cell(type, tpage, u, v, &clut)) return 0;
    *clut_x = 0;
    *clut_y = clut >> 6;
    return 1;
}

void TypeIcons_Clear(void)
{
    memset(made, 0, sizeof(made));
    memset(retry_at, 0, sizeof(retry_at));
    retry_clock = 0;
    pending = 0;
    TexturePack_BankSpritesClear(TEXTURE_BANK_OWNER_TYPE_ICONS);
}

static void store(uint16_t *bank, int type, const unsigned char *pixels)
{
    int x, y, slot = type - 24, left = (slot & 15) * SIDE / 4, top = (slot >> 4) * SIDE;
    for (y = 0; y < SIDE; y++) for (x = 0; x < SIDE / 4; x++)
        bank[(top + y) * SOFT_GPU_WIDTH + left + x] = (uint16_t)(pixels[y * SIDE / 2 + x * 2] |
                                                                   (pixels[y * SIDE / 2 + x * 2 + 1] << 8));
}

int TypeIcons_Cell(int type, int *tpage, int *u, int *v, int *clut)
{
    uint16_t *bank;
    unsigned char pixels[SIDE * SIDE / 2];
    unsigned short palette[16];
    char why[256];
    const char *path = Cards_TypeIcon(type);
    int slot;
    if (!path || type < 24 || type >= TYPE_LIMIT || !(bank = SoftGpu_Bank(BANK))) return 0;
    retry_clock++;
    if (made[type] == 2 && retry_clock < retry_at[type]) return 0;
    if (made[type] == 2) made[type] = 0;
    if (!made[type]) {
        made[type] = 2;
        if (!CardArt_IconFromImage(path, NULL, pixels, palette, why, sizeof(why))) {
            fprintf(stderr, "memories-pc: type %d: %s\n", type, why);
            /* A mod reload calls TypeIcons_Clear and retries at once.  Until
             * then, throttle a permanently bad file to one attempt per 60
             * requests, avoiding filesystem/PNG work every frame. */
            retry_at[type] = retry_clock + RETRY_INTERVAL;
            return 0;
        }
        memcpy(&bank[(CLUT_Y + type - 24) * SOFT_GPU_WIDTH], palette, sizeof(palette));
        store(bank, type, pixels);
        TexturePack_BankSpritesUseOwner(TEXTURE_BANK_OWNER_TYPE_ICONS);
        slot = type - 24;
        if (!TexturePack_AddBankSprite(BANK, PAGE * 64, 0, 0, 0, CLUT_Y + slot, (slot & 15) * SIDE, (slot >> 4) * SIDE,
                                       SIDE, SIDE, path))
            fprintf(stderr, "memories-pc: type %d: its icon has no HD replacement (the console-resolution one still applies)\n", type);
        made[type] = 1;
    }
    if (made[type] != 1) return 0;
    slot = type - 24;
    *tpage = PAGE | (BANK << 11);
    *u = (slot & 15) * SIDE;
    *v = (slot >> 4) * SIDE;
    *clut = (CLUT_Y + slot) << 6;
    return 1;
}
