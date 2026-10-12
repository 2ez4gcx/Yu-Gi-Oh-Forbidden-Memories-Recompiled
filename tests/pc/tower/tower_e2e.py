"""Tower mode end to end, in the running game (notes/tower-mode.md).

Drives the game through tools/pc/yfm_control.py with the tests' tower mod
(tests/pc/tower/tower-test) and checks, by the game's own state, its log
(MEMORIES_TRACE=mods: the tower says each step it takes) and pictures:

  1. the title's second menu offers the tower's button; it is entered;
  2. floor 1 is won: its reward lands in the chest and the starchips, its
     checkpoint is saved through the game's own save (the port's slot menu);
  3. floor 2 (no fusions, the player at 100 LP) is lost: a fusion of the
     player's does not happen, the CPU's deck of fusion material never fuses,
     and the tower sends the player back to its checkpoint;
  4. a new process LOADs the save: the tower is at the checkpoint, floor 2;
  5. the tower-only duelist is not in Free Duel until floor 2 is won, and is
     after;
  6. floor 3 (no equips): the tower is cleared, the ending is told, the
     reward's duelist (Nitemare) joins the Free Duel grid;
  7. a control run without the rule: the same CPU deck fuses;
  8. "unlock": "after_ending": the button is dimmed and buzzes until the
     save has seen the ending.

Pictures of the game's frames go to --out. Exit status 0 when every check
holds.

    python tests/pc/tower/tower_e2e.py --exe tmp/pc/game32/memories-pc.exe \
        --baseline <a build of master>/memories-pc.exe --out <folder>
"""
from __future__ import annotations

import argparse
import json
import shutil
import struct
import sys
import time
import traceback
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/pc"))
from yfm_control import Game  # noqa: E402

MOD = ROOT / "tests/pc/tower/tower-test"
MENU, DUEL, TOWER, FREE_DUEL, ANIMATED_BATTLE = 8, 3, 17, 6, 1
BLUE_EYES, JOB_CHANGE_MIRROR, MONSTER_EYE, KURIBOH, SUMMONED_SKULL = 1, 103, 402, 58, 22
DRAGON_TREASURE, NITEMARE, GHOST = 315, 38, 40
DECK = f"{BLUE_EYES},{JOB_CHANGE_MIRROR},{MONSTER_EYE}"
SAVE_STARCHIPS = 0x801D07E0
SAVE_CHEST = 0x801D0250          # one byte a card, card 1 first
SAVE_CREDITS_FLAGS = 0x801D05DE  # save_data.h: 0x200 + 0x3DE

checks: list[tuple[str, bool]] = []


def check(what: str, ok: bool) -> bool:
    checks.append((what, bool(ok)))
    print(("ok   " if ok else "FAIL ") + what, flush=True)
    return ok


class Tower:
    """A game with the tower mod, and what its log says."""

    def __init__(self, exe: Path, out: Path, name: str, manifest: dict | None = None, fresh: bool = True):
        self.out = out / name
        if fresh:
            shutil.rmtree(self.out, ignore_errors=True)
        self.out.mkdir(parents=True, exist_ok=True)
        mods = self.out / "mods"
        shutil.rmtree(mods, ignore_errors=True)
        shutil.copytree(MOD, mods / "tower-test")
        if manifest is not None:
            (mods / "tower-test" / "mod.json").write_text(json.dumps(manifest, indent=1), encoding="utf-8")
        self.game = Game(executable=exe, out=self.out / "run", mods_dir=mods, env={"MEMORIES_TRACE": "mods"},
                         settings={"mod.3d-monsters": 0, "mod.hand-camera": 0, "mod.tower-test": 1})
        self.read = 0

    def log(self) -> str:
        return self.game.log_path.read_text(encoding="utf-8", errors="replace")

    def said(self, text: str) -> int:
        return self.log().count(text)

    def until_said(self, text: str, timeout=6000, count=None):
        """Until the log has `text` once more than now (or `count` times):
        Cross whenever the tower's box waits for it (text channel 0 at
        D_800EB0F8: its state byte at +0x51 is 4 at a page's end and 1 on a
        choice, 0x80 above once the state has begun; its flags at +0x34 have
        0x2000 once its text is done), so a press never runs ahead into the
        next box."""
        g = self.game
        wanted = count if count is not None else self.said(text) + 1
        start = g.vblank
        while self.said(text) < wanted:
            if g.vblank - start > timeout:
                raise TimeoutError(f"the log's '{text}' not reached in {timeout} VBlanks (mode {g.mode()})")
            state, flags = g.u8(0x800EB0F8 + 0x51), g.u16(0x800EB0F8 + 0x34)
            if g.mode() == TOWER and flags & 0x8000 and (state in (0x81, 0x84) or flags & 0x2000):
                g.press("cross", hold=4, after=20)
            else:
                g.step(10)

    def shot(self, out: Path, name: str):
        path = self.game.shot(name)
        shutil.copy(path, out / name)

    def quit(self):
        self.game.quit()


def to_second_menu(game: Game, deck: str | None = DECK):
    """The title's second menu, through the Free Duel screen: a duel's jump
    puts a deck in the save, Free Duel's Circle leaves for the menu."""
    game.wait_until(lambda g: g.resident("main_menu"), 3000, what="the title")
    if deck:
        game.goto("duel", opponent=1, deck=deck)
        game.step(30)
    game.goto("free_duel")
    game.step(90)
    game.press("cross", hold=4, after=30)
    game.press_until(lambda g: g.mode() == MENU, "circle", every=30, timeout=3000, what="the title")
    game.wait_until(lambda g: g.u8("D_8009B26C") & 0x40 and g.u8("gMain_bMenuID") >= 5, 3000,
                    what="the second menu")
    game.step(60)


def to_tower_button(game: Game):
    """The cursor down to the tower's button, the last of the second menu,
    one below SAVE: there the game's cursor byte rests on the menu's first
    entry (title_menu.c's carrier), 5."""
    game.step(60)
    for _ in range(12):
        if game.u8("gMain_bMenuID") == 10:   # on SAVE
            break
        game.press("down", hold=4, after=30)
    for _ in range(4):
        game.press("down", hold=4, after=30)
        if game.u8("gMain_bMenuID") == 5:
            return
    raise RuntimeError("the cursor did not leave SAVE for the tower's button")


SIDES, SIDE_SIZE = 0x800E9FF0, 0x20   # duel_side_state.h: the shown LP at +0x12, the LP at +0x14


def set_lp(game: Game, side: int, value: int):
    """A side's LP and its figure on the screen, as a test arranges them."""
    game.poke(SIDES + side * SIDE_SIZE + 0x12, struct.pack("<hH", value, value))


def chest(game: Game, card: int) -> int:
    return game.u8(SAVE_CHEST + card - 1)


def hand_ids(game: Game):
    return [card and card["id"] for card in game.duel()[0]["hand"]]


def over(game: Game) -> bool:
    return game.mode() != ANIMATED_BATTLE and game.duel_over()


def settle(game: Game, timeout=8000):
    start = game.vblank
    while game.mode() == ANIMATED_BATTLE or not (over(game) or game.turn() != 0 or game.phase() in (4, 5)):
        if game.vblank - start >= timeout:
            raise TimeoutError(f"the turn's end not reached (frame {game.frame}, mode {game.mode()})")
        game.step(2)


def win_quickly(game: Game, opening=(BLUE_EYES, BLUE_EYES, BLUE_EYES, JOB_CHANGE_MIRROR, MONSTER_EYE),
                ready=None):
    """The opponent at 100 LP (the floor's, or `ready`'s): Blue-eyes down,
    then attacks until the duel is over. `ready` runs once the hand is
    dealt, `False` when the duel is already under way."""
    if ready is not False:
        game.duel_ready(before_deal=lambda g: g.arrange_deck(0, list(opening)))
    if ready:
        ready(game)
    turns = 0
    while not over(game) and turns < 12:
        turns += 1
        if game.turn() != 0:
            game.wait_turn(("hand",))
        hand = hand_ids(game)
        slot = next((s for s, c in enumerate(hand) if c == BLUE_EYES), None)
        if slot is None:
            slot = next(s for s, c in enumerate(hand) if c)
        game.play_card(slot, face_up=True)
        if turns > 1:
            for column in range(5):
                if over(game) or game.turn() != 0:
                    break
                mine = game.field()[2][column]
                if not mine:
                    continue
                theirs = [c for c, card in enumerate(game.field()[1]) if card]
                game.attack(column, theirs[0] if theirs else None)
                settle(game)
        if not over(game) and game.turn() == 0:
            game.end_turn()
    return over(game)


def enter_tower(t: Tower):
    g = t.game
    to_tower_button(g)
    g.press("cross", hold=4, after=10)
    g.wait_until(lambda g: g.mode() == TOWER, 900, what="the tower")


def to_duel(t: Tower):
    """From the tower's status box: DUEL, the floor's lines, its duel."""
    t.game.press_until(lambda g: g.mode() == DUEL, "cross", every=40, timeout=3000, what="the floor's duel")


def back_to_tower(t: Tower):
    """The duel's result and rewards, Cross through them, back to the tower."""
    t.game.press_until(lambda g: g.mode() == TOWER, "cross", every=40, timeout=9000, what="the tower again")


def phase_climb(exe: Path, out: Path) -> Tower:
    """Floors 1 (won, saved) and 2 (lost)."""
    t = Tower(exe, out, "climb")
    g = t.game
    to_second_menu(g)
    to_tower_button(g)
    g.step(20)
    t.shot(out, "01-menu-with-tower.png")
    g.press("cross", hold=4, after=10)
    g.wait_until(lambda g: g.mode() == TOWER, 900, what="the tower")
    check("the tower's button opens the tower's screen (mode 17)", g.mode() == TOWER)
    g.step(150)
    t.shot(out, "02-intro.png")
    t.until_said("tower: status")
    g.step(150)
    t.shot(out, "03-status-floor-1.png")
    check("the status is of floor 1", "tower: status (floor 1, checkpoint 1)" in t.log())
    # Floor 1: Simon Muran at 100 LP.
    g.press("cross", hold=4, after=60)
    g.step(90)
    t.shot(out, "04-before-floor-1.png")
    to_duel(t)
    chips, kuriboh = g.u32(SAVE_STARCHIPS), chest(g, KURIBOH)
    won = win_quickly(g)
    check("floor 1's duel is won (the opponent started at the floor's 100 LP)", won and g.duel()[1]["lp"] == 0)
    back_to_tower(t)
    g.step(120)
    t.shot(out, "05-after-floor-1.png")
    t.until_said("tower: notice")
    g.step(120)
    t.shot(out, "06-floor-1-cleared.png")
    # Eight lines (the floor, five cards, the starchips, CHECKPOINT): a page
    # wait after the fourth, inside the box that stays up.
    paged = g.u8(0x800EB0F8 + 0x51) == 0x84 and g.u16(0x800EB0F8 + 0x34) & 0x8000
    g.press("cross", hold=4, after=300)   # the letters settled
    t.shot(out, "06b-floor-1-cleared-page-2.png")
    check("floor 1's notice of eight lines waits for a button after four, then shows the rest in the same box",
          bool(paged) and g.u16(0x800EB0F8 + 0x34) & 0x8000 and "tower: save?" not in t.log())
    check("floor 1 is won in the tower's log", "tower: floor 1 won; next 1, checkpoint 1" in t.log())
    check("floor 1's reward: Kuriboh in the chest", chest(g, KURIBOH) >= kuriboh + 1)
    check("floor 1's reward: 25 starchips", "floor 1's reward: 5 cards, 25 starchips" in t.log() and
          g.u32(SAVE_STARCHIPS) >= chips + 25)
    t.until_said("tower: save?")
    g.step(120)
    t.shot(out, "07-save-question.png")
    # YES: the game's own save, the port's slot menu (not in the frame); Cross on the first slot.
    status = t.said("tower: status")
    g.press("cross", hold=4, after=60)
    g.press_until(lambda g: t.said("tower: status") > status, "cross", every=60, timeout=3000,
                  what="the save and the status again")
    saves = list((t.out / "run/user/tower").glob("*.txt"))
    text = saves[0].read_text() if saves else ""
    check("the checkpoint is saved beside the save (tower/<token>.txt: floor 1, checkpoint 1)",
          "floor 1\ncheckpoint 1\n" in text)
    g.step(150)
    t.shot(out, "08-status-floor-2.png")
    # Floor 2: no fusions, the player at 100 LP, the opponent a deck of fusion material.
    g.press("cross", hold=4, after=40)
    to_duel(t)
    g.duel_ready(before_deal=lambda g: g.arrange_deck(0, [JOB_CHANGE_MIRROR, MONSTER_EYE, BLUE_EYES,
                                                            BLUE_EYES, BLUE_EYES]))
    check("floor 2: the player starts at the floor's 100 LP", g.duel()[0]["lp"] == 100)
    theirs = g.decks()[1]
    check(f"floor 2: the opponent's deck is the floor's, 20 Job-change Mirror and 20 Monster Eye ({theirs})",
          sorted(theirs) == [JOB_CHANGE_MIRROR] * 20 + [MONSTER_EYE] * 20)
    g.fuse([0, 1], face_up=True)
    g.step(60)
    t.shot(out, "09-floor-2-fusion-refused.png")
    mine = [c for c in g.field()[2] if c]
    check("floor 2: the player's Job-change Mirror + Monster Eye makes no Summoned Skull",
          SUMMONED_SKULL not in mine and "tower: fusion refused: 103 with 402 would make 22" in t.log())
    # The CPU at its ease for two turns (the player's LP put up meanwhile:
    # at 100 it goes straight for the kill), then the floor's 100 again.
    set_lp(g, 0, 8000)
    g.press_until(lambda g: g.turn() != 0 or over(g), "start", every=60, timeout=1200, what="the turn's end")
    seen = cpu_monsters(t, 2) if not over(g) else set()
    t.shot(out, "10-floor-2-cpu.png")
    refusals = t.log().count("tower: fusion refused:")
    check(f"floor 2: the CPU never fused (its monsters {sorted(seen)}; {refusals - 1} of its fusions refused in "
          "the log)", SUMMONED_SKULL not in seen and refusals > 1)
    set_lp(g, 0, 100)
    while not over(g):
        g.wait_turn(("hand",))
        if over(g):
            break
        g.play_card(next((s for s, c in enumerate(hand_ids(g)) if c), 0))
        if not over(g):
            g.press_until(lambda g: g.turn() != 0 or over(g), "start", every=60, timeout=1200, what="the turn's end")
            g.wait_until(lambda g: g.turn() == 0 or over(g), 20000, what="the CPU's turn")
    check("floor 2 is lost (the player's LP down to 0)", over(g) and g.duel()[0]["lp"] == 0)
    back_to_tower(t)
    t.until_said("tower: notice")
    g.step(120)
    t.shot(out, "11-floor-2-lost.png")
    check("floor 2 lost: back to the checkpoint, floor 2", "tower: floor 2 lost; next 1, checkpoint 1" in t.log())
    t.until_said("tower: status")
    g.step(150)
    t.shot(out, "12-status-after-loss.png")
    return t


def cpu_monsters(t: Tower, turns: int = 2) -> set:
    """The player's turns ended (a card put down each), the CPU's monsters
    seen while it plays its `turns`."""
    g = t.game
    seen = set()
    for _ in range(turns):
        if over(g):
            break
        g.wait_turn(("hand",))
        hand = hand_ids(g)
        slot = next((s for s, c in enumerate(hand) if c), 0)
        g.play_card(slot)            # face down, in defence: it lets the CPU play on
        if over(g):
            break
        g.press_until(lambda g: g.turn() != 0 or over(g), "start", every=60, timeout=1200, what="the turn's end")
        start = g.vblank
        while not over(g) and g.vblank - start < 20000:
            for card in g.field()[1]:
                if card:
                    seen.add(card)
            if g.turn() == 0:
                break
            g.step(10)
    return seen


def prompt_shown(game: Game) -> bool:
    """PUSH START BUTTON on the title: its object (D_80184560) renderable
    (display_object_layout.h, 0x40 in the flags at +8)."""
    pointer = game.u32(0x80184560)
    return 0x80000000 <= pointer < 0x80200000 and bool(game.u16(pointer + 8) & 0x40)


def load_from_title(t: Tower):
    """A new game's title: PUSH START, LOAD, the slot menu's first slot (the
    port's, Cross), and the second menu the game opens after a load."""
    g = t.game
    g.wait_until(lambda g: g.resident("main_menu") and prompt_shown(g), 3000, what="PUSH START BUTTON")
    g.press_until(lambda g: not prompt_shown(g), "start", every=60, timeout=1200, what="the first menu")
    g.step(90)
    g.press("down", hold=4, after=30)          # LOAD
    g.press("cross", hold=4, after=60)          # the slot menu
    g.press_until(lambda g: g.u8("gMain_bMenuID") >= 5, "cross", every=60, timeout=3000, what="the loaded game")
    g.step(120)


def free_duel_cell(t: Tower, page: int) -> int:
    """Free Duel, the grid's page `page` (R1 turns it), Cross on its first
    cell: the opponent a duel was armed against (gDuel_bOpponentID), or 0
    when the cell took no Cross (no duelist shown there)."""
    g = t.game
    g.goto("free_duel")
    g.step(90)
    g.press("cross", hold=4, after=30)          # SELECT OPPONENT!
    for _ in range(page):
        g.press("r1", hold=4, after=40)
    g.step(30)
    g.shot(f"free-duel-page-{page}.png")
    g.press("cross", hold=4, after=60)
    armed = g.mode() == DUEL
    opponent = g.u8("gDuel_bOpponentID") if armed else 0
    return opponent


def manifest_variant(change) -> dict:
    manifest = json.loads((MOD / "mod.json").read_text(encoding="utf-8"))
    change(manifest["tower"])
    return manifest


def phase_control(exe: Path, out: Path):
    """The same CPU deck on a floor without the rule (floor 2 first, the
    player at 8000): the CPU fuses, so the rule is what kept it from it."""
    def first_unruled(tower):
        floor = dict(tower["floors"][1], no_fusions=False, life_points={"player": 8000})
        floor.pop("before", None)
        tower["floors"] = [floor] + tower["floors"]
        tower.pop("intro", None)
    t = Tower(exe, out, "control", manifest_variant(first_unruled))
    g = t.game
    to_second_menu(g)
    enter_tower(t)
    t.until_said("tower: status")
    g.press("cross", hold=4, after=40)
    to_duel(t)
    g.duel_ready(before_deal=lambda g: g.arrange_deck(0, [BLUE_EYES] * 3 + [JOB_CHANGE_MIRROR, MONSTER_EYE]))
    seen = cpu_monsters(t, 3)
    g.shot("control-cpu.png")
    shutil.copy(t.out / "run/control-cpu.png", out / "13-control-cpu-fuses.png")
    check(f"control (no rule): the same CPU deck fuses Summoned Skull (its monsters {sorted(seen)})",
          SUMMONED_SKULL in seen)
    t.quit()


def leave_tower(t: Tower):
    """LEAVE on the status box: the title's second menu."""
    g = t.game
    g.press("down", hold=4, after=30)
    g.press("cross", hold=4, after=30)
    g.wait_until(lambda g: g.mode() == MENU and g.u8("gMain_bMenuID") >= 5, 3000, what="the second menu")
    g.step(60)


def phase_load(exe: Path, out: Path):
    """A new process LOADs the save: the tower is at its checkpoint; the
    tower-only duelist stays out of Free Duel until floor 2 is won; floor 3
    (no equips) clears the tower, its ending told, Nitemare freed."""
    t = Tower(exe, out, "climb", fresh=False)    # the climb's user folder: its save and tower file
    g = t.game
    load_from_title(t)
    t.shot(out, "14-loaded-second-menu.png")
    enter_tower(t)
    t.until_said("tower: status")
    g.step(150)
    t.shot(out, "15-loaded-status-floor-2.png")
    check("LOAD in a new process: the tower is at the checkpoint, floor 2",
          "tower: progress read from" in t.log() and "tower: status (floor 2, checkpoint 2)" in t.log())
    leave_tower(t)
    hidden = free_duel_cell(t, 1)
    shutil.copy(t.out / "run/free-duel-page-1.png", out / "16-free-duel-page-2-before.png")
    check(f"before floor 2 is won, the tower-only Ghost Simon is not on Free Duel's page 2 (cell 0 took {hidden})",
          hidden == 0)
    to_second_menu(g, deck=None)
    enter_tower(t)
    t.until_said("tower: status")
    # Floor 2 won this time: the player's LP put up, the opponent's down.
    g.press("cross", hold=4, after=40)
    to_duel(t)
    won = win_quickly(g, ready=lambda g: (set_lp(g, 0, 8000), set_lp(g, 1, 100)))
    back_to_tower(t)
    t.until_said("tower: status")
    check("floor 2 won after the LOAD", won and "tower: floor 2 won; next 2, checkpoint 1" in t.log())
    g.step(150)
    t.shot(out, "17-status-floor-3.png")
    leave_tower(t)
    freed = free_duel_cell(t, 1)
    shutil.copy(t.out / "run/free-duel-page-1.png", out / "18-free-duel-page-2-after.png")
    check(f"after floor 2, Ghost Simon is on Free Duel's page 2 (a duel against {freed})", freed == GHOST)
    to_second_menu(g, deck=None)
    # Floor 3: no equips. The deck: Blue-eyes and Dragon Treasure, which equips it.
    g.poke("gDuel_awPlayerDeck", struct.pack("<40H", *([BLUE_EYES, DRAGON_TREASURE] * 20)))
    enter_tower(t)
    t.until_said("tower: status")
    g.press("cross", hold=4, after=40)
    to_duel(t)
    g.duel_ready(before_deal=lambda g: g.arrange_deck(0, [BLUE_EYES, DRAGON_TREASURE, BLUE_EYES, BLUE_EYES,
                                                            DRAGON_TREASURE]))
    g.fuse([0, 1], face_up=True)
    g.step(60)
    t.shot(out, "19-floor-3-equip-refused.png")
    mine = [c for c in g.duel()[0]["monsters"] if c]
    check(f"floor 3: Dragon Treasure does not equip Blue-eyes (its attack {[c['attack'] for c in mine]})",
          bool(mine) and all(c["attack"] == 3000 for c in mine) and
          f"tower: equip refused: {DRAGON_TREASURE} with {BLUE_EYES} would make {BLUE_EYES}" in t.log())
    g.end_turn()
    won = win_quickly(g, ready=False)
    back_to_tower(t)
    t.until_said("tower: notice")
    g.step(150)
    t.shot(out, "20-tower-cleared.png")
    # The top offers a save too, after the ending: YES, the slot the climb was loaded from.
    t.until_said("tower: save?")
    status = t.said("tower: status")
    g.press("cross", hold=4, after=60)
    g.press_until(lambda g: t.said("tower: status") > status, "cross", every=60, timeout=3000,
                  what="the save after the top and the status again")
    saves = list((t.out / "run/user/tower").glob("*.txt"))
    text = saves[0].read_text() if saves else ""
    check("the top's save: the climb back at floor 1, cleared once (tower/<token>.txt)",
          "floor 0\ncheckpoint 0\ncompletions 1\n" in text)
    g.step(150)
    t.shot(out, "21-status-cleared-once.png")
    log = t.log()
    after = log.split("floor 3 won")[-1]
    check("floor 3 won: the tower is cleared, its ending told, back to floor 1",
          won and "tower: floor 3 won; next 0, checkpoint 0" in log and "tower: story" in after and
          "tower: status (floor 1, checkpoint 1)" in after)
    flags = g.u8(0x801D0618 + ((0x6E0 + NITEMARE) >> 3))
    check("floor 3's reward: Nitemare's Free Duel flag (0x6E0 + 38) is set",
          bool(flags & (0x80 >> ((0x6E0 + NITEMARE) & 7))))
    t.quit()


def phase_after_ending(exe: Path, out: Path):
    """The "unlock": "after_ending" variant: the button dimmed and a buzz
    until the save has seen the ending (save_data.h: Main_RunCredits sets
    byte 0x3DE's low bits)."""
    t = Tower(exe, out, "after-ending", manifest_variant(lambda tower: tower.update(unlock="after_ending")))
    g = t.game
    to_second_menu(g)
    g.poke(SAVE_CREDITS_FLAGS, bytes([g.u8(SAVE_CREDITS_FLAGS) & 0xFC]))
    to_tower_button(g)
    g.step(30)
    t.shot(out, "22-after-ending-locked.png")
    g.press("cross", hold=4, after=60)
    check("after_ending, the ending not seen: the tower's button only buzzes", g.mode() == MENU)
    g.poke(SAVE_CREDITS_FLAGS, bytes([g.u8(SAVE_CREDITS_FLAGS) | 3]))
    g.step(30)
    t.shot(out, "23-after-ending-open.png")
    g.press("cross", hold=4, after=10)
    g.wait_until(lambda g: g.mode() == TOWER, 900, what="the tower")
    check("after_ending, the ending seen (byte 0x3DE & 3): the tower opens", g.mode() == TOWER)
    t.quit()


def phase_no_mod(exe: Path, out: Path, baseline: Path | None):
    """Without the tower mod the title's menus are the same frames as a
    build without tower mode."""
    if not baseline or not baseline.is_file():
        check("no-mod comparison: a baseline build given (--baseline)", False)
        return
    hashes = []
    for name, executable in (("no-mod-tower-build", exe), ("no-mod-baseline", baseline)):
        out_dir = out / name
        shutil.rmtree(out_dir, ignore_errors=True)
        game = Game(executable=executable, out=out_dir, settings={"mod.3d-monsters": 0, "mod.hand-camera": 0})
        seen = []
        game.wait_until(lambda g: g.resident("main_menu") and prompt_shown(g), 3000, what="PUSH START BUTTON")
        game.step(120)
        game.press_until(lambda g: not prompt_shown(g), "start", every=60, timeout=1200, what="the first menu")
        game.step(90)
        seen.append(game.hash())
        to_second_menu(game)
        seen.append(game.hash())
        for _ in range(7):
            game.press("down", hold=4, after=30)
            seen.append(game.hash())
        game.shot("second-menu.png")
        game.quit()
        hashes.append(seen)
    shutil.copy(out / "no-mod-tower-build/second-menu.png", out / "24-no-mod-second-menu.png")
    check(f"without the mod, the first and second menus (cursor walked) hash the same as the baseline's "
          f"({len(hashes[0])} frames)", hashes[0] == hashes[1])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, default=ROOT / "tmp/pc/game32" / ("memories-pc.exe" if sys.platform == "win32" else "memories-pc"))
    parser.add_argument("--baseline", type=Path, help="a build without tower mode, for the no-mod frames")
    parser.add_argument("--out", type=Path, default=ROOT / "tmp/pc/tower-e2e")
    parser.add_argument("--only", help="run one phase")
    options = parser.parse_args()
    options.out.mkdir(parents=True, exist_ok=True)
    phases = {"climb": lambda: phase_climb(options.exe, options.out).quit(),
              "load": lambda: phase_load(options.exe, options.out),
              "control": lambda: phase_control(options.exe, options.out),
              "after_ending": lambda: phase_after_ending(options.exe, options.out),
              "no_mod": lambda: phase_no_mod(options.exe, options.out, options.baseline)}
    for name, phase in phases.items():
        if options.only and name != options.only:
            continue
        try:
            phase()
        except Exception as error:   # a phase that cannot go on fails, the others still run
            traceback.print_exc()
            check(f"phase {name} ran to its end ({error!r})", False)
    failed = [what for what, ok in checks if not ok]
    print(f"{len(checks) - len(failed)}/{len(checks)} checks hold")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
