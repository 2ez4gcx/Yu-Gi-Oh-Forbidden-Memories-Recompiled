/* A mod's "tower" as read, and a save's progress in it
 * (src/pc/tower/tower_config.c): real manifests through the real JSON
 * reader. What is checked without a game: the defaults, each key landing
 * where the screen looks for it, the notes a mistake raises (and what a
 * mistake leaves of the tower), the last mod's tower winning, a duel's
 * result moving the progress on (checkpoints, restarts, the top), and the
 * progress file keeping the sections of towers that are not this run's. */
#include "../../src/pc/tower/tower_config.c"
#include <stdarg.h>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);     \
            exit(1);                                                            \
        }                                                                       \
    } while (0)

/* --- what the reader leans on ------------------------------------------- */

static int notes;
static char said[8192];                 /* every note, one a line */
void Mods_Note(const char *id, const char *format, ...)
{
    va_list arguments;
    size_t at = strlen(said);
    notes++;
    at += (size_t)snprintf(said + at, sizeof(said) - at, "[%s] ", id);
    va_start(arguments, format);
    if (at < sizeof(said)) at += (size_t)vsnprintf(said + at, sizeof(said) - at, format, arguments);
    va_end(arguments);
    if (at + 1 < sizeof(said)) strcat(said, "\n");
}
static int heard(const char *words) { return strstr(said, words) != NULL; }
static void forget(void) { notes = 0; said[0] = 0; }

int Mods_LoadedCount(void) { return 0; }
int Mods_Loaded(int index) { (void)index; return 0; }
int Mods_Active(int mod) { (void)mod; return 0; }
const char *Mods_Id(int mod) { (void)mod; return "test"; }
const JsonValue *Mods_Manifest(int mod) { (void)mod; return NULL; }

/* Duelists: the disc's 1-39 by number, a few by name, and one a mod added
 * ("tower-mod:ghost", 40). Cards: 1-722 by number, two by name. */
int Duelists_Valid(int duelist) { return duelist >= 1 && duelist <= 40; }
int Duelists_Named(const char *text)
{
    char *end;
    long id = strtol(text, &end, 10);
    if (*text && !*end) return id >= 0 && id <= 40 ? (int)id : -1;
    if (!strcmp(text, "Seto")) return 7;
    if (!strcmp(text, "Simon Muran")) return 1;
    if (!strcmp(text, "Ghost") || !strcmp(text, "tower-mod:ghost")) return 40;
    return -1;
}
int Cards_Valid(int id) { return id >= 1 && id <= 722; }
int Cards_Named(const char *text)
{
    char *end;
    long id = strtol(text, &end, 10);
    if (*text && !*end) return (int)id;
    if (!strcmp(text, "Blue-eyes White Dragon")) return 1;
    if (!strcmp(text, "Kuriboh")) return 40;
    return 0;
}
int Tables_TerrainNamed(const char *text)
{
    if (!strcmp(text, "Forest")) return 1;
    if (!strcmp(text, "Yami") || !strcmp(text, "Dark")) return 6;
    if (!strcmp(text, "3")) return 3;
    return -1;
}

static JsonDocument *documents[32];
static int document_count;

static void read_as(const char *mod, const char *text)
{
    char error[256];
    JsonDocument *document = Json_Parse(text, error, sizeof(error));
    if (!document) fprintf(stderr, "%s\n", error);
    CHECK(document != NULL);
    CHECK(document_count < 32);
    documents[document_count++] = document;
    Tower_Read(mod, Json_Root(document));
}

static const TowerLine *line_of(TowerStory story, int index)
{
    CHECK(index < story.count);
    return &Tower_Config()->lines[story.first + index];
}

/* --- the tower as written -------------------------------------------------- */

static void defaults(void)
{
    const TowerConfig *tower;
    Tower_Reset();
    forget();
    CHECK(Tower_Config() == NULL && Tower_Mod() == NULL);
    CHECK(!strcmp(Tower_Label(), "TOWER"));
    read_as("plain", "{\"tower\": {\"floors\": [{\"duelist\": \"Seto\"}]}}");
    tower = Tower_Config();
    CHECK(tower != NULL && notes == 0);
    CHECK(!strcmp(Tower_Mod(), "plain") && !strcmp(tower->name, "plain"));
    CHECK(!strcmp(Tower_Label(), "TOWER"));
    CHECK(tower->unlock.kind == TOWER_UNLOCK_ALWAYS && !tower->unlock.after_ending && tower->unlock.story == -1);
    CHECK(tower->on_lose == TOWER_ON_LOSE_CHECKPOINT && tower->music == -1);
    CHECK(tower->floor_count == 1);
    CHECK(tower->floors[0].duelist == 7 && tower->floors[0].terrain == 0 && !tower->floors[0].deck_given);
    CHECK(tower->floors[0].life_points[0] == 0 && tower->floors[0].life_points[1] == 0);
    CHECK(tower->floors[0].rules == 0 && !tower->floors[0].checkpoint);
    CHECK(tower->floors[0].reward_card_count == 0 && tower->floors[0].reward_starchips == 0);
    CHECK(tower->intro.count == 0 && tower->ending.count == 0 && tower->floors[0].before.count == 0);
    /* A manifest without one leaves the tower as it was. */
    read_as("other", "{\"cards\": []}");
    CHECK(Tower_Config() == tower && !strcmp(Tower_Mod(), "plain"));
}

static void every_key(void)
{
    const TowerConfig *tower;
    const TowerFloor *floor;
    Tower_Reset();
    forget();
    read_as("tower-mod",
            "{\"tower\": {\"name\": \"The Shadow Tower\", \"label\": \"SHADOW TOWER\", \"music\": 42,"
            " \"unlock\": \"after_ending\", \"on_lose\": \"restart\","
            " \"intro\": \"Climb.\", \"ending\": [\"The top.\", {\"text\": \"Well done.\", \"speaker\": \"Simon Muran\"}],"
            " \"lose\": {\"text\": \"Again.\", \"portrait\": 8},"
            " \"floors\": ["
            "  {\"duelist\": \"Simon Muran\", \"field\": \"Forest\", \"checkpoint\": true,"
            "   \"life_points\": {\"player\": 100}, \"reward\": {\"cards\": [\"Kuriboh\", 2], \"starchips\": 50},"
            "   \"before\": [\"Welcome.\", {\"text\": \"Narrated.\", \"speaker\": \"none\"}], \"after\": \"Well fought.\"},"
            "  {\"duelist\": 7, \"field\": \"Dark\", \"no_fusions\": true, \"no_equips\": true, \"life_points\": 4000,"
            "   \"deck\": {\"Blue-eyes White Dragon\": 3, \"Kuriboh\": 37}},"
            "  {\"duelist\": \"tower-mod:ghost\", \"field\": 3, \"reward\": {\"unlock_duelist\": \"Ghost\", \"card\": 5},"
            "   \"deck\": [1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,4,4]}"
            " ]}}");
    tower = Tower_Config();
    if (notes) fprintf(stderr, "%s", said);
    CHECK(tower != NULL && notes == 0);
    CHECK(!strcmp(tower->name, "The Shadow Tower") && !strcmp(Tower_Label(), "SHADOW TOWER"));
    CHECK(tower->music == 42 && tower->on_lose == TOWER_ON_LOSE_RESTART);
    CHECK(tower->unlock.kind == TOWER_UNLOCK_CONDITIONS && tower->unlock.after_ending);
    CHECK(tower->intro.count == 1 && !strcmp(line_of(tower->intro, 0)->text, "Climb."));
    CHECK(line_of(tower->intro, 0)->speaker == TOWER_SPEAKER_NONE && line_of(tower->intro, 0)->portrait == -1);
    CHECK(tower->ending.count == 2 && line_of(tower->ending, 1)->speaker == 1);
    CHECK(tower->lose.count == 1 && line_of(tower->lose, 0)->portrait == 8);
    CHECK(tower->floor_count == 3);
    floor = &tower->floors[0];
    CHECK(floor->duelist == 1 && floor->terrain == 1 && floor->checkpoint);
    CHECK(floor->life_points[0] == 100 && floor->life_points[1] == 0);
    CHECK(floor->reward_card_count == 2 && floor->reward_cards[0] == 40 && floor->reward_cards[1] == 2);
    CHECK(floor->reward_starchips == 50 && floor->reward_duelist_count == 0);
    CHECK(floor->before.count == 2 && line_of(floor->before, 0)->speaker == TOWER_SPEAKER_FLOOR);
    CHECK(line_of(floor->before, 1)->speaker == TOWER_SPEAKER_NONE);
    CHECK(floor->after.count == 1 && !strcmp(line_of(floor->after, 0)->text, "Well fought."));
    floor = &tower->floors[1];
    CHECK(floor->duelist == 7 && floor->terrain == 6 && !floor->checkpoint);
    CHECK(floor->rules == (TOWER_RULE_NO_FUSIONS | TOWER_RULE_NO_EQUIPS));
    CHECK(floor->life_points[0] == 4000 && floor->life_points[1] == 4000);
    CHECK(floor->deck_given && floor->deck[0] == 1 && floor->deck[2] == 1 && floor->deck[3] == 40 && floor->deck[39] == 40);
    floor = &tower->floors[2];
    CHECK(floor->duelist == 40 && floor->terrain == 3);
    CHECK(floor->reward_duelist_count == 1 && floor->reward_duelists[0] == 40);
    CHECK(floor->reward_card_count == 1 && floor->reward_cards[0] == 5);
    CHECK(floor->deck_given && floor->deck[9] == 1 && floor->deck[10] == 2 && floor->deck[39] == 4);
    /* An object of conditions, and {} which asks for nothing. */
    read_as("cond", "{\"tower\": {\"unlock\": {\"beat\": \"Seto\", \"wins\": 3, \"story\": 69, \"after_ending\": true},"
                    " \"floors\": [{\"duelist\": 1}]}}");
    tower = Tower_Config();
    CHECK(tower->unlock.kind == TOWER_UNLOCK_CONDITIONS && tower->unlock.after_ending);
    CHECK(!strcmp(tower->unlock.beat, "Seto") && tower->unlock.wins == 3 && tower->unlock.story == 69);
    read_as("empty", "{\"tower\": {\"unlock\": {}, \"floors\": [{\"duelist\": 1}]}}");
    CHECK(Tower_Config()->unlock.kind == TOWER_UNLOCK_ALWAYS);
    read_as("always", "{\"tower\": {\"unlock\": \"always\", \"floors\": [{\"duelist\": 1}]}}");
    CHECK(Tower_Config()->unlock.kind == TOWER_UNLOCK_ALWAYS);
}

/* The last mod in load order has the menu's tower; the one it replaced is
 * told so. */
static void last_wins(void)
{
    Tower_Reset();
    forget();
    read_as("first", "{\"tower\": {\"name\": \"One\", \"floors\": [{\"duelist\": 1}]}}");
    read_as("second", "{\"tower\": {\"name\": \"Two\", \"floors\": [{\"duelist\": 7}, {\"duelist\": 1}]}}");
    CHECK(!strcmp(Tower_Mod(), "second") && !strcmp(Tower_Config()->name, "Two"));
    CHECK(Tower_Config()->floor_count == 2 && Tower_Config()->floors[0].duelist == 7);
    CHECK(notes == 1 && heard("[first] tower: second's tower"));
}

static void mistakes(void)
{
    const TowerConfig *tower;
    /* What makes no tower at all. */
    Tower_Reset();
    forget();
    read_as("m", "{\"tower\": [1]}");
    CHECK(Tower_Config() == NULL && heard("\"tower\" is an object"));
    read_as("m", "{\"tower\": {\"name\": \"x\"}}");
    CHECK(Tower_Config() == NULL && heard("\"floors\" is a list of at least one floor"));
    read_as("m", "{\"tower\": {\"floors\": []}}");
    CHECK(Tower_Config() == NULL);
    forget();
    read_as("m", "{\"tower\": {\"floors\": [{\"duelist\": \"Nobody\"}, {\"field\": \"Forest\"}, 7]}}");
    CHECK(Tower_Config() == NULL);
    CHECK(heard("tower floors[0]: no duelist \"Nobody\""));
    CHECK(heard("tower floors[1]: a floor needs a \"duelist\"; left out"));
    CHECK(heard("tower floors[2]: a floor is an object"));
    CHECK(heard("tower: no floor can be fought; no tower"));

    /* What leaves the tower, without the part written wrong. */
    forget();
    read_as("m", "{\"tower\": {\"unlock\": \"after-ending\", \"on_lose\": \"retry\", \"colour\": 1, \"music\": 99999,"
                 " \"label\": \"A LABEL FAR LONGER THAN THIRTY ONE LETTERS\","
                 " \"floors\": [{\"duelist\": \"Nobody\"},"
                 "  {\"duelist\": \"Seto\", \"field\": \"Lava\", \"no_fusions\": \"true\", \"life_points\": 0,"
                 "   \"deck\": {\"Kuriboh\": 39}, \"reward\": {\"cards\": [\"Nothing\"], \"starchips\": -5, \"gold\": 1},"
                 "   \"before\": [{\"text\": \"Hi.\", \"portrait\": 30}, 5], \"checkpoint\": 1, \"boss\": true}]}}");
    tower = Tower_Config();
    CHECK(tower != NULL && tower->floor_count == 1);
    CHECK(tower->unlock.kind == TOWER_UNLOCK_NEVER && heard("the tower stays locked"));
    CHECK(tower->on_lose == TOWER_ON_LOSE_CHECKPOINT && heard("\"on_lose\" is \"checkpoint\" or \"restart\""));
    CHECK(heard("tower: unknown key \"colour\"") && heard("tower floors[1]: unknown key \"boss\""));
    CHECK(tower->music == -1 && heard("\"music\" is a song id"));
    CHECK(strlen(tower->label) == TOWER_LABEL - 1 && heard("\"label\" is longer than 31 letters"));
    CHECK(tower->floors[0].duelist == 7 && tower->floors[0].terrain == 0 && heard("\"field\" is none, Forest"));
    CHECK(tower->floors[0].rules == 0 && heard("\"no_fusions\" is true or false, without quotes"));
    CHECK(tower->floors[0].life_points[0] == 0 && heard("\"life_points\" is 1 to 65535"));
    CHECK(!tower->floors[0].deck_given && heard("a deck is 40 cards, and this one has 39"));
    CHECK(tower->floors[0].reward_card_count == 0 && heard("tower floors[1] reward: no card \"Nothing\""));
    CHECK(tower->floors[0].reward_starchips == 0 && heard("\"starchips\" is 0 to 999999"));
    CHECK(heard("tower floors[1]: unknown key \"gold\""));
    CHECK(!tower->floors[0].checkpoint && heard("\"checkpoint\" is true or false"));
    CHECK(tower->floors[0].before.count == 1 && line_of(tower->floors[0].before, 0)->portrait == -1);
    CHECK(heard("\"portrait\" is one of the campaign's portraits, 0 to 24"));
    CHECK(heard("tower floors[1] before[1]: a line is text"));
    /* A deck naming a card that is not there deals the opponent's own. */
    forget();
    read_as("m", "{\"tower\": {\"floors\": [{\"duelist\": 1, \"deck\": [\"Kuriboh\", \"Nothing\"]},"
                 " {\"duelist\": 1, \"deck\": 5}]}}");
    CHECK(!Tower_Config()->floors[0].deck_given && heard("the opponent's own deck is dealt instead"));
    CHECK(!Tower_Config()->floors[1].deck_given && heard("a deck is {card: copies} or a list of forty cards"));
    /* Conditions written wrong keep it locked too. */
    forget();
    read_as("m", "{\"tower\": {\"unlock\": {\"wins\": \"many\"}, \"floors\": [{\"duelist\": 1}]}}");
    CHECK(Tower_Config()->unlock.kind == TOWER_UNLOCK_NEVER && heard("\"wins\" is 0 to 65535"));
    read_as("m", "{\"tower\": {\"unlock\": 3, \"floors\": [{\"duelist\": 1}]}}");
    CHECK(Tower_Config()->unlock.kind == TOWER_UNLOCK_NEVER);
}

/* --- progress ---------------------------------------------------------------- */

static void results(void)
{
    TowerProgress progress;
    TowerOutcome outcome;
    TowerConfig *tower;
    Tower_Reset();
    forget();
    read_as("p", "{\"tower\": {\"floors\": [{\"duelist\": 1, \"checkpoint\": true}, {\"duelist\": 2},"
                 " {\"duelist\": 3}]}}");
    tower = (TowerConfig *)Tower_Config();
    Tower_ClearProgress(&progress, 0x1234, Tower_Identity(tower));
    CHECK(progress.owner == 0x1234 && progress.floor == 0 && !Tower_FloorCleared(&progress, 0));
    /* Floor 1 won: a checkpoint, its reward the first time. */
    outcome = Tower_ApplyResult(tower, &progress, 0, 1);
    CHECK(outcome.won && outcome.first_clear && outcome.checkpoint && !outcome.completed);
    CHECK(progress.floor == 1 && progress.checkpoint == 1 && Tower_FloorCleared(&progress, 0));
    /* Floor 2 won, floor 3 lost: back to the checkpoint, floor 2. */
    outcome = Tower_ApplyResult(tower, &progress, 1, 1);
    CHECK(outcome.first_clear && !outcome.checkpoint && progress.floor == 2 && progress.checkpoint == 1);
    outcome = Tower_ApplyResult(tower, &progress, 2, 0);
    CHECK(!outcome.won && !outcome.first_clear && outcome.back_to == 1 && progress.floor == 1);
    CHECK(!Tower_FloorCleared(&progress, 2));
    /* Floor 2 again: no reward the second time. */
    outcome = Tower_ApplyResult(tower, &progress, 1, 1);
    CHECK(outcome.won && !outcome.first_clear && progress.floor == 2);
    /* The top: the ending, and the tower from the first floor again. */
    outcome = Tower_ApplyResult(tower, &progress, 2, 1);
    CHECK(outcome.completed && outcome.first_clear && progress.floor == 0 && progress.checkpoint == 0);
    CHECK(progress.completions == 1 && Tower_FloorCleared(&progress, 2));
    /* "restart": a loss goes back to the first floor, checkpoint or not. */
    tower->on_lose = TOWER_ON_LOSE_RESTART;
    Tower_ApplyResult(tower, &progress, 0, 1);
    CHECK(progress.floor == 1 && progress.checkpoint == 1);
    outcome = Tower_ApplyResult(tower, &progress, 1, 0);
    CHECK(outcome.back_to == 0 && progress.floor == 0 && progress.checkpoint == 0);
    /* A floor the tower has not is nothing. */
    outcome = Tower_ApplyResult(tower, &progress, 7, 1);
    CHECK(!outcome.won && progress.floor == 0);
    /* The identity follows the mod and the name. */
    CHECK(Tower_Identity(tower) != 0);
    {
        TowerConfig other = *tower;
        snprintf(other.name, sizeof(other.name), "Another");
        CHECK(Tower_Identity(&other) != Tower_Identity(tower));
    }
}

static void progress_file(void)
{
    TowerProgress progress, back;
    char *foreign = NULL;
    FILE *file = tmpfile();
    CHECK(file != NULL);
    Tower_ClearProgress(&progress, 0xABCD1234u, 0x55AA55AAu);
    progress.floor = 2;
    progress.checkpoint = 1;
    progress.completions = 3;
    progress.flags = 1;
    progress.cleared[0] = 0x05;
    progress.cleared[12] = 0x04;                       /* floor 98, the last there can be */
    fputs("tower old-mod\nidentity 00000001\nfloor 4\nend\n", file);
    Tower_WriteProgress(file, "my-mod", &progress, "tower gone-mod\nidentity 00000002\nend\n");
    rewind(file);
    CHECK(Tower_ReadProgress(file, "my-mod", 0x55AA55AAu, &back, &foreign));
    CHECK(back.owner == 0xABCD1234u && back.tower == 0x55AA55AAu && back.floor == 2 && back.checkpoint == 1);
    CHECK(back.completions == 3 && back.flags == 1);
    CHECK(Tower_FloorCleared(&back, 0) && !Tower_FloorCleared(&back, 1) && Tower_FloorCleared(&back, 2));
    CHECK(Tower_FloorCleared(&back, 98));
    /* The other towers' sections, whole, to be written back. */
    CHECK(foreign && strstr(foreign, "tower old-mod\nidentity 00000001\nfloor 4\nend\n"));
    CHECK(strstr(foreign, "tower gone-mod\nidentity 00000002\nend\n") && !strstr(foreign, "my-mod"));
    free(foreign);
    /* The same mod with another tower: none of its progress. */
    rewind(file);
    CHECK(!Tower_ReadProgress(file, "my-mod", 0x11111111u, &back, &foreign));
    CHECK(back.floor == 0 && back.tower == 0x11111111u && !Tower_FloorCleared(&back, 0));
    free(foreign);
    /* A tower the file has not. */
    rewind(file);
    CHECK(!Tower_ReadProgress(file, "new-mod", 7, &back, &foreign));
    CHECK(foreign && strstr(foreign, "tower my-mod"));
    free(foreign);
    fclose(file);
}

int main(void)
{
    int i;
    defaults();
    every_key();
    last_wins();
    mistakes();
    results();
    progress_file();
    Tower_Reset();
    for (i = 0; i < document_count; i++) Json_Free(documents[i]);
    printf("tower: ok\n");
    return 0;
}
