#ifndef MEMORIES_PC_TOWER_H
#define MEMORIES_PC_TOWER_H
/* Tower mode: a mod's "tower" key (notes/tower-mode.md).
 *
 * A tower is a run of duels, its floors, that the title's second menu
 * offers once a mod with a "tower" is on: each floor an opponent with a
 * field, a deck, starting LP and rules of its own (no fusions, no equips),
 * a reward, and lines of story before and after it, told in the game's
 * dialogue box with the speaker's portrait. Progress is the save's: it is
 * kept beside the memory card's block, as the card packs' is, and in every
 * save state.
 *
 * This header is in three parts: the tower as a mod writes it (read once at
 * startup, tower_config.c), the progress a save has in it and the rules it
 * follows (tower_config.c too, so the tests reach both without the game),
 * and what the game asks of it (tower.c, tower_screen.c). */
#include "tower_state.h"
#include <stdint.h>
#include <stdio.h>

struct JsonValue;

#define TOWER_REF 64                  /* a duelist or card as written: an id, a name, an identity */
#define TOWER_NAME 64
#define TOWER_LABEL 32                /* the menu button's words (title_config.h, TITLE_LABEL) */
#define TOWER_DECK 40
#define TOWER_REWARD_CARDS 10
#define TOWER_REWARD_DUELISTS 8
#define TOWER_LINE_TEXT 1024          /* one line of story as written */
#define TOWER_CAMPAIGN_PORTRAITS 25   /* the campaign's dialogue portraits (campaign_scene_package.h) */

/* The title's second menu returns this for the tower's button
 * (title_menu.c); none of the game's eleven choices nor the debug menu's
 * 11, so Main_ApplyMenuSelection's PC side takes it. */
#define TOWER_MENU_SELECTION 12
/* The tower's own screen: a mode past the game's seventeen, which Main_Loop
 * runs itself (main_loop.c) rather than growing the game's table. */
#define TOWER_MODE 17

enum { TOWER_RULE_NO_FUSIONS = 1, TOWER_RULE_NO_EQUIPS = 2 };
enum { TOWER_ON_LOSE_CHECKPOINT, TOWER_ON_LOSE_RESTART };
enum { TOWER_UNLOCK_ALWAYS, TOWER_UNLOCK_CONDITIONS, TOWER_UNLOCK_NEVER };
/* A line's speaker: a duelist id (1 up), or one of these. */
enum { TOWER_SPEAKER_NONE = 0, TOWER_SPEAKER_FLOOR = -1 };

/* "unlock": "always" (the default), "after_ending", or an object of the
 * duelists' conditions (pc/free_duel/duelists.h) and "after_ending": true.
 * Written wrong, the tower stays locked (NEVER), so nothing opens by
 * mistake. */
typedef struct {
    int kind;                         /* TOWER_UNLOCK_* */
    int after_ending;                 /* the save has seen the ending (Tower_EndingSeen) */
    char beat[TOWER_REF];
    int wins;
    int story;                        /* -1 for none */
    char card[TOWER_REF];
    int copies;
} TowerUnlock;

/* One line of story: what one dialogue box says, page after page. */
typedef struct {
    int speaker;                      /* TOWER_SPEAKER_*, or a duelist whose portrait shows */
    int portrait;                     /* one of the campaign's 25, 0-24; -1 for the speaker's */
    char *text;                       /* UTF-8, as written */
} TowerLine;

/* A run of lines (TowerConfig.lines). */
typedef struct {
    int first, count;
} TowerStory;

typedef struct {
    int duelist;                      /* the opponent's id */
    int terrain;                      /* 1-6 (Forest ... Yami), 0 none, as Free Duel has it */
    int deck_given;                   /* the opponent plays `deck`, not one dealt from its pool */
    unsigned short deck[TOWER_DECK];
    int life_points[2];               /* the player's and the opponent's; 0 the usual */
    int rules;                        /* TOWER_RULE_* */
    int reward_cards[TOWER_REWARD_CARDS];
    int reward_card_count;
    unsigned reward_starchips;
    int reward_duelists[TOWER_REWARD_DUELISTS];
    int reward_duelist_count;
    int checkpoint;                   /* won, a loss after it comes back to the next floor */
    TowerStory before, after;
} TowerFloor;

typedef struct {
    char mod[64];                     /* whose tower it is */
    char name[TOWER_NAME];
    char label[TOWER_LABEL];
    int music;                        /* the screen's song, -1 the game's (TOWER_DEFAULT_SONG) */
    TowerUnlock unlock;
    int on_lose;                      /* TOWER_ON_LOSE_* */
    TowerStory intro, ending, lose;
    int floor_count;
    TowerFloor floors[TOWER_MAX_FLOORS];
    TowerLine *lines;
    int line_count, line_room;
} TowerConfig;

/* --- the tower as written (tower_config.c) -------------------------------- */

/* Back to no tower. */
void Tower_Reset(void);
/* One mod's manifest: its "tower", if it has one, over the one read before
 * (the last mod in load order to declare a tower has the menu's). Names a
 * duelist and a card are resolved now, so Duelists_Build and Cards_Build
 * have run. */
void Tower_Read(const char *mod, const struct JsonValue *manifest);
/* Every active mod's, in load order; once, from Cards_Build. */
void Tower_Build(void);
/* The tower this run has, or NULL. */
const TowerConfig *Tower_Config(void);
/* The mod whose tower it is, or NULL: its button is "<mod>:tower" (title_config.c). */
const char *Tower_Mod(void);
/* The button's words: the tower's "label", else "TOWER". */
const char *Tower_Label(void);

/* --- the progress a save has (tower_config.c) ----------------------------- */

/* TowerProgress (tower_state.h): what a save holds of the tower, kept with
 * the game's own variables so a save state has it, and beside the save as
 * tower/<slot token>.txt. */
enum { TOWER_PROGRESS_INTRO_SEEN = 1 };

/* A number standing for the tower: its mod's id and its name. */
uint32_t Tower_Identity(const TowerConfig *config);

int Tower_FloorCleared(const TowerProgress *progress, int floor);
/* A fresh progress, of the save with duelist code `owner`. */
void Tower_ClearProgress(TowerProgress *progress, uint32_t owner, uint32_t tower);

/* What a duel's result did to the progress (Tower_ApplyResult). */
typedef struct {
    int won;
    int first_clear;                  /* won for the first time: the floor's reward */
    int completed;                    /* the last floor won: the ending */
    int checkpoint;                   /* a checkpoint reached: offer a save */
    int back_to;                      /* lost: the floor the player goes back to */
} TowerOutcome;
/* The duel on floor `floor` was won (`won`) or lost: the next floor, a
 * checkpoint, the cleared floors and the completions move on. A lost duel
 * sends the player back to the last checkpoint, or to the first floor with
 * "on_lose": "restart". After the last floor the tower begins again from
 * the first, its floors still cleared. */
TowerOutcome Tower_ApplyResult(const TowerConfig *config, TowerProgress *progress, int floor, int won);

/* The progress file: one section per tower, "tower <mod>" up to "end"
 * (notes/tower-mode.md). Tower_ReadProgress takes the section of
 * `identity` into `progress` (cleared first) and returns 1 if there was
 * one; the others' lines go to `foreign` (NUL-terminated, malloc'd, NULL
 * for none), to be written back with it. */
int Tower_ReadProgress(FILE *file, const char *mod, uint32_t identity, TowerProgress *progress, char **foreign);
void Tower_WriteProgress(FILE *file, const char *mod, const TowerProgress *progress, const char *foreign);

/* --- what the game asks (tower.c) ------------------------------------------ */

/* Whether the running save has seen the ending: Main_RunCredits sets the
 * low two bits of the save's byte 0x3DE (save_data.h, field_3DE) as the
 * ending begins and the game saves it there; NEW GAME clears the block. */
int Tower_EndingSeen(const void *state);
/* Whether the tower's "unlock" holds for the running save. */
int Tower_Unlocked(void);

/* Whether the floor's duel is the one being fought, and its rules: 0
 * outside a tower duel. Duel_CheckFusion / Duel_CheckEquip and
 * CardRules_Fusion / CardRules_Equip refuse what a rule forbids, for the
 * player, the CPU and the fusion helper alike. */
int Tower_DuelRules(void);
/* Whether the floor's `rule` refuses what `a` with `b` would make
 * (`result`): 1 when it does, which the log says (the first few). */
int Tower_Refuses(int rule, int a, int b, int result);
/* Duel_InitSideStates: the floor's starting LP for `side` (0 the player),
 * or `usual`. */
int Tower_StartingLifePoints(int side, int usual);
/* Duel_ShuffleDeck: the floor's deck for the opponent; 0 when it has none. */
int Tower_OpponentDeck(unsigned short cards[TOWER_DECK]);

/* The "tower" condition of a duelist's "unlock" (duelists.h): floor
 * `floor` (1 the first) won on this save, or the whole tower with 0. */
int Tower_ConditionMet(int floor);
/* A duelist with "tower_only": whether the tower has let it out: a won
 * floor names it as its opponent or in its "reward" "unlock_duelist". */
int Tower_FreesDuelist(int duelist);

/* A won floor's reward: the duelists of the disc it names appear on the
 * Free Duel grid (their 0x6E0 + id flag); an added one is let out by
 * Tower_FreesDuelist. */
void Tower_UnlockRetailDuelists(const TowerFloor *floor);

/* Main_ApplyMenuSelection took TOWER_MENU_SELECTION. */
void Tower_Enter(void);
/* Main_Loop, mode TOWER_MODE (tower_screen.c). */
void Tower_RunMode(void);
/* Text_Resolve: the screen's text by string id, NULL for any other. */
const unsigned char *Tower_Text(int id);
/* Text_Retarget: the screen's choices end its text. NULL for a stream not
 * the screen's. */
unsigned char *Tower_Retarget(const unsigned char *cursor);

/* The running save was loaded (SaveCards_Applied) or written
 * (SaveCards_Saved): its progress, beside it. */
void Tower_SaveLoaded(const void *state);
void Tower_SaveWritten(const void *state);
/* Once a frame: NEW GAME writes a new duelist code without loading, which
 * is a new save with no progress. */
void Tower_Frame(void);
/* The towers other than this run's, which a save's file keeps (a state's
 * chunk "tower-foreign"). */
struct MemoriesState;
void Tower_State(struct MemoriesState *state);

#endif
