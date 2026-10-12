#ifndef MEMORIES_PC_TOWER_STATE_H
#define MEMORIES_PC_TOWER_STATE_H
/* What the tower keeps with the game's own variables (gTower,
 * src/pc/game/zz_tower_storage.c): the save's progress, the screen's place
 * in what it shows, the duel it armed, and the text its boxes show. A game
 * unit's data is in every save state and at a fixed address the game's text
 * boxes can point into, which is why this is not a host struct; plain
 * numbers and bytes only (no pointers: G32). */
#include "types.h"

#define TOWER_MAX_FLOORS 99
#define TOWER_TEXT_SIZE 2048              /* the box's text, in the game's codes */

/* What a save holds of the tower, kept beside it as tower/<slot token>.txt. */
typedef struct {
    u32 owner;                            /* the duelist code of the save it is of */
    u32 tower;                            /* Tower_Identity of the tower it is of; 0 none yet */
    u16 floor;                            /* the next floor to fight, 0 the first */
    u16 checkpoint;                       /* where a loss sends the player back to */
    u16 completions;                      /* times the whole tower was won */
    u16 flags;                            /* TOWER_PROGRESS_* */
    u8 cleared[(TOWER_MAX_FLOORS + 7) / 8]; /* floors ever won: rewards are given once */
    u8 pad[3];
} TowerProgress;

typedef struct {
    TowerProgress progress;
    s16 step;                             /* the screen's state (tower_screen.c) */
    s16 phase;                            /* within it */
    s16 armed;                            /* 1 + the floor whose duel is armed; 0 none */
    s16 rules;                            /* that duel's TOWER_RULE_* */
    s16 story;                            /* the story being told (TOWER_STORY_*) */
    s16 line;                             /* its line on the box */
    s16 then;                             /* the step after the story */
    s16 choice;                           /* the answer the box gave */
    s16 outcome;                          /* what the last duel did (TOWER_OUTCOME_*) */
    s16 floor;                            /* the floor the last duel was on */
    s16 portrait;                         /* the cell the box's portrait is in, -1 none */
    s16 pad;
    u8 text[TOWER_TEXT_SIZE];
} TowerState;

extern TowerState gTower;

/* Text channel 0's box, and the title's wall behind the screen (1 on, 0
 * off): game code (zz_tower_storage.c). */
void *Tower_CreateBox(s32 id, s32 x, s32 y, s32 width, s32 height, s32 flags);
void Tower_Background(s32 on);

#endif
