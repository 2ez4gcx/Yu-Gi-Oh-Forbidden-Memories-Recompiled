# Tower mode

A mod can add a **tower**: a run of duels, its *floors*, that the title's
second menu offers once the mod is on. Each floor is an opponent with a field,
a deck, starting LP and rules of its own (no fusions, no equips), a reward and
lines of story before and after it, told in the game's dialogue box with the
speaker's portrait. A save keeps how far it climbed. Without a tower mod the
game is the same, frame for frame (checked against a build without tower mode:
the title's two menus, the cursor walked through them).

```json
{
    "id": "shadow-tower",
    "name": "The Shadow Tower",
    "min_api": 13,
    "duelists": [
        {"id": "ghost", "copy": "Simon Muran", "name": "Ghost Simon", "tower_only": true}
    ],
    "tower": {
        "name": "The Shadow Tower",
        "label": "SHADOW TOWER",
        "unlock": "after_ending",
        "on_lose": "checkpoint",
        "intro": [
            "Three floors stand between you and the top of the tower.",
            {"text": "Win them all, my prince.", "portrait": 0}
        ],
        "ending": "The tower is yours.",
        "floors": [
            {
                "duelist": "Simon Muran",
                "checkpoint": true,
                "life_points": {"opponent": 4000},
                "reward": {"cards": ["Kuriboh"], "starchips": 25},
                "before": "Show me what you have learned.",
                "after": "Well fought. Climb on."
            },
            {
                "duelist": "shadow-tower:ghost",
                "field": "Dark",
                "no_fusions": true,
                "deck": {"Job-change Mirror": 20, "Monster Eye": 20},
                "before": ["Nobody fuses on this floor.", {"text": "Not even me.", "speaker": "shadow-tower:ghost"}],
                "after": "You may find me in Free Duel now."
            },
            {
                "duelist": "Seto",
                "no_equips": true,
                "reward": {"unlock_duelist": "Nitemare"}
            }
        ]
    }
}
```

The FM Editor writes all of it from its **Tower** tab, and the tests' own
tower is `tests/pc/tower/tower-test/mod.json`.

## What the player sees

The second menu (CAMPAIGN to SAVE, once a game is loaded) has one more button,
the tower's `label` (`TOWER` without one), in the entries' look. While the
tower's `unlock` does not hold for the save loaded, it is drawn **dimmed** and
choosing it buzzes; it is never hidden, so the menu does not move as a save
opens it. The button is the mod's own `"<mod>:tower"` (made before the mod's
`menu` is read), so the mod's `menu` may move it, give it a picture, put it in
its `order` by `"tower"`, or hide it. A button of a mod's own may take the
action `"tower"` too, in the second menu only ([The title's menus](modding.md#the-titles-menus)).

The tower's screen is the game's own pieces, drawn by the game's own code:

- behind it, the title's wall (the main menu's package and
  `MainMenu_DrawFrontendBackground`), without the dark-to-light shade, so the
  boxes keep their window to the bottom;
- the game's text boxes (their dark window, letters, page arrow, choice cursor
  and sounds): the tower's name, `FLOOR 2 OF 3: Ghost Simon`, the floor's rules
  (`NO FUSIONS  LP YOU 100 COM 8000`, `CLEARED x1` once the top is reached),
  over **DUEL** and **LEAVE**; after a duel, `FLOOR 1 CLEARED!` with the reward
  and `CHECKPOINT` (four lines a page, a button between), or `YOU LOST. BACK
  TO FLOOR 2.`; at a checkpoint and after the top, `SAVE YOUR PROGRESS?` and
  then the game's own save (the port's slot menu);
- the story in the campaign's dialogue box at the foot of the screen, page by
  page, with the speaker's portrait sliding in above it as a scene's does.

DUEL tells the floor's `before`, then the duel, armed as the Free Duel screen
arms one (the deck screen first); the game comes back to the tower after it.
A short deck gets the Free Duel screen's own answer. LEAVE goes back to the
second menu, the cursor on the tower's button.

## The tower

| Key | Meaning |
|---|---|
| `name` | its name on the screen (the mod's id without one; 63 bytes) |
| `label` | the menu button's words, 31 bytes at most (UTF-8); `TOWER` without one |
| `music` | the screen's song, as `title`'s `music` takes it (`0x000`-`0xFFF`); the Free Duel screen's (`0x2C0`) without one |
| `unlock` | when the button opens it: `"always"` (the default), `"after_ending"`, or an object of conditions (below) |
| `on_lose` | `"checkpoint"` (the default): a loss sends the player back to the floor after the last checkpoint won; `"restart"`: back to floor 1, the checkpoints forgotten |
| `intro` | told the first time the tower is entered on a save |
| `ending` | told when the last floor is won |
| `lose` | told after a loss, before where it goes back to |
| `floors` | the floors, bottom first, at most 99. At least one must be fought, else the mod has no tower |

**Several towers.** One tower a run: the **last mod in load order** that has a
`tower` has the menu's, and the one it replaced gets a note in the Mods window.
A save's progress in each tower is kept apart (below), so turning mods on and
off loses nothing.

### `unlock`

| Value | Opens |
|---|---|
| `"always"` (or none, or `{}`) | from the start |
| `"after_ending"` | once the save has seen the ending |
| `{"beat": ..., "wins": n, "story": n, "card": ..., "copies": n, "after_ending": true}` | when every condition holds: the duelists' own ([More duelists](more-duelists.md), read from the save's records, trunk and flags), and the ending when `after_ending` is `true` |

Written wrong (a word that is not one of those, a number that is not one), the
tower stays locked, so nothing opens by mistake; the Mods window says why.

**The ending, in the save.** `Main_RunCredits`
(`src/game/main_run_credits.c`, `0x8002DA1C`) begins the ending by setting the
low two bits of the save block's byte `0x3DE` (`save_state->field_3DE |= 3`,
`SAVE_DATA_CREDITS_FLAGS_OFFSET` in `save_data.h`), parking
`campaign_scene_index` at `0x30` and clearing the story flags `0x20`-`0x11F`,
and then asks for the save (`SaveData_RequestWrite`). Nothing on the US disc
reads the byte back (the Japanese release opens its PocketStation menu by it,
[the game](research/the-game.md#79-the-ending)); it is inside the secondary
checksum's range (`0x380`-`0x3EB`), so it is written with the save, and NEW
GAME clears the whole block (`NameEntry_Main` clears `0x801D0000`-`+0x3000`).
`"after_ending"` reads those two bits (`Tower_EndingSeen`). The story flags are
no use for it: the ending clears them, and `campaign_scene_index` is `0x30`
after NEW GAME too. A player who turns the ending's save down has not "seen
the ending" for the tower, as for the Japanese game.

### A floor

| Key | Meaning |
|---|---|
| `duelist` | the opponent: an id, a name, or an added duelist's identity (`"mod:id"`). A floor without one it can find is left out |
| `field` | `none` (the default, as Free Duel), `Forest`, `Wasteland`, `Mountain`, `Meadow` (`Sogen`), `Sea` (`Umi`), `Dark` (`Yami`), or 1-6 |
| `deck` | the opponent's deck: `{card: copies}` as a fixed deck is written, or a list of forty cards; exactly forty, else its own is dealt. It comes before a fixed deck of `decks` |
| `life_points` | the starting LP of both sides, or `{"player": ..., "opponent": ...}`, 1 to 65535. Between Game > Cheats > Starting LP (which wins) and a mod's `limits` |
| `no_fusions`, `no_equips` | `true`: nothing fuses, nothing equips, below |
| `reward` | `{"cards": [...], "starchips": n, "unlock_duelist": ...}`, given the **first** time the floor is won (`card` takes one card) |
| `checkpoint` | `true`: won, a loss later comes back to the next floor, and the tower offers a save |
| `before`, `after` | told before the duel, and after a win |

A won duel moves the climb on; the top tells the `ending`, offers a save, and
sets the tower back to floor 1, its floors still won (rewards are not given twice), the
status counting the climbs (`CLEARED x2`). The usual drops and starchips of a
won duel come as in any duel. A tower duel is **not** a Free Duel: it is never
counted on the Free Duel grid's win/loss records (the Free Duel screen's
return flag `0x80` would), which is why the duelists' unlock has a `tower`
condition of its own.

**The rules are the game's own checks.** `no_fusions` and `no_equips` are
answered in `CardRules_Fusion` / `CardRules_Equip` and in `Duel_CheckFusion` /
`Duel_CheckEquip` (`src/game/duel_card_checks.c`), which the player's card
placement, the CPU's planning (`Ai_CompleteFusion`, `AiScript_FindEquipTarget`
and `ai_turn_action.c`) and the fusion helper all ask: a fusion of the
player's fails as an unmatched pair fails, and the CPU never plans one. A code
mod's answer to `MEMORIES_EVENT_FUSION` / `EQUIP` cannot overrule a floor's
rule. The rule holds only in the floor's own duel (armed by the tower, the
game in that duel or its 3D battle, on its way back to the tower), never in
Free Duel or the campaign.

`reward.unlock_duelist`: a duelist of the disc gets its Free Duel flag
(`0x6E0 + id`, as its "you won" text gives it; not the campaign's `0x1F + id`,
which the event script tests to choose scenes); an added duelist marked
`tower_only` is let out (below).

### Story

A story is one line of text, or a list of them; each line is one dialogue box,
cut into pages of three lines at the box's width, a button between them. A
line may be an object instead:

| Key | Meaning |
|---|---|
| `text` | the words (UTF-8; letters the game and the mods' fonts lack are left out) |
| `speaker` | whose portrait shows beside it: a duelist (id, name, identity), `"none"` for nobody's, `"opponent"` for the floor's |
| `portrait` | one of the campaign's 25 dialogue portraits, `0`-`24` (0 is Simon Muran), in place of the speaker's |

A floor's `before` and `after` lines are the floor's opponent's unless they say
otherwise; `intro`, `ending` and `lose` lines are nobody's.

How it is drawn: the box is the campaign's dialogue box (`Script_OpShowDialog`'s
place, `0x10, 0xB0`, `0x120 x 0x30`), and the portrait is the dialogue's own
portrait effect (text code `F6 41`-`59`, `Text_HandleDisplayEffectCommand`),
which draws from the 48 x 48 cells of VRAM the campaign's scene package fills
(`Campaign_LoadScenePackage`, [dialog portrait bank](dialog-portrait-bank.md)).
The tower puts the face it needs into the first cell itself, a duelist's Free
Duel portrait (a mod's own, else its base's, from the disc's Free Duel package,
WA sector `0x1EAA`) or a campaign portrait (WA `0xF35000`), so the scene
package is never loaded outside the campaign. That cell is clear of the
title's wall (VRAM x 960 up), which the screen draws behind.

## Duelists of the tower's alone

A duelist's `unlock` takes `"tower": true` (the tower won to its top) or a
floor (`1` up) won on the save, in **this run's** tower (the last mod's; with
no tower on, the condition does not hold); `"tower_only": true` on an added duelist keeps
it off the Free Duel grid until a won floor fights it or names it in its
reward's `unlock_duelist` ([More duelists](more-duelists.md)). The tower fights
it whatever its unlock says.

## A save's progress

The climb of each tower is the save's: the next floor, the checkpoint, the
floors ever won (for rewards and unlocks), the climbs to the top, whether the
intro was told.

- It is **game data** (`gTower`, `src/pc/game/zz_tower_storage.c`), so every
  save state has it, and a state saved in a tower duel keeps its floor and
  rules. It is the last game unit, so no game variable moves; it does make the
  game's zeroed variables longer, and a state restores those only at this
  build's size, so a save state made by a build before tower mode loads
  without them, as after `drops.c` came in (memory card saves are not
  touched).
- Beside the save, as `tower/<slot token>.txt` in the user folder, read when
  the save is loaded and written when it is saved (as the card packs' progress
  is), one section per tower by its mod's id:

  ```text
  tower shadow-tower
  identity 1A2B3C4D
  owner 0012ABCD
  floor 1
  checkpoint 1
  completions 0
  flags 1
  cleared 0
  end
  ```

  `identity` is the mod's id and the tower's name: the same mod with another
  tower begins again. `owner` is the save's duelist code; NEW GAME is a save
  with no climb. A section of a tower whose mod is off this run is kept as it
  was and written back, and a save state carries those lines too (chunk
  `tower-foreign`). Saved in a run with no tower at all (its mod turned
  off), the save's file follows it to the slot it is saved to, untouched (a
  save state knows which save was loaded last, chunk `tower-loaded`). The
  file of a slot saved over goes when a save is written with a tower on or
  carried over; until then it is only left behind, read by no save.
- The climb counts when the game is **saved**, as everything else does: the
  tower offers a save at each checkpoint and after its top. A loss sends the climb in memory
  back at once; LOAD gives the save's.

## The screen's words

The port's own strings `FE70`-`FE7F` ([translation](translation.md)), which a
translation may give, letters and spaces and `%d` / `%s` only: `FE70` FLOOR %d
OF %d: %s, `FE71` DUEL, `FE72` LEAVE, `FE73` NO FUSIONS, `FE74` NO EQUIPS,
`FE75` LP YOU %d COM %d, `FE76` CLEARED x%d, `FE77` FLOOR %d CLEARED!, `FE78`
%d STARCHIPS, `FE79` %s UNLOCKED, `FE7A` CHECKPOINT, `FE7B` THE TOWER IS
CLEARED!, `FE7C` YOU LOST. BACK TO FLOOR %d., `FE7D` SAVE YOUR PROGRESS?,
`FE7E` YES, `FE7F` NO. A line is the box's width: 32 letters.

## Which game

A tower needs mod API 13 (`"min_api": 13`, [Which game a mod
needs](modding.md#which-game-a-mod-needs)): `tower`, a duelist's `tower_only`
or `unlock` `tower`, a menu item's action `tower`. An older game would play the
mod without its tower.

## How it is built

| Where | What |
|---|---|
| `src/pc/tower/tower_config.c` | the key as read, with its notes; the progress and its file format |
| `src/pc/tower/tower.c` | the unlock, the floor's rules, LP and deck in its duel, the duelists' conditions, the save's file and the state chunk |
| `src/pc/tower/tower_screen.c` | the screen: its steps, text, portraits, the duel's arming and its result |
| `src/pc/game/zz_tower_storage.c` | `gTower`, and the two calls only game code reaches (the box's layout reset, the title's wall) |
| `src/game/main_loop.c` | mode 17, the tower's, run beside the game's seventeen rather than growing the table |
| `src/game/main_apply_menu_selection.c` | the button's choice (12, none of the game's) |
| `src/pc/platform/title_config.c`, `title_menu.c` | the button, the action, dimmed while locked |

Every step the screen takes is logged with `MEMORIES_TRACE=mods`
(`tower: status (floor 2, checkpoint 2)`, `tower: floor 1 won; next 1,
checkpoint 1`, `tower: fusion refused: 103 with 402 would make 22`...), which is
what the end-to-end test follows.

## Tests

- `pc_tower` (`tests/pc/tower_test.c`): the key read, its defaults and every
  note, the last mod's tower winning, a duel's result moving the climb (a
  checkpoint, a loss, `restart`, the top), the progress file with other
  towers' sections.
- `pc_tower_game` (`tests/pc/tower_game_test.c`): `unlock` (always,
  `after_ending` by byte `0x3DE`, conditions), the rules, LP and deck only in
  the tower's own duel, the duelists' `tower` and `tower_only`, the save's
  file across loads, NEW GAME, slots and the state chunk.
- `pc_fusion_rules`, `pc_duelists`, `pc_title_config`: the rules through
  `CardRules_*` and the `Duel_Check*` the CPU asks, a code mod's answer
  overruled; the duelists' conditions; the button and the action.
- `tools/pc/fm_editor/tests/test_tower.py`: the Tower tab.
- `tests/pc/tower/tower_e2e.py`, in the running game through the control
  channel ([agent control](agent-control.md)), with the tests' tower: the
  button, floor 1 won (its reward, the save at its checkpoint), floor 2 lost
  (the player's fusion refused, the CPU's deck of fusion material never
  fusing while a control run without the rule fuses Summoned Skull), back to
  the checkpoint; a new process LOADs and is at floor 2; the tower-only
  duelist out of Free Duel until floor 2 is won; floor 3 (an equip refused)
  clears the tower, its ending told and the climb saved, Nitemare on the grid; `after_ending`
  dimmed and buzzing until the save's byte says the ending was seen; and,
  without the mod, the title's menus the same frames as a build without tower
  mode.

  ```sh
  python tests/pc/tower/tower_e2e.py --exe tmp/pc/game32/memories-pc.exe \
      --baseline <a build of master>/memories-pc.exe --out tmp/pc/tower-e2e
  ```
