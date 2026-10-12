r"""A mod's "tower" (notes/tower-mode.md; src/pc/tower/tower_config.c): the
checks the game makes when it reads one, and the text forms the Tower tab
edits it in.

The tower stays in project.other["tower"] as written. The tab shows a floor's
story as lines of text, one dialogue box a line:

    Welcome to the tower.
    @Seto: You will not pass.        said by a duelist (an id, a name, an identity)
    #8: The prince steps in.         with one of the campaign's 25 portraits
    #8 @Seto: Both.                  a portrait, and who says it
    @none: The wind howls.           nobody's (no portrait)
    \@ plain words                   a backslash: words that would read as a speaker's
    One\nTwo                         \n: a line break inside the box

and a deck as a card a line, "Blue-eyes White Dragon x3" (x1 when left out).
"""
from __future__ import annotations

import copy as _copy
import re

TOWER_MAX_FLOORS = 99
LABEL_MAX = 31
NAME_MAX = 63
LINE_MAX = 1023
CAMPAIGN_PORTRAITS = 25
REWARD_CARDS = 10
REWARD_DUELISTS = 8
DECK = 40
TERRAINS = ("none", "Forest", "Wasteland", "Mountain", "Meadow", "Sea", "Dark")
TERRAIN_WORDS = {"none": 0, "0": 0, "forest": 1, "wasteland": 2, "mountain": 3, "sogen": 4, "meadow": 4,
                 "umi": 5, "sea": 5, "yami": 6, "dark": 6}
UNLOCKS = ("always", "after_ending", "conditions")
TOWER_KEYS = ("name", "label", "music", "unlock", "on_lose", "intro", "ending", "lose", "floors")
FLOOR_KEYS = ("duelist", "field", "deck", "life_points", "no_fusions", "no_equips", "reward", "checkpoint",
              "before", "after")
REWARD_KEYS = ("card", "cards", "starchips", "unlock_duelist")
UNLOCK_KEYS = ("after_ending", "beat", "wins", "story", "card", "copies")
LINE_KEYS = ("text", "speaker", "portrait")


def _number(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    return int(value)


def terrain_of(value) -> int | None:
    """A floor's "field" as the game reads it: 0-6, or None for none it knows."""
    if value is None:
        return 0
    text = str(value).strip().lower()
    if text.isdigit():
        number = int(text)
        return number if 0 <= number <= 6 else None
    return TERRAIN_WORDS.get(text)


# --- story lines ----------------------------------------------------------------------

# "#8 @Seto: words": a campaign portrait, a speaker, or both, then the words.
LINE_RE = re.compile(r"^(?:#(\d+)\s*)?(?:@([^:]*))?:\s?(.*)$")
NEWLINE = r"\n"        # a line break inside a box's words, as the tab shows it: a backslash and n


def _speaks(line: str) -> bool:
    found = LINE_RE.match(line)
    return bool(found) and (found.group(1) is not None or found.group(2) is not None)


def story_text(value) -> str:
    """A story as the tab shows it: a line of text per line. A plain line
    that would read as a speaker's starts with a backslash."""
    if value is None:
        return ""
    items = value if isinstance(value, list) else [value]
    out = []
    for item in items:
        if isinstance(item, str):
            text = item.replace("\n", NEWLINE)
            out.append("\\" + text if _speaks(text) or text.startswith("\\") else text)
        elif isinstance(item, dict):
            text = str(item.get("text", "")).replace("\n", NEWLINE)
            head = f"#{item['portrait']}" if "portrait" in item else ""
            if "speaker" in item:
                speaker = item["speaker"]
                head += (" " if head else "") + f"@{'none' if speaker in (None, '') else speaker}"
            if head:
                out.append(f"{head}: {text}")
            else:
                out.append("\\" + text if _speaks(text) or text.startswith("\\") else text)
    return "\n".join(out)


def story_value(text: str):
    """The tab's lines as the manifest writes them: None for none, a string
    for one plain line, else a list."""
    out = []
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        if line.startswith("\\"):
            out.append(line[1:].replace(NEWLINE, "\n"))
            continue
        found = LINE_RE.match(line)
        if found and (found.group(1) is not None or found.group(2) is not None):
            entry = {"text": found.group(3).strip().replace(NEWLINE, "\n")}
            if found.group(1) is not None:
                entry["portrait"] = int(found.group(1))
            if found.group(2) is not None:
                who = found.group(2).strip()
                entry["speaker"] = "none" if who.lower() in ("", "none") else int(who) if who.isdigit() else who
            out.append(entry)
        else:
            out.append(line.replace(NEWLINE, "\n"))
    if not out:
        return None
    if len(out) == 1 and isinstance(out[0], str):
        return out[0]
    return out


# --- decks ------------------------------------------------------------------------------

DECK_LINE_RE = re.compile(r"^(.*?)(?:\s*[x×]\s*(\d+))?$", re.I)


def deck_text(value) -> str:
    """A floor's deck as the tab shows it: "Card x3" a line."""
    if isinstance(value, dict):
        return "\n".join(f"{card} x{copies}" if copies != 1 else str(card) for card, copies in value.items())
    if isinstance(value, list):
        counted = {}
        for card in value:
            counted[card] = counted.get(card, 0) + 1
        return deck_text(counted)
    return ""


def deck_value(text: str):
    """The tab's lines as {card: copies} (cards in the order written), or
    None for no deck."""
    out = {}
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        found = DECK_LINE_RE.match(line)
        card, copies = found.group(1).strip(), int(found.group(2) or 1)
        key = int(card) if card.isdigit() else card
        out[key] = out.get(key, 0) + copies
    return out or None


def deck_count(value) -> int:
    if isinstance(value, dict):
        return sum(_number(copies) or 0 for copies in value.values())
    if isinstance(value, list):
        return len(value)
    return 0


def names_text(value) -> str:
    """One name or a list of them, as "a, b"."""
    if value is None:
        return ""
    items = value if isinstance(value, list) else [value]
    return ", ".join(str(item) for item in items)


def names_value(text: str):
    """"a, b" as a list (ids as numbers), one alone, or None."""
    items = [part.strip() for part in text.split(",") if part.strip()]
    items = [int(item) if item.isdigit() else item for item in items]
    if not items:
        return None
    return items[0] if len(items) == 1 else items


# --- the tower-only duelists --------------------------------------------------------------

def tower_only(project) -> list:
    """The mod's added duelists marked "tower_only", by key."""
    from . import roster
    return [e.key for e in roster.copies(project) if e.extra.get("tower_only") is True]


def set_tower_only(project, key: str, on: bool) -> bool:
    """Mark or unmark an added duelist; True when that changed it."""
    from . import roster
    e = roster.find(project, key)
    if e is None or e.replace or bool(e.extra.get("tower_only")) == on:
        return False
    if on:
        e.extra["tower_only"] = True
    else:
        e.extra.pop("tower_only", None)
    return True


# --- checks ---------------------------------------------------------------------------------

def check(tower, duelist_named, card_named) -> list:
    """What the game would note of a "tower" (tower_config.c): [(level,
    where, message)]. An error is something it leaves out (a floor, a deck,
    the whole tower); a warning what it reads otherwise than meant.
    duelist_named(text) is a duelist or None, card_named(text) a card or 0."""
    out = []
    if tower is None:
        return out

    def add(level, where, message):
        out.append((level, where, message))

    if not isinstance(tower, dict):
        add("error", "tower", "is an object ({\"floors\": [...]}); the game has no tower")
        return out
    for key in tower:
        if key not in TOWER_KEYS:
            add("warning", "tower", f"unknown key \"{key}\"")
    if not isinstance(tower.get("label", ""), str) or len(str(tower.get("label", ""))) > LABEL_MAX:
        add("warning", "tower.label", f"is text of at most {LABEL_MAX} letters")
    if len(str(tower.get("name", ""))) > NAME_MAX:
        add("warning", "tower.name", f"is at most {NAME_MAX} bytes; the rest is left out")
    music = tower.get("music")
    if music is not None:
        number = _number(music)
        if isinstance(music, str):
            try:
                number = int(music, 0)
            except ValueError:
                number = None
        if number is None or not 0 <= number <= 0xFFF:
            add("warning", "tower.music", "is a song id, 0x000 to 0xFFF; the Free Duel screen's is played")
    unlock = tower.get("unlock")
    if isinstance(unlock, str):
        if unlock not in ("always", "after_ending"):
            add("error", "tower.unlock", "is \"always\", \"after_ending\" or an object of conditions; "
                                         "the tower stays locked")
    elif isinstance(unlock, dict):
        for key in unlock:
            if key not in UNLOCK_KEYS:
                add("warning", "tower.unlock", f"unknown key \"{key}\"")
        if "after_ending" in unlock and not isinstance(unlock["after_ending"], bool):
            add("error", "tower.unlock.after_ending", "is true or false, without quotes; the game keeps the tower locked")
        for key, high in (("wins", 65535), ("copies", 250), ("story", 0x7FF)):
            if key in unlock and (_number(unlock[key]) is None or not 0 <= _number(unlock[key]) <= high):
                add("error", f"tower.unlock.{key}", f"is 0 to {high}; the tower stays locked")
        if "beat" in unlock and duelist_named(unlock["beat"]) is None:
            add("warning", "tower.unlock.beat", f"names no duelist ({unlock['beat']!r}); the tower stays locked")
        if "card" in unlock and not card_named(unlock["card"]):
            add("warning", "tower.unlock.card", f"names no card ({unlock['card']!r}); the tower stays locked")
    elif unlock is not None:
        add("error", "tower.unlock", "is \"always\", \"after_ending\" or an object of conditions; the tower stays locked")
    if tower.get("on_lose") not in (None, "checkpoint", "restart"):
        add("warning", "tower.on_lose", "is \"checkpoint\" or \"restart\"; checkpoint it is")
    for key in ("intro", "ending", "lose"):
        _check_story(tower.get(key), f"tower.{key}", duelist_named, add)
    floors = tower.get("floors")
    if not isinstance(floors, list) or not floors:
        add("error", "tower.floors", "is a list of at least one floor; the game has no tower")
        return out
    if len(floors) > TOWER_MAX_FLOORS:
        add("warning", "tower.floors", f"at most {TOWER_MAX_FLOORS} floors; the rest are left out")
    fought = 0
    for index, floor in enumerate(floors):
        if _check_floor(floor, f"tower.floors[{index}]", duelist_named, card_named, add):
            fought += 1
    if not fought:
        add("error", "tower.floors", "no floor can be fought; the game has no tower")
    return out


def _check_story(value, where, duelist_named, add):
    if value is None:
        return
    items = value if isinstance(value, list) else [value]
    for index, item in enumerate(items):
        at = f"{where}[{index}]" if isinstance(value, list) else where
        if isinstance(item, str):
            text = item
        elif isinstance(item, dict):
            for key in item:
                if key not in LINE_KEYS:
                    add("warning", at, f"unknown key \"{key}\"")
            text = item.get("text")
            speaker = item.get("speaker")
            if speaker not in (None, "", "none", "opponent") and duelist_named(speaker) is None:
                add("warning", at, f"\"speaker\" names no duelist ({speaker!r})")
            if "portrait" in item and (_number(item["portrait"]) is None or
                                       not 0 <= _number(item["portrait"]) < CAMPAIGN_PORTRAITS):
                add("warning", at, f"\"portrait\" is one of the campaign's portraits, 0 to {CAMPAIGN_PORTRAITS - 1}")
            if not isinstance(text, str):
                add("error", at, "a line is text, or {\"text\": ..., \"speaker\": ...}; left out")
                continue
        else:
            add("error", at, "a line is text, or {\"text\": ..., \"speaker\": ...}; left out")
            continue
        if len(text.encode("utf-8")) > LINE_MAX:
            add("warning", at, f"a line is at most {LINE_MAX} bytes; the rest is left out")


def _check_floor(floor, where, duelist_named, card_named, add) -> bool:
    """1 when the game keeps the floor."""
    if not isinstance(floor, dict):
        add("error", where, "a floor is an object ({\"duelist\": ...}); left out")
        return False
    for key in floor:
        if key not in FLOOR_KEYS:
            add("warning", where, f"unknown key \"{key}\"")
    if "duelist" not in floor:
        add("error", where, "a floor needs a \"duelist\"; left out")
        return False
    if duelist_named(floor["duelist"]) is None:
        add("error", where, f"no duelist {floor['duelist']!r}; the floor is left out")
        return False
    if "field" in floor and terrain_of(floor["field"]) is None:
        add("warning", f"{where}.field", "is none, Forest, Wasteland, Mountain, Meadow, Sea or Dark (1-6)")
    for key in ("no_fusions", "no_equips", "checkpoint"):
        if key in floor and not isinstance(floor[key], bool):
            add("warning", f"{where}.{key}", "is true or false, without quotes")
    lp = floor.get("life_points")
    if lp is not None:
        values = [lp] if not isinstance(lp, dict) else [lp[side] for side in ("player", "opponent") if side in lp]
        if isinstance(lp, dict) and any(side not in ("player", "opponent") for side in lp):
            add("warning", f"{where}.life_points", "is {\"player\": ..., \"opponent\": ...}")
        if any(_number(value) is None or not 1 <= _number(value) <= 65535 for value in values):
            add("warning", f"{where}.life_points", "is 1 to 65535")
    deck = floor.get("deck")
    if deck is not None:
        if not isinstance(deck, (dict, list)):
            add("error", f"{where}.deck", "is {card: copies} or a list of forty cards; the opponent's own is dealt")
        else:
            cards = deck if isinstance(deck, list) else list(deck)
            missing = [card for card in cards if not card_named(card)]
            if missing:
                add("error", f"{where}.deck", f"names no card {missing[0]!r}; the opponent's own is dealt")
            elif deck_count(deck) != DECK:
                add("error", f"{where}.deck", f"is {DECK} cards, and this one has {deck_count(deck)}; the "
                                              "opponent's own is dealt")
    reward = floor.get("reward")
    if reward is not None:
        if not isinstance(reward, dict):
            add("warning", f"{where}.reward", "is an object ({\"cards\": [...], \"starchips\": 100})")
        else:
            for key in reward:
                if key not in REWARD_KEYS:
                    add("warning", f"{where}.reward", f"unknown key \"{key}\"")
            cards = []
            for key in ("card", "cards"):
                value = reward.get(key)
                cards += value if isinstance(value, list) else [] if value is None else [value]
            for card in cards:
                if not card_named(card):
                    add("warning", f"{where}.reward", f"names no card {card!r}; left out")
            if len(cards) > REWARD_CARDS:
                add("warning", f"{where}.reward", f"at most {REWARD_CARDS} cards; the rest are left out")
            chips = reward.get("starchips")
            if chips is not None and (_number(chips) is None or not 0 <= _number(chips) <= 999999):
                add("warning", f"{where}.reward.starchips", "is 0 to 999999")
            duelists = reward.get("unlock_duelist")
            duelists = duelists if isinstance(duelists, list) else [] if duelists is None else [duelists]
            for name in duelists:
                if duelist_named(name) is None:
                    add("warning", f"{where}.reward.unlock_duelist", f"names no duelist {name!r}; left out")
    _check_story(floor.get("before"), f"{where}.before", duelist_named, add)
    _check_story(floor.get("after"), f"{where}.after", duelist_named, add)
    return True


def new_floor(duelist="1") -> dict:
    return {"duelist": int(duelist) if str(duelist).isdigit() else duelist}


def copy(value):
    return _copy.deepcopy(value)
