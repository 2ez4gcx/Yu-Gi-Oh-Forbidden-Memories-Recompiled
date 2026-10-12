/* Tower mode's state (src/pc/tower/tower_state.h): the save's progress in
 * the tower, the tower screen's place and the text its boxes show.
 *
 * A game unit, like drops.c, so it is in the game's data: at a fixed
 * address the game's text boxes can point into, and inside every save
 * state, so a state saved in the tower, or in a tower duel, comes back to
 * the same floor and the same rules. Game units link in name order, and the
 * "zz_" keeps this one after every other: no existing variable moves, which
 * would keep states from earlier builds from loading. */
#include "types.h"
#include "pc/tower/tower_state.h"
#include "game/func_8003B6AC.h"
#include "game/text_box_lifecycle.h"
#include "game/file_transfer.h"
#include "game/main_services.h"
#include "game/menu_record_reset.h"
#include "overlays/main_menu/frontend.h"

TowerState gTower = {{0}};

/* A box on text channel 0, its entries' layout reset first as the
 * campaign's dialogue does (Script_OpShowDialog): here because that reset
 * (func_8003B6AC) is still the game's own code, which only game code
 * reaches, as pack_shop_box.c says. */
void *Tower_CreateBox(s32 id, s32 x, s32 y, s32 width, s32 height, s32 flags)
{
    func_8003B6AC(0, 2);
    return TextBox_CreateFlagged(0, id, x, y, width, height, flags);
}

/* The title's wall behind the tower's screen: the main menu's own package
 * and background (MainMenu_DrawFrontendBackground in the frame services'
 * slot 0, where the title puts it), dimmed as behind the title's menus;
 * gone with the screen. The portraits' records are reset after the load, as
 * Main_RunMenu resets them after the same one. Game code, so the callback is
 * stored as the game stores one in every build. */
void Tower_Background(s32 on)
{
    if (on) {
        File_RequestMainMenuPackage();
        File_WaitForTransfers();
        func_80039E9C();
        D_80184597 = 0x80;
        D_800E9DB0[0] = MainMenu_DrawFrontendBackground;
    } else if (D_800E9DB0[0] == MainMenu_DrawFrontendBackground) {
        D_800E9DB0[0] = 0;
    }
}
