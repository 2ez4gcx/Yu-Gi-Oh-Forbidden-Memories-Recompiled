/* A mod's "tower" as written, and the progress a save has in it (tower.h,
 * notes/tower-mode.md).
 *
 * Nothing here touches the game: the manifest is read through the JSON
 * reader, names through the duelist and card lists, and the progress is
 * numbers in a struct and lines in a file, so the tests run all of it
 * without a disc. */
#include "tower.h"
#include "pc/mods/json.h"
#include "pc/mods/mods.h"
#include "pc/cards/cards.h"
#include "pc/cards/tables.h"
#include "pc/free_duel/duelists.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static TowerConfig config;
static int defined;

/* Every line's text, freed with the tower. */
static void free_lines(void)
{
    int i;
    for (i = 0; i < config.line_count; i++) free(config.lines[i].text);
    free(config.lines);
    config.lines = NULL;
    config.line_count = config.line_room = 0;
}

void Tower_Reset(void)
{
    free_lines();
    memset(&config, 0, sizeof(config));
    defined = 0;
}

const TowerConfig *Tower_Config(void) { return defined ? &config : NULL; }
const char *Tower_Mod(void) { return defined ? config.mod : NULL; }
const char *Tower_Label(void) { return defined && config.label[0] ? config.label : "TOWER"; }

/* --- reading ---------------------------------------------------------------- */

/* Keys a part of the tower knows; another is noted, as a misspelling would
 * otherwise be silently left out. */
static void check_keys(const char *mod, const char *where, const JsonValue *object, const char *const *known,
                       size_t count)
{
    const JsonValue *member;
    for (member = Json_At(object, 0); member; member = Json_Next(member)) {
        size_t k;
        for (k = 0; k < count && strcmp(Json_Name(member), known[k]); k++) {}
        if (k == count) Mods_Note(mod, "%s: unknown key \"%s\"", where, Json_Name(member));
    }
}

/* A reference (a duelist, a card) as written: a name, an identity, or an
 * id written as a number. "" when it is neither. */
static void reference(const JsonValue *value, char *out, size_t size)
{
    if (Json_TypeOf(value) == JSON_NUMBER) snprintf(out, size, "%ld", Json_Number(value, 0));
    else snprintf(out, size, "%s", Json_String(value, ""));
}

static int whole(const JsonValue *value, long low, long high, long *out)
{
    long number;
    if (Json_TypeOf(value) != JSON_NUMBER) return 0;
    number = Json_Number(value, low - 1);
    if (number < low || number > high) return 0;
    *out = number;
    return 1;
}

static int same_word(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    }
    return !*a && !*b;
}

/* A duelist named in the tower: its id, or 0 noted. */
static int duelist_named(const char *mod, const char *where, const JsonValue *value)
{
    char text[TOWER_REF];
    int id;
    reference(value, text, sizeof(text));
    id = text[0] ? Duelists_Named(text) : -1;
    if (id <= 0 || !Duelists_Valid(id)) {
        Mods_Note(mod, "%s: no duelist \"%s\"", where, text);
        return 0;
    }
    return id;
}

static int card_named(const char *mod, const char *where, const JsonValue *value)
{
    char text[TOWER_REF];
    int id;
    reference(value, text, sizeof(text));
    id = text[0] ? Cards_Named(text) : -1;
    if (id <= 0 || !Cards_Valid(id)) {
        Mods_Note(mod, "%s: no card \"%s\"", where, text);
        return 0;
    }
    return id;
}

static int add_line(int speaker, int portrait, const char *text)
{
    TowerLine *line;
    if (config.line_count == config.line_room) {
        int room = config.line_room ? config.line_room * 2 : 32;
        TowerLine *grown = realloc(config.lines, (size_t)room * sizeof(*grown));
        if (!grown) return 0;
        config.lines = grown;
        config.line_room = room;
    }
    line = &config.lines[config.line_count];
    line->speaker = speaker;
    line->portrait = portrait;
    line->text = malloc(strlen(text) + 1);
    if (!line->text) return 0;
    strcpy(line->text, text);
    config.line_count++;
    return 1;
}

/* One line: its text, alone or with who says it. */
static void read_line(const char *mod, const char *where, const JsonValue *value, int speaker)
{
    static const char *const known[] = {"text", "speaker", "portrait"};
    const char *text = Json_String(value, NULL);
    int portrait = -1;
    if (!text && Json_TypeOf(value) == JSON_OBJECT) {
        const JsonValue *who = Json_Member(value, "speaker"), *face = Json_Member(value, "portrait");
        check_keys(mod, where, value, known, sizeof(known) / sizeof(known[0]));
        text = Json_String(Json_Member(value, "text"), NULL);
        if (who) {
            const char *word = Json_String(who, NULL);
            if (Json_TypeOf(who) == JSON_NULL || (word && (same_word(word, "none") || !word[0]))) speaker = TOWER_SPEAKER_NONE;
            else if (word && same_word(word, "opponent")) speaker = TOWER_SPEAKER_FLOOR;
            else speaker = duelist_named(mod, where, who);
        }
        if (face) {
            long number;
            if (whole(face, 0, TOWER_CAMPAIGN_PORTRAITS - 1, &number)) portrait = (int)number;
            else Mods_Note(mod, "%s: \"portrait\" is one of the campaign's portraits, 0 to %d", where,
                           TOWER_CAMPAIGN_PORTRAITS - 1);
        }
    }
    if (!text) {
        Mods_Note(mod, "%s: a line is text, or {\"text\": ..., \"speaker\": ...}", where);
        return;
    }
    if (strlen(text) >= TOWER_LINE_TEXT) {
        char cut[TOWER_LINE_TEXT];
        Mods_Note(mod, "%s: a line is at most %d bytes; the rest is left out", where, TOWER_LINE_TEXT - 1);
        snprintf(cut, sizeof(cut), "%s", text);
        add_line(speaker, portrait, cut);
        return;
    }
    if (!text[0]) return;
    add_line(speaker, portrait, text);
}

/* A story: a line, or a list of them. `speaker` says them unless a line
 * names another. */
static TowerStory read_story(const char *mod, const char *where, const JsonValue *value, int speaker)
{
    TowerStory story;
    char at[160];
    story.first = config.line_count;
    story.count = 0;
    if (!value) return story;
    if (Json_TypeOf(value) == JSON_ARRAY) {
        const JsonValue *item;
        int index = 0;
        for (item = Json_At(value, 0); item; item = Json_Next(item), index++) {
            snprintf(at, sizeof(at), "%s[%d]", where, index);
            read_line(mod, at, item, speaker);
        }
    } else {
        read_line(mod, where, value, speaker);
    }
    story.count = config.line_count - story.first;
    return story;
}

/* "deck": {card: copies} as a fixed deck is written ("decks", tables.h), or
 * a list of the forty. Exactly forty, else the floor deals the opponent's
 * own. */
static void read_deck(const char *mod, const char *where, const JsonValue *value, TowerFloor *floor)
{
    unsigned short cards[TOWER_DECK];
    int count = 0, broken = 0;
    char at[200];
    if (Json_TypeOf(value) == JSON_ARRAY) {
        const JsonValue *item;
        for (item = Json_At(value, 0); item; item = Json_Next(item)) {
            int id = card_named(mod, where, item);
            if (!id) { broken = 1; continue; }
            if (count < TOWER_DECK) cards[count] = (unsigned short)id;
            count++;
        }
    } else if (Json_TypeOf(value) == JSON_OBJECT) {
        const JsonValue *member;
        for (member = Json_At(value, 0); member; member = Json_Next(member)) {
            long copies;
            int id = Cards_Named(Json_Name(member)), i;
            snprintf(at, sizeof(at), "%s \"%s\"", where, Json_Name(member));
            if (id <= 0 || !Cards_Valid(id)) {
                Mods_Note(mod, "%s: no such card", at);
                broken = 1;
                continue;
            }
            if (!whole(member, 0, TOWER_DECK, &copies)) {
                Mods_Note(mod, "%s: a deck gives each card its copies, 0 to %d", at, TOWER_DECK);
                broken = 1;
                continue;
            }
            for (i = 0; i < copies; i++, count++) {
                if (count < TOWER_DECK) cards[count] = (unsigned short)id;
            }
        }
    } else {
        Mods_Note(mod, "%s: a deck is {card: copies} or a list of forty cards; the opponent's own is dealt", where);
        return;
    }
    if (broken) {
        Mods_Note(mod, "%s: the opponent's own deck is dealt instead", where);
        return;
    }
    if (count != TOWER_DECK) {
        Mods_Note(mod, "%s: a deck is %d cards, and this one has %d; the opponent's own is dealt", where, TOWER_DECK,
                  count);
        return;
    }
    memcpy(floor->deck, cards, sizeof(cards));
    floor->deck_given = 1;
}

/* "life_points": the same for both, or {"player": ..., "opponent": ...}. */
static void read_life_points(const char *mod, const char *where, const JsonValue *value, TowerFloor *floor)
{
    static const char *const sides[] = {"player", "opponent"};
    long number;
    int side;
    if (Json_TypeOf(value) == JSON_NUMBER) {
        if (!whole(value, 1, 65535, &number)) {
            Mods_Note(mod, "%s: \"life_points\" is 1 to 65535", where);
            return;
        }
        floor->life_points[0] = floor->life_points[1] = (int)number;
        return;
    }
    if (Json_TypeOf(value) != JSON_OBJECT) {
        Mods_Note(mod, "%s: \"life_points\" is a number, or {\"player\": ..., \"opponent\": ...}", where);
        return;
    }
    check_keys(mod, where, value, sides, 2);
    for (side = 0; side < 2; side++) {
        const JsonValue *part = Json_Member(value, sides[side]);
        if (!part) continue;
        if (whole(part, 1, 65535, &number)) floor->life_points[side] = (int)number;
        else Mods_Note(mod, "%s: \"life_points\" \"%s\" is 1 to 65535", where, sides[side]);
    }
}

/* One card, or a list of them. */
static void read_reward_cards(const char *mod, const char *where, const JsonValue *value, TowerFloor *floor)
{
    const JsonValue *item = Json_TypeOf(value) == JSON_ARRAY ? Json_At(value, 0) : value;
    for (; item; item = Json_TypeOf(value) == JSON_ARRAY ? Json_Next(item) : NULL) {
        int id = card_named(mod, where, item);
        if (!id) continue;
        if (floor->reward_card_count >= TOWER_REWARD_CARDS) {
            Mods_Note(mod, "%s: at most %d cards; the rest are left out", where, TOWER_REWARD_CARDS);
            return;
        }
        floor->reward_cards[floor->reward_card_count++] = id;
    }
}

static void read_reward(const char *mod, const char *where, const JsonValue *value, TowerFloor *floor)
{
    static const char *const known[] = {"card", "cards", "starchips", "unlock_duelist"};
    const JsonValue *part;
    char at[160];
    if (Json_TypeOf(value) != JSON_OBJECT) {
        Mods_Note(mod, "%s: \"reward\" is an object ({\"cards\": [...], \"starchips\": 100, \"unlock_duelist\": ...})",
                  where);
        return;
    }
    check_keys(mod, where, value, known, sizeof(known) / sizeof(known[0]));
    snprintf(at, sizeof(at), "%s reward", where);
    if ((part = Json_Member(value, "card"))) read_reward_cards(mod, at, part, floor);
    if ((part = Json_Member(value, "cards"))) read_reward_cards(mod, at, part, floor);
    if ((part = Json_Member(value, "starchips"))) {
        long number;
        if (whole(part, 0, 999999, &number)) floor->reward_starchips = (unsigned)number;
        else Mods_Note(mod, "%s: \"starchips\" is 0 to 999999", at);
    }
    if ((part = Json_Member(value, "unlock_duelist"))) {
        const JsonValue *item = Json_TypeOf(part) == JSON_ARRAY ? Json_At(part, 0) : part;
        for (; item; item = Json_TypeOf(part) == JSON_ARRAY ? Json_Next(item) : NULL) {
            int id = duelist_named(mod, at, item);
            if (!id) continue;
            if (floor->reward_duelist_count >= TOWER_REWARD_DUELISTS) {
                Mods_Note(mod, "%s: at most %d duelists to unlock", at, TOWER_REWARD_DUELISTS);
                break;
            }
            floor->reward_duelists[floor->reward_duelist_count++] = id;
        }
    }
}

static int read_flag(const char *mod, const char *where, const JsonValue *object, const char *key)
{
    const JsonValue *value = Json_Member(object, key);
    if (!value) return 0;
    if (Json_TypeOf(value) != JSON_BOOL) {
        Mods_Note(mod, "%s: \"%s\" is true or false, without quotes", where, key);
        return 0;
    }
    return Json_Bool(value, 0);
}

/* One floor; 0 when it cannot be fought (no opponent), which leaves it out. */
static int read_floor(const char *mod, int index, const JsonValue *value, TowerFloor *floor)
{
    static const char *const known[] = {"duelist", "field", "deck", "life_points", "no_fusions", "no_equips",
                                        "reward", "checkpoint", "before", "after"};
    char where[64], at[96];
    const JsonValue *part;
    memset(floor, 0, sizeof(*floor));
    snprintf(where, sizeof(where), "tower floors[%d]", index);
    if (Json_TypeOf(value) != JSON_OBJECT) {
        Mods_Note(mod, "%s: a floor is an object ({\"duelist\": \"Seto\", ...}); left out", where);
        return 0;
    }
    check_keys(mod, where, value, known, sizeof(known) / sizeof(known[0]));
    if (!(part = Json_Member(value, "duelist"))) {
        Mods_Note(mod, "%s: a floor needs a \"duelist\"; left out", where);
        return 0;
    }
    floor->duelist = duelist_named(mod, where, part);
    if (!floor->duelist) {
        Mods_Note(mod, "%s: left out", where);
        return 0;
    }
    if ((part = Json_Member(value, "field"))) {
        char text[TOWER_REF];
        reference(part, text, sizeof(text));
        if (same_word(text, "none") || !strcmp(text, "0")) floor->terrain = 0;
        else if ((floor->terrain = Tables_TerrainNamed(text)) < 0) {
            Mods_Note(mod, "%s: \"field\" is none, Forest, Wasteland, Mountain, Meadow, Sea or Dark (1-6)", where);
            floor->terrain = 0;
        }
    }
    if ((part = Json_Member(value, "deck"))) {
        snprintf(at, sizeof(at), "%s deck", where);
        read_deck(mod, at, part, floor);
    }
    if ((part = Json_Member(value, "life_points"))) read_life_points(mod, where, part, floor);
    if (read_flag(mod, where, value, "no_fusions")) floor->rules |= TOWER_RULE_NO_FUSIONS;
    if (read_flag(mod, where, value, "no_equips")) floor->rules |= TOWER_RULE_NO_EQUIPS;
    floor->checkpoint = read_flag(mod, where, value, "checkpoint");
    if ((part = Json_Member(value, "reward"))) read_reward(mod, where, part, floor);
    snprintf(at, sizeof(at), "%s before", where);
    floor->before = read_story(mod, at, Json_Member(value, "before"), TOWER_SPEAKER_FLOOR);
    snprintf(at, sizeof(at), "%s after", where);
    floor->after = read_story(mod, at, Json_Member(value, "after"), TOWER_SPEAKER_FLOOR);
    return 1;
}

static void read_unlock(const char *mod, const JsonValue *value)
{
    static const char *const known[] = {"after_ending", "beat", "wins", "story", "card", "copies"};
    TowerUnlock *unlock = &config.unlock;
    const char *word = Json_String(value, NULL);
    long number;
    memset(unlock, 0, sizeof(*unlock));
    unlock->story = -1;
    if (word) {
        if (same_word(word, "always")) return;
        if (same_word(word, "after_ending")) {
            unlock->kind = TOWER_UNLOCK_CONDITIONS;
            unlock->after_ending = 1;
            return;
        }
        Mods_Note(mod, "tower: \"unlock\" is \"always\", \"after_ending\" or an object of conditions; "
                       "the tower stays locked");
        unlock->kind = TOWER_UNLOCK_NEVER;
        return;
    }
    if (Json_TypeOf(value) != JSON_OBJECT) {
        Mods_Note(mod, "tower: \"unlock\" is \"always\", \"after_ending\" or an object of conditions; "
                       "the tower stays locked");
        unlock->kind = TOWER_UNLOCK_NEVER;
        return;
    }
    check_keys(mod, "tower unlock", value, known, sizeof(known) / sizeof(known[0]));
    unlock->kind = TOWER_UNLOCK_CONDITIONS;
    unlock->after_ending = read_flag(mod, "tower unlock", value, "after_ending");
    if (Json_Member(value, "after_ending") && Json_TypeOf(Json_Member(value, "after_ending")) != JSON_BOOL) {
        Mods_Note(mod, "tower unlock: the tower stays locked");   /* "yes" is no condition to open it on */
        unlock->kind = TOWER_UNLOCK_NEVER;
    }
    if (Json_Member(value, "beat")) reference(Json_Member(value, "beat"), unlock->beat, sizeof(unlock->beat));
    if (Json_Member(value, "card")) reference(Json_Member(value, "card"), unlock->card, sizeof(unlock->card));
    if (Json_Member(value, "wins")) {
        if (whole(Json_Member(value, "wins"), 0, 65535, &number)) unlock->wins = (int)number;
        else { Mods_Note(mod, "tower unlock: \"wins\" is 0 to 65535; the tower stays locked"); unlock->kind = TOWER_UNLOCK_NEVER; }
    }
    if (Json_Member(value, "copies")) {
        if (whole(Json_Member(value, "copies"), 0, 250, &number)) unlock->copies = (int)number;
        else { Mods_Note(mod, "tower unlock: \"copies\" is 0 to 250; the tower stays locked"); unlock->kind = TOWER_UNLOCK_NEVER; }
    }
    if (Json_Member(value, "story")) {
        if (whole(Json_Member(value, "story"), 0, 0x7FF, &number)) unlock->story = (int)number;
        else { Mods_Note(mod, "tower unlock: \"story\" is a flag, 0 to 2047; the tower stays locked"); unlock->kind = TOWER_UNLOCK_NEVER; }
    }
    if (unlock->beat[0] && Duelists_Named(unlock->beat) < 0)
        Mods_Note(mod, "tower unlock: no duelist \"%s\"; the tower stays locked until there is one", unlock->beat);
    if (unlock->card[0] && Cards_Named(unlock->card) <= 0)
        Mods_Note(mod, "tower unlock: no card \"%s\"; the tower stays locked until there is one", unlock->card);
    if (unlock->kind == TOWER_UNLOCK_CONDITIONS && !unlock->after_ending && !unlock->beat[0] && !unlock->card[0] &&
        !unlock->wins && unlock->story < 0)
        unlock->kind = TOWER_UNLOCK_ALWAYS;   /* {} asks for nothing */
}

void Tower_Read(const char *mod, const JsonValue *manifest)
{
    static const char *const known[] = {"name", "label", "music", "unlock", "on_lose", "intro", "ending", "lose",
                                        "floors"};
    const JsonValue *tower = Json_Member(manifest, "tower"), *part, *floor;
    char previous[64];
    int index = 0;
    if (!tower) return;
    if (Json_TypeOf(tower) != JSON_OBJECT) {
        Mods_Note(mod, "\"tower\" is an object (notes/tower-mode.md)");
        return;
    }
    part = Json_Member(tower, "floors");
    if (Json_TypeOf(part) != JSON_ARRAY || !Json_Count(part)) {
        Mods_Note(mod, "tower: \"floors\" is a list of at least one floor ({\"duelist\": \"Seto\"}); no tower");
        return;
    }
    snprintf(previous, sizeof(previous), "%s", defined ? config.mod : "");
    if (previous[0]) Mods_Note(previous, "tower: %s's tower, loaded after this mod, is the one the menu offers", mod);
    Tower_Reset();
    check_keys(mod, "tower", tower, known, sizeof(known) / sizeof(known[0]));
    snprintf(config.mod, sizeof(config.mod), "%s", mod);
    snprintf(config.name, sizeof(config.name), "%s", Json_String(Json_Member(tower, "name"), mod));
    if (strlen(Json_String(Json_Member(tower, "name"), "")) >= sizeof(config.name))
        Mods_Note(mod, "tower: \"name\" is at most %d bytes", (int)sizeof(config.name) - 1);
    if ((part = Json_Member(tower, "label"))) {
        const char *label = Json_String(part, NULL);
        if (!label) Mods_Note(mod, "tower: \"label\" is text");
        else if (strlen(label) >= sizeof(config.label))
            Mods_Note(mod, "tower: \"label\" is longer than %d letters", (int)sizeof(config.label) - 1);
        snprintf(config.label, sizeof(config.label), "%s", label ? label : "");
    }
    config.music = -1;
    if ((part = Json_Member(tower, "music"))) {
        long song;
        if (whole(part, 0, 0xFFF, &song) || (Json_TypeOf(part) == JSON_STRING && (song = Json_Number(part, -1)) >= 0 &&
                                            song <= 0xFFF))
            config.music = (int)song;
        else
            Mods_Note(mod, "tower: \"music\" is a song id, 0x000 to 0xFFF");
    }
    config.unlock.story = -1;
    if ((part = Json_Member(tower, "unlock"))) read_unlock(mod, part);
    if ((part = Json_Member(tower, "on_lose"))) {
        const char *word = Json_String(part, "");
        if (same_word(word, "checkpoint")) config.on_lose = TOWER_ON_LOSE_CHECKPOINT;
        else if (same_word(word, "restart")) config.on_lose = TOWER_ON_LOSE_RESTART;
        else Mods_Note(mod, "tower: \"on_lose\" is \"checkpoint\" or \"restart\"; checkpoint it is");
    }
    config.intro = read_story(mod, "tower intro", Json_Member(tower, "intro"), TOWER_SPEAKER_NONE);
    config.ending = read_story(mod, "tower ending", Json_Member(tower, "ending"), TOWER_SPEAKER_NONE);
    config.lose = read_story(mod, "tower lose", Json_Member(tower, "lose"), TOWER_SPEAKER_NONE);
    for (floor = Json_At(Json_Member(tower, "floors"), 0); floor; floor = Json_Next(floor), index++) {
        if (config.floor_count >= TOWER_MAX_FLOORS) {
            Mods_Note(mod, "tower: at most %d floors; the rest are left out", TOWER_MAX_FLOORS);
            break;
        }
        if (read_floor(mod, index, floor, &config.floors[config.floor_count])) config.floor_count++;
    }
    if (!config.floor_count) {
        Mods_Note(mod, "tower: no floor can be fought; no tower");
        Tower_Reset();
        return;
    }
    defined = 1;
}

void Tower_Build(void)
{
    int i;
    Tower_Reset();
    for (i = 0; i < Mods_LoadedCount(); i++) {
        int mod = Mods_Loaded(i);
        if (Mods_Active(mod)) Tower_Read(Mods_Id(mod), Mods_Manifest(mod));
    }
}

/* --- progress ---------------------------------------------------------------- */

uint32_t Tower_Identity(const TowerConfig *tower)
{
    uint32_t hash = 2166136261u;
    const char *text;
    if (!tower) return 0;
    for (text = tower->mod; *text; text++) hash = (hash ^ (unsigned char)*text) * 16777619u;
    hash = (hash ^ '\n') * 16777619u;
    for (text = tower->name; *text; text++) hash = (hash ^ (unsigned char)*text) * 16777619u;
    return hash ? hash : 1;
}

int Tower_FloorCleared(const TowerProgress *progress, int floor)
{
    if (!progress || floor < 0 || floor >= TOWER_MAX_FLOORS) return 0;
    return (progress->cleared[floor >> 3] >> (floor & 7)) & 1;
}

void Tower_ClearProgress(TowerProgress *progress, uint32_t owner, uint32_t tower)
{
    memset(progress, 0, sizeof(*progress));
    progress->owner = owner;
    progress->tower = tower;
}

TowerOutcome Tower_ApplyResult(const TowerConfig *tower, TowerProgress *progress, int floor, int won)
{
    TowerOutcome outcome;
    memset(&outcome, 0, sizeof(outcome));
    outcome.back_to = progress->floor;
    if (!tower || floor < 0 || floor >= tower->floor_count) return outcome;
    outcome.won = won;
    if (!won) {
        progress->floor = tower->on_lose == TOWER_ON_LOSE_RESTART ? 0 : progress->checkpoint;
        if (tower->on_lose == TOWER_ON_LOSE_RESTART) progress->checkpoint = 0;
        outcome.back_to = progress->floor;
        return outcome;
    }
    outcome.first_clear = !Tower_FloorCleared(progress, floor);
    progress->cleared[floor >> 3] |= (uint8_t)(1 << (floor & 7));
    if (floor + 1 >= tower->floor_count) {
        /* The top: told the ending, and the tower stands open again from
         * the first floor. */
        outcome.completed = 1;
        if (progress->completions < 0xFFFF) progress->completions++;
        progress->floor = 0;
        progress->checkpoint = 0;
        return outcome;
    }
    progress->floor = (uint16_t)(floor + 1);
    if (tower->floors[floor].checkpoint) {
        progress->checkpoint = (uint16_t)(floor + 1);
        outcome.checkpoint = 1;
    }
    outcome.back_to = progress->floor;
    return outcome;
}

/* --- the progress file ---------------------------------------------------------
 *
 *   tower <mod id>
 *   identity 1A2B3C4D
 *   owner 0012ABCD
 *   floor 1
 *   checkpoint 1
 *   completions 0
 *   flags 1
 *   cleared 0 3
 *   end
 *
 * one section per tower a save has played, the mod id first so a person can
 * read it. A section of a tower this run has not (its mod is off) is kept
 * as it was and written back. */

static void append(char **buffer, size_t *length, const char *line)
{
    size_t add = strlen(line);
    char *grown = realloc(*buffer, *length + add + 1);
    if (!grown) return;
    memcpy(grown + *length, line, add + 1);
    *buffer = grown;
    *length += add;
}

int Tower_ReadProgress(FILE *file, const char *mod, uint32_t identity, TowerProgress *progress, char **foreign)
{
    char line[512];
    int mine = 0, found = 0, inside = 0, stale = 0;
    size_t foreign_length = 0;
    *foreign = NULL;
    Tower_ClearProgress(progress, 0, identity);
    while (fgets(line, sizeof(line), file)) {
        char word[64], name[256];
        unsigned value;
        if (!inside) {
            if (sscanf(line, "tower %255s", name) == 1) {
                inside = 1;
                mine = !strcmp(name, mod);
                if (!mine) append(foreign, &foreign_length, line);
            }
            continue;
        }
        if (!mine && !stale) append(foreign, &foreign_length, line);
        if (sscanf(line, "%63s", word) != 1) continue;
        if (!strcmp(word, "end")) {
            inside = 0;
            mine = 0;
            stale = 0;
            continue;
        }
        if (!mine) continue;
        if (!strcmp(word, "identity") && sscanf(line, "identity %x", &value) == 1) {
            /* The same mod with another tower ("name" changed): the floors
             * may be others, so its progress begins again, and the rest of
             * the section is left out (written again as this tower's). */
            if (value != identity) {
                Tower_ClearProgress(progress, 0, identity);
                mine = 0;
                stale = 1;
                continue;
            }
            found = 1;
        } else if (!strcmp(word, "owner") && sscanf(line, "owner %x", &value) == 1) {
            progress->owner = value;
        } else if (!strcmp(word, "floor") && sscanf(line, "floor %u", &value) == 1) {
            progress->floor = (uint16_t)(value < TOWER_MAX_FLOORS ? value : 0);
        } else if (!strcmp(word, "checkpoint") && sscanf(line, "checkpoint %u", &value) == 1) {
            progress->checkpoint = (uint16_t)(value < TOWER_MAX_FLOORS ? value : 0);
        } else if (!strcmp(word, "completions") && sscanf(line, "completions %u", &value) == 1) {
            progress->completions = (uint16_t)(value < 0xFFFF ? value : 0xFFFF);
        } else if (!strcmp(word, "flags") && sscanf(line, "flags %u", &value) == 1) {
            progress->flags = (uint16_t)value;
        } else if (!strcmp(word, "cleared")) {
            const char *at = line + strlen("cleared");
            while (*at) {
                char *end;
                long floor = strtol(at, &end, 10);
                if (end == at) break;
                if (floor >= 0 && floor < TOWER_MAX_FLOORS)
                    progress->cleared[floor >> 3] |= (uint8_t)(1 << (floor & 7));
                at = end;
            }
        }
    }
    if (!found) Tower_ClearProgress(progress, 0, identity);
    return found;
}

void Tower_WriteProgress(FILE *file, const char *mod, const TowerProgress *progress, const char *foreign)
{
    int floor;
    fprintf(file, "tower %s\n", mod);
    fprintf(file, "identity %08X\n", (unsigned)progress->tower);
    fprintf(file, "owner %08X\n", (unsigned)progress->owner);
    fprintf(file, "floor %u\n", (unsigned)progress->floor);
    fprintf(file, "checkpoint %u\n", (unsigned)progress->checkpoint);
    fprintf(file, "completions %u\n", (unsigned)progress->completions);
    fprintf(file, "flags %u\n", (unsigned)progress->flags);
    fprintf(file, "cleared");
    for (floor = 0; floor < TOWER_MAX_FLOORS; floor++) {
        if (Tower_FloorCleared(progress, floor)) fprintf(file, " %d", floor);
    }
    fprintf(file, "\nend\n");
    if (foreign && *foreign) fputs(foreign, file);
}
