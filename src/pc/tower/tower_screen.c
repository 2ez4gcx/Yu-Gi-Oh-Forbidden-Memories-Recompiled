/* The tower's screen (tower.h): mode TOWER_MODE, which Main_Loop runs
 * itself (main_loop.c), entered from the title's second menu and come back
 * to after every floor's duel.
 *
 * Everything on it is the game's own: its text boxes with their window,
 * letters, page arrow, choice cursor and sounds, and, beside a line of
 * story, the speaker's portrait drawn by the dialogue's portrait effect
 * (F6 41-59, Text_HandleDisplayEffectCommand) from the same 48 x 48 cells
 * of VRAM the campaign's scene package fills (campaign_scene_package.h):
 * a duelist's is its Free Duel portrait (a mod's own, else its base's, off
 * the disc), and one of the campaign's 25 is that package's record. The
 * text is composed here in the game's codes into gTower.text, a game
 * variable, so the boxes point into the game's data and a save state keeps
 * them.
 *
 * Its steps (gTower.step):
 *
 *   BEGIN    the first frame: the music, the result of the duel just come
 *            back from (Tower_ApplyResult, the reward), then what to tell
 *   STORY    lines of story, one dialogue box each, page by page
 *   NOTICE   one box of what happened (a floor won, a loss), then on
 *   STATUS   the tower's name, the floor, its opponent and rules: DUEL or
 *            LEAVE
 *   SAVE     a checkpoint reached: SAVE? YES/NO, then the game's own save
 *            (the memory card dialog, the port's slot menu)
 *   DUEL     the floor's duel armed as the Free Duel screen arms one; the
 *            game comes back here after it (D_8009B368)
 *   LEAVE    the title's second menu, the tower's button under the cursor
 */
#include "tower.h"
#include "pc/free_duel/duelists.h"
#include "pc/cards/cards.h"
#include "pc/cards/disc_art.h"
#include "pc/cards/tables.h"
#include "pc/text/glyphs.h"
#include "pc/text/text.h"
#include "pc/debug/log.h"
#ifdef MEMORIES_TRANSLATED
#include "pc/guest/translated_runtime.h"
#endif
#include "types.h"
#include "psyq/libgte.h"
#include "psyq/libgpu.h"
#include "game/duel_effect.h"
#include "game/text_box_lifecycle.h"
#include "game/func_80039794.h"
#include "game/dialog_choice.h"
#include "game/fade.h"
#include "game/sound.h"
#include "game/func_80024DC8.h"
#include "game/duel_rewards.h"
#include "game/main_modes.h"
#include "game/save_data.h"
#include "game/data_transfer_request.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The game's bytes, declared plainly: this unit is native (title_jump.c). */
extern u8 D_8009B26C, D_8009B26D, D_8009B268, D_8009B368, D_8009B362, gDuel_bTerrain;
extern u16 gDuel_awPlayerDeck[];

/* The box's string id: past the port's own (text.h), clear of the disc's. */
#define TOWER_TEXT_ID 0xFFF8
#define TEXT_START 16                   /* gTower.text[0] is the {end} a choice jumps to */
#define LETTER 8                        /* a letter's width in a box */
#define DIALOG_LINES 3                  /* a page of the dialogue box */
#define SECOND_MENU 5                   /* D_8009B26D: the title opens on its second menu */
#define TOWER_DEFAULT_SONG 0x2C0        /* the Free Duel screen's (SD_BGMPlay(0x72C0)) */
#define SE_CHOOSE 0x30                  /* the Free Duel screen's for a duel chosen */
#define SE_BUZZ 9
#define FREE_DUEL_PORTRAITS_SECTOR 0x1EAA   /* WA: the Free Duel package's portrait phase */
#define CAMPAIGN_PORTRAITS_SECTOR 0x1E6A    /* WA 0xF35000: the scene package's */
#define PORTRAIT_RECORD 0x980
#define PORTRAIT_IMAGE 0x900
#define PORTRAIT_EFFECT 0x41            /* F6 id of cell 0 */
#define SPEAKER_CELL 0                  /* the cell a face is put in: clear of the title's wall (x 960 up) */

/* The dialogue box (Script_OpShowDialog's place) and the menu box. */
enum { DIALOG_X = 0x10, DIALOG_Y = 0xB0, DIALOG_W = 0x120, DIALOG_H = 0x30 };
enum { MENU_X = 0x18, MENU_W = 0x110 };
/* TextBox_CreateFlagged's flags: 0x20 the dark window behind it
 * (func_800391E4 builds it), 0x08 kept up at its end until a button, which
 * func_80039794 clears. (Not 0x1000, which says a menu's choices are being
 * laid out.) A story's line ends in a page wait instead, and a choice is
 * its own button. */
#define BOX_WINDOW 0x20
#define BOX_KEPT 0x08

enum { STEP_BEGIN, STEP_STORY, STEP_NOTICE, STEP_STATUS, STEP_SAVE_ASK, STEP_SAVE, STEP_DUEL, STEP_LEAVE,
       STEP_NO_DECK };
enum { STORY_INTRO = 1, STORY_BEFORE, STORY_AFTER, STORY_ENDING, STORY_LOSE };
enum { OUTCOME_NONE, OUTCOME_WON, OUTCOME_CHECKPOINT, OUTCOME_COMPLETED, OUTCOME_LOST };

/* --- the text --------------------------------------------------------------------- */

typedef struct {
    u8 *at, *end;
    int x, limit;                       /* pixels into the line, and the line's width */
} Out;

static Out begin(int limit)
{
    Out out;
    memset(gTower.text, 0xFF, TEXT_START);
    out.at = gTower.text + TEXT_START;
    out.end = gTower.text + TOWER_TEXT_SIZE - 8;
    out.x = 0;
    out.limit = limit;
    return out;
}

static void put(Out *out, int byte)
{
    if (out->at < out->end) *out->at++ = (u8)byte;
}

static void finish(Out *out)
{
    *out->at = 0xFF;
}

static void newline(Out *out)
{
    put(out, 0xFE);
    out->x = 0;
}

static void glyph(Out *out, int code)
{
    if (code < 0 || out->x + LETTER > out->limit) return;
    if (code >= 0xF0) put(out, 0xF0 + (code >> 8));
    put(out, code & 0xFF);
    out->x += LETTER;
}

static void words(Out *out, const char *text)
{
    while (*text) {
        uint32_t character = Glyphs_NextCharacter(&text);
        if (character == GLYPHS_NOT_UTF8) continue;
        glyph(out, character == ' ' ? 0 : Glyphs_Code(character));
    }
}

/* Codes as the game keeps a name (ending in FF). */
static void codes(Out *out, const unsigned char *text)
{
    while (text && *text != 0xFF && *text != 0xFE) {
        if (*text >= 0xF0 && *text < 0xF6) {
            glyph(out, ((text[0] - 0xF0) << 8) | text[1]);
            text += 2;
        } else if (*text < 0xF0) {
            glyph(out, *text++);
        } else {
            break;
        }
    }
}

static void number(Out *out, unsigned value)
{
    char digits[16];
    snprintf(digits, sizeof(digits), "%u", value);
    words(out, digits);
}

/* One of the port's own strings (text.h): a translation's, else `english`;
 * each %d the next number, each %s the next name (UTF-8). */
static void own(Out *out, int id, const char *english, const unsigned *numbers, const char *const *names)
{
    const u8 *text = Text_Own(id);
    int percent = Glyphs_Code('%'), letter_d = Glyphs_Code('d'), letter_s = Glyphs_Code('s'), used = 0, named = 0;
    if (!text) {
        const char *c;
        for (c = english; *c; c++) {
            if (c[0] == '%' && c[1] == 'd') { number(out, numbers ? numbers[used++] : 0); c++; }
            else if (c[0] == '%' && c[1] == 's') { words(out, names ? names[named++] : ""); c++; }
            else glyph(out, *c == ' ' ? 0 : Glyphs_Code((unsigned char)*c));
        }
        return;
    }
    while (*text != 0xFF) {
        int code = *text++;
        if (code >= 0xF6) break;                       /* letters and spaces only */
        if (code >= 0xF0) code = ((code - 0xF0) << 8) | *text++;
        if (code == percent && *text == letter_d) { text++; number(out, numbers ? numbers[used++] : 0); }
        else if (code == percent && *text == letter_s) { text++; words(out, names ? names[named++] : ""); }
        else glyph(out, code);
    }
}

/* A choice of two under up to three lines of heading, as the game lays one
 * out ({choice}, FB): its byte says the rows in all (the low three bits)
 * and how many of them head the choices (bits 4-5, which also make it a
 * choice of two; 0x40 as the password shop's EXCHANGE/QUIT has it), and
 * every row comes after it, since the command clears what the box showed.
 * Then the rows, and the jump the answer takes, which Tower_Retarget ends
 * the text at. `heading` is that many lines, FE between them. */
static void choices(Out *out, const char *const *heading, const int *heading_ids, int rows, int first,
                    const char *first_english, int second, const char *second_english)
{
    int row;
    put(out, 0xFB);
    put(out, 0x40 | rows << 4 | (rows + 2));
    for (row = 0; row < rows; row++) {
        if (heading_ids && heading_ids[row]) own(out, heading_ids[row], heading[row], NULL, NULL);
        else codes(out, (const unsigned char *)heading[row]);
        newline(out);
    }
    glyph(out, 0);
    own(out, first, first_english, NULL, NULL);
    newline(out);
    glyph(out, 0);
    own(out, second, second_english, NULL, NULL);
    newline(out);
    put(out, 0xFB);
    put(out, 0x80);
    put(out, 0); put(out, 0);
    put(out, 0); put(out, 0);
}

/* `text` (UTF-8) in lines of the box's width, broken at spaces, a page of
 * DIALOG_LINES at a time: a page wait (FA) between pages. A word longer
 * than a line is cut. */
static void wrapped(Out *out, const char *text, int lines_a_page)
{
    int line = 0;
    while (*text) {
        const char *word = text, *after;
        int letters = 0, room = (out->limit - out->x) / LETTER;
        if (*text == '\n') {
            text++;
            goto next_line;
        }
        if (*text == ' ') {
            if (out->x) glyph(out, 0);
            text++;
            continue;
        }
        after = word;
        while (*after && *after != ' ' && *after != '\n') {
            uint32_t character = Glyphs_NextCharacter(&after);
            if (character != GLYPHS_NOT_UTF8) letters++;
        }
        if (letters > room && out->x) goto next_line;
        while (text < after) {
            uint32_t character = Glyphs_NextCharacter(&text);
            if (character == GLYPHS_NOT_UTF8) continue;
            glyph(out, Glyphs_Code(character));
        }
        continue;
next_line:
        if (++line % lines_a_page == 0) {
            put(out, 0xFA);   /* the page is full: a button, then the next from its top */
            out->x = 0;
        } else {
            newline(out);
        }
        while (*text == ' ') text++;
    }
}

const unsigned char *Tower_Text(int id)
{
    if (id != TOWER_TEXT_ID || (D_8009B26C & 0x1F) != TOWER_MODE || !Tower_Config()) return NULL;
    return gTower.text + TEXT_START;
}

static uintptr_t text_address(const void *pointer)
{
#ifdef MEMORIES_TRANSLATED
    return GuestRuntime_EncodePointer((void *)pointer);
#else
    return (uintptr_t)pointer;
#endif
}

unsigned char *Tower_Retarget(const unsigned char *cursor)
{
    uintptr_t address = text_address(cursor), base = text_address(gTower.text);
    if (address >= base && address - base < TOWER_TEXT_SIZE) {
        gTower.text[0] = 0xFF;
        return gTower.text;
    }
    return NULL;
}

/* --- portraits -------------------------------------------------------------------- */

/* A 0x980-byte record (48 x 48 at 8 bits, then 64 colors) into cell
 * `cell` of the portrait grid, where the portrait effect draws it from:
 * x 0x380 + (cell % 5) * 24 words, y (cell / 5) * 48, the palette at
 * 0x380 + (cell / 16) * 64, 0xF0 + cell % 16 (Campaign_LoadScenePackage). */
static void upload(int cell, const unsigned char *record)
{
    RECT rect;
    rect.x = (s16)(0x380 + (cell % 5) * 24);
    rect.y = (s16)((cell / 5) * 48);
    rect.w = 24;
    rect.h = 48;
    LoadImage2(&rect, (u32 *)record);
    rect.x = (s16)(0x380 + (cell / 16) * 64);
    rect.y = (s16)(0xF0 + cell % 16);
    rect.w = 64;
    rect.h = 1;
    LoadImage2(&rect, (u32 *)(record + PORTRAIT_IMAGE));
    DrawSync(0);
}

/* Record `index` of the bank at WA sector `sector`, off the disc. */
static int disc_record(int sector, int index, unsigned char record[PORTRAIT_RECORD])
{
    static unsigned char sectors[3 * 2048];
    int offset = index * PORTRAIT_RECORD;
    if (!DiscArt_Read(sector + offset / 2048, sectors, (int)sizeof(sectors))) return 0;
    memcpy(record, sectors + offset % 2048, PORTRAIT_RECORD);
    return 1;
}

/* The F6 id of the face a line shows, uploaded now; 0 for none. */
static int portrait_for(const TowerLine *line, int floor_duelist)
{
    unsigned char record[PORTRAIT_RECORD];
    int speaker = line->speaker == TOWER_SPEAKER_FLOOR ? floor_duelist : line->speaker;
    if (line->portrait >= 0) {
        if (!disc_record(CAMPAIGN_PORTRAITS_SECTOR, line->portrait, record)) return 0;
        upload(SPEAKER_CELL, record);
        return PORTRAIT_EFFECT + SPEAKER_CELL;
    }
    if (speaker <= 0 || !Duelists_Valid(speaker)) return 0;
    if (Duelists_Portrait(speaker)) {
        memcpy(record, Duelists_Portrait(speaker), PORTRAIT_RECORD);
    } else if (!disc_record(FREE_DUEL_PORTRAITS_SECTOR, Duelists_BaseId(speaker), record)) {
        return 0;
    }
    upload(SPEAKER_CELL, record);
    return PORTRAIT_EFFECT + SPEAKER_CELL;
}

/* --- the boxes --------------------------------------------------------------------- */

static DuelEffectChannel *box(void) { return &D_800EB0F8[0]; }

static void close_box(void)
{
    if (box()->flags_34 & 0x8000) TextBox_Destroy((struct DuelEffectChannel *)box());
}

static void open_box(int x, int y, int width, int height, int flags)
{
    close_box();
    Tower_CreateBox(TOWER_TEXT_ID, x, y, width, height, BOX_WINDOW | flags);
}

/* Its text all shown, and, for one kept up, the button pressed. */
static int box_done(void)
{
    return (box()->flags_34 & TEXT_BOX_FLAG_DONE) && !(box()->flags_34 & BOX_KEPT);
}

/* --- what is told ------------------------------------------------------------------ */

static const TowerConfig *tower(void) { return Tower_Config(); }

static TowerStory story_of(int which, int floor)
{
    TowerStory none = {0, 0};
    const TowerConfig *config = tower();
    if (!config) return none;
    switch (which) {
    case STORY_INTRO: return config->intro;
    case STORY_ENDING: return config->ending;
    case STORY_LOSE: return config->lose;
    case STORY_BEFORE: return floor >= 0 && floor < config->floor_count ? config->floors[floor].before : none;
    case STORY_AFTER: return floor >= 0 && floor < config->floor_count ? config->floors[floor].after : none;
    }
    return none;
}

/* Tell story `which` (of floor `floor`), then go to step `then`. */
static void tell(int which, int floor, int then)
{
    gTower.story = (s16)which;
    gTower.floor = (s16)floor;
    gTower.line = 0;
    gTower.then = (s16)then;
    gTower.step = STEP_STORY;
    gTower.phase = 0;
}

/* The line on the box: its portrait (F6 id flags 0: in, as a scene's
 * dialogue brings one in) and its text, a page at a time. */
static void show_line(const TowerLine *line, int floor_duelist)
{
    Out out = begin(DIALOG_W - 2 * LETTER);
    int face = portrait_for(line, floor_duelist);
    if (face) {
        put(&out, 0xF6);
        put(&out, face);
        put(&out, 0);
    }
    wrapped(&out, line->text, DIALOG_LINES);
    put(&out, 0xFA);
    if (face) {
        put(&out, 0xF6);   /* out again with the box */
        put(&out, face);
        put(&out, 0x80);
    }
    finish(&out);
    open_box(DIALOG_X, DIALOG_Y, DIALOG_W, DIALOG_H, 0);
}

static void step_story(void)
{
    const TowerStory story = story_of(gTower.story, gTower.floor);
    const TowerConfig *config = tower();
    int duelist = gTower.floor >= 0 && gTower.floor < config->floor_count ? config->floors[gTower.floor].duelist : 0;
    if (gTower.phase == 0) {
        if (gTower.line >= story.count) {
            close_box();
            gTower.step = gTower.then;
            gTower.phase = 0;
            return;
        }
        show_line(&config->lines[story.first + gTower.line], duelist);
        gTower.phase = 1;
        return;
    }
    if (!box_done()) return;
    gTower.line++;
    gTower.phase = 0;
}

static unsigned deck_size(void)
{
    unsigned n = 0;
    int i;
    for (i = 0; i < TOWER_DECK; i++) n += gDuel_awPlayerDeck[i] != 0;
    return n;
}

/* The tower's name, the floor and who is on it, its rules: DUEL or LEAVE. */
static void show_status(void)
{
    const TowerConfig *config = tower();
    const TowerProgress *done = &gTower.progress;
    int floor = done->floor < config->floor_count ? done->floor : 0;
    const TowerFloor *one = &config->floors[floor];
    unsigned numbers[2];
    const char *names[1];
    /* The heading's rows composed apart (each ends in FF), then laid under
     * the choice command (choices). */
    static u8 rows[3][160];
    const char *heading[3];
    int count = 0, i;
    Out out;
    for (i = 0; i < 3; i++) {
        Out row = {rows[i], rows[i] + sizeof(rows[i]) - 4, 0, MENU_W - 2 * LETTER};
        if (i == 0) {
            words(&row, config->name);
        } else if (i == 1) {
            numbers[0] = (unsigned)floor + 1;
            numbers[1] = (unsigned)config->floor_count;
            names[0] = Duelists_Name(one->duelist);
            own(&row, TEXT_OWN_TOWER_FLOOR, "FLOOR %d OF %d: %s", numbers, names);
        } else {
            int any = 0;
            if (one->rules & TOWER_RULE_NO_FUSIONS) {
                own(&row, TEXT_OWN_TOWER_NO_FUSIONS, "NO FUSIONS", NULL, NULL);
                any = 1;
            }
            if (one->rules & TOWER_RULE_NO_EQUIPS) {
                if (any) { glyph(&row, 0); glyph(&row, 0); }
                own(&row, TEXT_OWN_TOWER_NO_EQUIPS, "NO EQUIPS", NULL, NULL);
                any = 1;
            }
            if (one->life_points[0] || one->life_points[1]) {
                if (any) { glyph(&row, 0); glyph(&row, 0); }
                numbers[0] = one->life_points[0] ? (unsigned)one->life_points[0] : 8000;
                numbers[1] = one->life_points[1] ? (unsigned)one->life_points[1] : 8000;
                own(&row, TEXT_OWN_TOWER_LIFE_POINTS, "LP YOU %d COM %d", numbers, NULL);
                any = 1;
            }
            if (done->completions) {
                if (any) { glyph(&row, 0); glyph(&row, 0); }
                numbers[0] = done->completions;
                own(&row, TEXT_OWN_TOWER_CLEARED, "CLEARED x%d", numbers, NULL);
                any = 1;
            }
            if (!any) break;
        }
        *row.at = 0xFF;
        heading[count++] = (const char *)rows[i];
    }
    out = begin(MENU_W - 2 * LETTER);
    choices(&out, heading, NULL, count, TEXT_OWN_TOWER_DUEL, "DUEL", TEXT_OWN_TOWER_LEAVE, "LEAVE");
    finish(&out);
    open_box(MENU_X, 0x78 - ((count + 2) * 12 + 16) / 2, MENU_W, (count + 2) * 12 + 16, 0);
}

static void step_status(void)
{
    if (gTower.phase == 0) {
        show_status();
        gTower.phase = 1;
        return;
    }
    if (!box_done()) return;
    close_box();
    gTower.phase = 0;
    if (gDialog_bChoice != 0) {
        gTower.step = STEP_LEAVE;
        return;
    }
    if (deck_size() < TOWER_DECK) {
        gTower.step = STEP_NO_DECK;
        return;
    }
    tell(STORY_BEFORE, gTower.progress.floor, STEP_DUEL);
}

/* The Free Duel screen's own answer to a short deck (string 8), in its
 * own box. */
static void step_no_deck(void)
{
    if (gTower.phase == 0) {
        SD_SEPlayFull(SE_BUZZ);
        close_box();
        Tower_CreateBox(8, 0x30, 0x6C, 0xE0, 0x10, 0x1028);   /* as the Free Duel screen's */
        gTower.phase = 1;
        return;
    }
    if (!box_done()) return;
    close_box();
    gTower.step = STEP_STATUS;
    gTower.phase = 0;
}

/* What the duel just fought did: FLOOR N CLEARED, the reward, a
 * checkpoint; or the loss and the floor it goes back to. */
static void show_notice(void)
{
    const TowerConfig *config = tower();
    const TowerFloor *one = &config->floors[gTower.floor];
    unsigned numbers[2];
    const char *names[1];
    int lines = 1, i;
    char name[128];
    Out out = begin(MENU_W - 2 * LETTER);
    if (gTower.outcome == OUTCOME_LOST) {
        numbers[0] = (unsigned)gTower.progress.floor + 1;
        own(&out, TEXT_OWN_TOWER_LOST, "YOU LOST. BACK TO FLOOR %d.", numbers, NULL);
    } else {
        numbers[0] = (unsigned)gTower.floor + 1;
        own(&out, TEXT_OWN_TOWER_FLOOR_WON, "FLOOR %d CLEARED!", numbers, NULL);
        /* The reward, the first time only (gTower.choice holds that). */
        if (gTower.choice) {
            for (i = 0; i < one->reward_card_count; i++) {
                newline(&out);
                codes(&out, Cards_NameCodes(one->reward_cards[i]));
                lines++;
            }
            if (one->reward_starchips) {
                newline(&out);
                numbers[0] = one->reward_starchips;
                own(&out, TEXT_OWN_TOWER_STARCHIPS, "%d STARCHIPS", numbers, NULL);
                lines++;
            }
            for (i = 0; i < one->reward_duelist_count; i++) {
                newline(&out);
                snprintf(name, sizeof(name), "%s", Duelists_Name(one->reward_duelists[i]));
                names[0] = name;
                own(&out, TEXT_OWN_TOWER_UNLOCKED, "%s UNLOCKED", NULL, names);
                lines++;
            }
        }
        if (gTower.outcome == OUTCOME_CHECKPOINT) {
            newline(&out);
            own(&out, TEXT_OWN_TOWER_CHECKPOINT, "CHECKPOINT", NULL, NULL);
            lines++;
        }
        if (gTower.outcome == OUTCOME_COMPLETED) {
            newline(&out);
            own(&out, TEXT_OWN_TOWER_COMPLETE, "THE TOWER IS CLEARED!", NULL, NULL);
            lines++;
        }
    }
    finish(&out);
    open_box(MENU_X, 0x78 - (lines * 12 + 16) / 2, MENU_W, lines * 12 + 16, BOX_KEPT);
}

static void step_notice(void)
{
    if (gTower.phase == 0) {
        show_notice();
        gTower.phase = 1;
        return;
    }
    if (!box_done()) return;
    close_box();
    gTower.phase = 0;
    if (gTower.outcome == OUTCOME_CHECKPOINT) gTower.step = STEP_SAVE_ASK;
    else if (gTower.outcome == OUTCOME_COMPLETED) tell(STORY_ENDING, -1, STEP_STATUS);
    else gTower.step = STEP_STATUS;
    gTower.outcome = OUTCOME_NONE;
}

static void step_save_ask(void)
{
    Out out;
    if (gTower.phase == 0) {
        static const char *const question[] = {"SAVE YOUR PROGRESS?"};
        static const int question_id[] = {TEXT_OWN_TOWER_SAVE};
        out = begin(MENU_W - 2 * LETTER);
        choices(&out, question, question_id, 1, TEXT_OWN_TOWER_YES, "YES", TEXT_OWN_TOWER_NO, "NO");
        finish(&out);
        open_box(MENU_X, 0x78 - 26, MENU_W, 3 * 12 + 16, 0);
        gTower.phase = 1;
        return;
    }
    if (!box_done()) return;
    close_box();
    gTower.phase = 0;
    gTower.step = gDialog_bChoice == 0 ? STEP_SAVE : STEP_STATUS;
}

/* The game's own save: the memory card dialog, as the credits ask for
 * theirs (Main_RunCredits), which on the PC is the slot menu. */
static void step_save(void)
{
    int answer;
    if (gTower.phase == 0) {
        SaveData_RequestWrite();
        gTower.phase = 1;
    }
    answer = MemCardDialog_Poll();
    if (answer == 0) return;
    if (answer == 2) {             /* again (the credits' retry) */
        gTower.phase = 0;
        return;
    }
    gTower.step = STEP_STATUS;
    gTower.phase = 0;
}

/* The floor's duel, armed as the Free Duel screen arms one
 * (screen_runtime.c): func_80024DC8 clears the field and the way back, and
 * sets the duel's mode; then the floor's field, and the way back here
 * rather than to the Free Duel screen, whose return flag 0x80 would count
 * the result on its grid (gFreeDuel_bReturnFlags is left alone). */
static void step_duel(void)
{
    const TowerConfig *config = tower();
    int floor = gTower.progress.floor < config->floor_count ? gTower.progress.floor : 0;
    const TowerFloor *one = &config->floors[floor];
    close_box();
    Tower_Background(0);
    SD_SEPlayFull(SE_CHOOSE);
    SD_BGMFadeOut();
    gTower.armed = (s16)(floor + 1);
    gTower.rules = (s16)one->rules;
    func_80024DC8(-1, one->duelist, 0x6000, 0x6000);
    gDuel_bTerrain = (u8)one->terrain;
    D_8009B368 = TOWER_MODE;
    LOG(LOG_MODS, "tower: floor %d against %s (field %d, rules %d)", floor + 1, Duelists_Name(one->duelist),
        one->terrain, one->rules);
}

static void step_leave(void)
{
    Tower_Background(0);
    close_box();
    SD_SEPlayFull(8);
    SD_BGMFadeOut();
    D_8009B268 = 1;
    D_8009B26D = SECOND_MENU;
    D_8009B26C = MAIN_MODE_MENU;
}

/* The duel just come back from: the progress moves on, the reward is
 * given the first time, and what to tell is chosen. */
static void take_result(void)
{
    const TowerConfig *config = tower();
    int floor = gTower.armed - 1, won = D_8009B362 == 0, i;
    TowerOutcome outcome;
    gTower.armed = 0;
    gTower.rules = 0;
    if (floor < 0 || floor >= config->floor_count) return;
    outcome = Tower_ApplyResult(config, &gTower.progress, floor, won);
    gTower.floor = (s16)floor;
    gTower.choice = (s16)outcome.first_clear;   /* the notice lists the reward the first time */
    LOG(LOG_MODS, "tower: floor %d %s; next %u, checkpoint %u", floor + 1, won ? "won" : "lost",
        (unsigned)gTower.progress.floor, (unsigned)gTower.progress.checkpoint);
    if (!won) {
        gTower.outcome = OUTCOME_LOST;
        tell(STORY_LOSE, floor, STEP_NOTICE);
        return;
    }
    if (outcome.first_clear) {
        const TowerFloor *one = &config->floors[floor];
        SaveDataState *save = (SaveDataState *)gDuel_awPlayerDeck;
        for (i = 0; i < one->reward_card_count; i++) Duel_AwardCard(one->reward_cards[i]);
        if (one->reward_starchips) {
            unsigned cap = Tables_StarchipCap(), now = save->starchips;
            save->starchips = now + one->reward_starchips > cap ? cap : now + one->reward_starchips;
        }
        Tower_UnlockRetailDuelists(one);
        LOG(LOG_MODS, "tower: floor %d's reward: %d cards, %u starchips (now %u), %d duelists", floor + 1,
            one->reward_card_count, one->reward_starchips, (unsigned)save->starchips, one->reward_duelist_count);
    }
    gTower.outcome = outcome.completed ? OUTCOME_COMPLETED : outcome.checkpoint ? OUTCOME_CHECKPOINT : OUTCOME_WON;
    tell(STORY_AFTER, floor, STEP_NOTICE);
}

void Tower_Enter(void)
{
    gTower.step = STEP_BEGIN;
    gTower.phase = 0;
    gTower.armed = 0;
    gTower.outcome = OUTCOME_NONE;
    D_8009B26C = TOWER_MODE;
}

void Tower_RunMode(void)
{
    const TowerConfig *config = tower();
    if (!config) {
        /* A state of a run with the tower's mod, loaded without it. */
        step_leave();
        return;
    }
    if ((D_8009B26C & 0x40) == 0) {
        int song = config->music >= 0 ? config->music : TOWER_DEFAULT_SONG;
        D_8009B26C |= 0x40;
        Tower_Frame();   /* the save's progress is of this tower (tower.c) */
        Tower_Background(1);
        SD_BGMPlay(0x7000 + (u32)song);
        gTower.step = STEP_BEGIN;
        gTower.phase = 0;
        if (gTower.armed) {
            take_result();
        } else if (config->intro.count && !(gTower.progress.flags & TOWER_PROGRESS_INTRO_SEEN)) {
            gTower.progress.flags |= TOWER_PROGRESS_INTRO_SEEN;
            tell(STORY_INTRO, -1, STEP_STATUS);
        } else {
            gTower.step = STEP_STATUS;
        }
        Fade_WaitIn();
        return;
    }
    {   /* For the log (MEMORIES_TRACE=mods), which the tests follow. */
        static const char *const names[] = {"begin", "story", "notice", "status", "save?", "save", "duel", "leave",
                                            "no deck"};
        static int told = -1;
        int line = gTower.step == STEP_STORY ? gTower.story * 100 + gTower.line : 0;
        if (gTower.step * 10000 + line != told && gTower.step >= 0 && gTower.step <= STEP_NO_DECK) {
            told = gTower.step * 10000 + line;
            LOG(LOG_MODS, "tower: %s (floor %u, checkpoint %u%s)", names[gTower.step],
                (unsigned)gTower.progress.floor + 1, (unsigned)gTower.progress.checkpoint + 1,
                gTower.step == STEP_STORY ? ", a line" : "");
        }
    }
    switch (gTower.step) {
    case STEP_STORY: step_story(); break;
    case STEP_NOTICE: step_notice(); break;
    case STEP_STATUS: step_status(); break;
    case STEP_NO_DECK: step_no_deck(); break;
    case STEP_SAVE_ASK: step_save_ask(); break;
    case STEP_SAVE: step_save(); break;
    case STEP_DUEL: step_duel(); return;
    case STEP_LEAVE: step_leave(); return;
    default: gTower.step = STEP_STATUS; gTower.phase = 0; break;
    }
    func_80039794();
}
