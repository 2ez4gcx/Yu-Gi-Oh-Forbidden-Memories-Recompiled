/* View > Opponent's name for COM on the duel results screens, when a
 * translation gives those screens text of its own (strings 0x40-0x45) and
 * its own YOU, COM and winner strings (0x3E, 0x3F, 0x3D, called by the
 * labels L050E, L051C and L0504): the names go in where the translation's
 * labels were, the columns move by the labels' width (VOCE is four letters,
 * not three), and what cannot be shown leaves the translation's own words.
 *
 * Including the translation lets this drive its static state (add_unit,
 * Text_Resolve and Text_Retarget as the text boxes call them) without a
 * window, a disc or the mods; the stubs below stand for everything else. */
#include "pc/text/translation.c"
#include <assert.h>

/* --- stubs ---------------------------------------------------------------- */

#undef fopen
#undef remove
FILE *Memories_Fopen(const char *path, const char *mode) { return fopen(path, mode); }

static int opponent_name = 1, opponent_id = 7, entry_letters = 254;
static const char *opponent = "Weevil", *player = "Joe";
u8 gDuel_bWinnerSide;
short gCard_asNameSortKey[1];
int gCard_nCount;

int Settings_Get(SettingId id) { return id == SET_OPPONENT_NAME ? opponent_name : 0; }
int Tables_OpponentId(void) { return opponent_id; }
const char *Tables_DuelistShortName(int duelist) { return duelist >= 1 && duelist == opponent_id ? opponent : NULL; }
int Tables_DuelistRenamed(int duelist) { (void)duelist; return 0; }
int Tables_ShortenName(const char *name, char *out) { (void)name; (void)out; return 0; }
const char *Duelists_Name(int duelist) { (void)duelist; return ""; }
const char *HdText_PlayerName(void) { return player; }
int TextEntries_PageLetters(int channel) { (void)channel; return entry_letters; }

/* Lower case letters 27-52, capitals 1-26, a space 0. E with a circumflex
 * (0xCA) is an added glyph, 0x100; A with a tilde (0xC3) 0x1F8, whose second
 * byte, F8, with the 02 of a "B" after it, looks like a step to a reader of
 * bytes; a with a tilde (0xE3) 0x102, whose plain letter is an a; sharp s
 * (0xDF) 0x103, which has none (the screen keeps its word). The middle dot
 * of "VENCEDOR ..." is 63. */
int Glyphs_Code(uint32_t character)
{
    if (character == ' ') return 0;
    if (character >= 'A' && character <= 'Z') return (int)(character - 'A' + 1);
    if (character >= 'a' && character <= 'z') return (int)(character - 'a' + 27);
    if (character >= '0' && character <= '9') return (int)(character - '0' + 53);
    if (character == 0xB7) return 63;
    if (character == 0xCA) return 0x100;
    if (character == 0xC3) return 0x1F8;
    if (character == 0xE3) return 0x102;
    if (character == 0xDF) return 0x103;
    return -1;
}
uint32_t Glyphs_Character(int code)
{
    (void)code;
    return 0;
}
int Glyphs_Base(int code)
{
    if (code == 0x100) return 5;  /* E */
    if (code == 0x1F8) return 1;  /* A */
    if (code == 0x102) return 27; /* a */
    if (code == 0x103) return 0;
    return code;
}
uint32_t Glyphs_SortCharacter(int code) { return (uint32_t)code; }
void Glyphs_AddFont(const char *path) { (void)path; }

const char *Mods_Id(int mod) { (void)mod; return "test"; }
int Mods_LoadedCount(void) { return 0; }
int Mods_Loaded(int index) { return index; }
int Mods_Active(int mod) { (void)mod; return 1; }
int Mods_File(int mod, const char *key, int index, char *path, size_t size, const char **name)
{ (void)mod; (void)key; (void)index; (void)path; (void)size; (void)name; return 0; }
void Mods_Note(const char *id, const char *format, ...) { (void)id; (void)format; }
int Language_Current(void) { return 0; }
const char *Language_Label(int language) { (void)language; return ""; }
char *Language_Listing(size_t *length) { (void)length; return NULL; }
void Language_Drop(void) {}
int Log_Wanted(LogChannel channel) { (void)channel; return 0; }
void Log_Printf(LogChannel channel, const char *format, ...) { (void)channel; (void)format; }

const unsigned char *CardDrops_Text(int id) { (void)id; return NULL; }
const unsigned char *CardPassword_Text(int id) { (void)id; return NULL; }
const unsigned char *DeckMenu_Text(int id) { (void)id; return NULL; }
const unsigned char *FreeDuelPage_Text(int id) { (void)id; return NULL; }
const unsigned char *PackShop_Text(int id) { (void)id; return NULL; }
unsigned char *PackShop_Retarget(const unsigned char *cursor) { (void)cursor; return NULL; }
const unsigned char *StarchipPrize_Text(int id) { (void)id; return NULL; }
const unsigned char *Tower_Text(int id) { (void)id; return NULL; }
unsigned char *Tower_Retarget(const unsigned char *cursor) { (void)cursor; return NULL; }
const unsigned char *Stars_NameText(int star) { (void)star; return NULL; }
int Stars_Named(int star) { (void)star; return 0; }
int Cards_BaseId(int id) { return id; }
const unsigned char *Cards_NameText(int id) { (void)id; return NULL; }
const unsigned char *Cards_DescriptionText(int id) { (void)id; return NULL; }

/* --- the translation ------------------------------------------------------ *
 * Shaped like the Portuguese mod's menus (b06_menus.txt): its YOU, COM and
 * winner strings carry the labels the retail ones have, and the result
 * screens call them by label. VOCE is four letters; the first line of 0x44
 * has COM's step 0x30, 0x45's is 0x18. */
static const char listing[] =
    "@bank dialog\n"
    "\n"
    "[003D]  {:L0504}\n"
    "{f8 18 L0514 L0522 L0518 L0526}\n"
    "\n"
    "[003E]  {:L050E}\n"
    "{f8 17 L0514 L0518}\n"
    "\n"
    "{:L0514}\n"
    "VOC\xC3\x8A{end}\n"
    "\n"
    "{:L0518}\n"
    "  1P{end}\n"
    "\n"
    "[003F]  {:L051C}\n"
    "{f8 17 L0522 L0526}\n"
    "\n"
    "{:L0522}\n"
    "COM{end}\n"
    "\n"
    "{:L0526}\n"
    " 2P{end}\n"
    "\n"
    "[0040]\n"
    "{f8 01 08}{f8 0A 01} TOTAL{cont}\n"
    "{:L0543}\n"
    "{f8 0A 00}{f8 04 01}{f8 06 F8 FF}  {f8 02 30}{call L050E}{f8 02 30}{call L051C}{end}\n"
    "\n"
    "[0041]\n"
    "{f8 01 08}{f8 0A 01} ATTRITION{jump L0543}\n"
    "\n"
    "[0043]\n"
    "{f8 01 10}{f8 0A 02}PLAIN{end}\n"
    "\n"
    "[0044]\n"
    "{f8 04 01}\n"
    "{f8 06 F8 FF}               {f8 02 30}{call L050E}{f8 02 30}{call L051C}{f8 01 20}\n"
    "{f8 06 F8 FF}          {f8 02 58}{call L050E}{f8 02 30}{call L051C}{f8 01 20}\n"
    "{f8 06 F8 FF}  {f8 02 18}VENCEDOR \xC2\xB7\xC2\xB7\xC2\xB7 \xC3\x83" "B {call L0504}{f8 01 42}{f8 02 38}     {sp}\n"
    "{f8 02 38}       CARTAS{end}\n"
    "\n"
    "[0045]\n"
    "{f8 04 01}\n"
    "{f8 06 F8 FF}  {f8 02 18}{call L050E}{f8 02 18}{call L051C}{end}\n"
    "\n"
    "[0050]\n"
    "{f8 04 01}\n"
    "{f8 06 F8 FF}    {call L050E}   {call L051C}{end}\n";

static unsigned char retail_dummy[16];

static const TextUnit *the_unit(void) { return units[0]; }

static size_t string_at(int id) { return (size_t)(overrides[id] - the_unit()->data); }

static size_t label_at(unsigned name)
{
    const TextUnit *unit = the_unit();
    int i;
    for (i = 0; i < unit->label_count; i++) {
        if (unit->labels[i].name == name) return unit->labels[i].offset;
    }
    assert(!"no such label");
    return 0;
}

/* The offset of the `n`th call or jump (counting from 0) that lands on the
 * label `name`, from the string `first` on; the test's letters are never
 * FC or FD, but its extended letter's second byte is F8. */
static size_t call_to(unsigned name, int first, int n)
{
    const TextUnit *unit = the_unit();
    size_t i;
    for (i = string_at(first); i + 2 < unit->size; i++) {
        unsigned operand = unit->data[i + 1] | unit->data[i + 2] << 8;
        if ((unit->data[i] != 0xFC && unit->data[i] != 0xFD) || operand >= (unsigned)unit->target_count) continue;
        if (unit->targets[operand] == unit->data + label_at(name) && n-- == 0) return i;
    }
    assert(!"no such call");
    return 0;
}

/* What the text box does at the call at offset `at` of the unit, the
 * cursor just past its operand: in the unit's own text, or in a copy of it
 * (`delta` bytes on). */
static const unsigned char *at_call(size_t at, ptrdiff_t delta)
{
    const TextUnit *unit = the_unit();
    unsigned operand = unit->data[at + 1] | unit->data[at + 2] << 8;
    return Text_Retarget((unsigned char *)(unit->data + at + 3 + delta), operand);
}

/* A name as the side strings have it: glyph codes, ended by FF. */
static int is_name(const unsigned char *got, const char *wanted)
{
    for (; *wanted; wanted++, got++) {
        if (*got != Glyphs_Code((unsigned char)*wanted)) return 0;
    }
    return *got == 0xFF;
}

static int is_label(const unsigned char *got, unsigned name) { return got == the_unit()->data + label_at(name); }

static void set(const char *opponent_is, const char *player_is, int winner, int named)
{
    opponent = opponent_is;
    player = player_is;
    gDuel_bWinnerSide = (u8)winner;
    opponent_name = named;
    opponent_id = 7;
}

/* Weevil against Joe: the names go in, over the translation's labels, and
 * the columns move by the labels' width. */
static void names_and_columns(void)
{
    const TextUnit *unit = the_unit();
    const unsigned char *own = overrides[0x44], *copy;
    size_t you0, you1, com0, com1, won, k;
    ptrdiff_t delta;
    set("Weevil", "Joe", 0, 1);
    copy = Text_Resolve(0x44, retail_dummy);
    assert(copy != own);
    delta = copy - own;
    you0 = call_to(0x050E, 0x44, 0);
    you1 = call_to(0x050E, 0x44, 1);
    com0 = call_to(0x051C, 0x44, 0);
    com1 = call_to(0x051C, 0x44, 1);
    won = call_to(0x0504, 0x44, 0);
    assert(you0 < com0 && com0 < you1 && you1 < com1 && com1 < won);
    /* Joe is three letters where VOCE is four: a cell (8) more before it. */
    assert(is_name(at_call(you0, delta), "Joe") && is_name(at_call(you1, delta), "Joe"));
    assert(unit->data[you0 - 1] == 0x30 && unit->data[you0 + delta - 1] == 0x38);
    assert(unit->data[you1 - 1] == 0x58 && unit->data[you1 + delta - 1] == 0x60);
    /* Weevil is three letters past COM: 7 pixels each, less before it. */
    assert(is_name(at_call(com0, delta), "Weevil") && is_name(at_call(com1, delta), "Weevil"));
    assert(unit->data[com0 - 1] == 0x30 && unit->data[com0 + delta - 1] == 0x30 - 21);
    assert(unit->data[com1 - 1] == 0x30 && unit->data[com1 + delta - 1] == 0x30 - 21);
    /* WINNER: the player's, ending where VOCE did. The letter A with a tilde
     * is F1 F8 and "B" 02: no step, and no byte of the text changed but the
     * real step's (the control code before it). */
    assert(is_name(at_call(won, delta), "Joe"));
    for (k = won; !(unit->data[k] == 0xF8 && unit->data[k + 1] == 0x02 && unit->data[k + 2] == 0x18); k--) {}
    assert(unit->data[k + 2] == 0x18 && unit->data[k + delta + 2] == 0x20);
    assert(memcmp(unit->data + k + 3, unit->data + k + 3 + delta, won - k - 3) == 0);
    /* The unit's own text is as it was. */
    assert(unit->data[you0 - 1] == 0x30);
    /* Outside the result strings (0x50) the labels are what they call. */
    assert(is_label(at_call(call_to(0x050E, 0x50, 0), 0), 0x050E));
    assert(is_label(at_call(call_to(0x051C, 0x50, 0), 0), 0x051C));
}

/* 0x41 jumps into 0x40's tail (L0543): in the copy, it stays in the copy,
 * where COM's column was moved. */
static void jumps_stay_in_the_copy(void)
{
    const TextUnit *unit = the_unit();
    const unsigned char *copy;
    size_t jump, com;
    ptrdiff_t delta;
    set("Weevil", "Joe", 0, 1);
    copy = Text_Resolve(0x41, retail_dummy);
    assert(copy != overrides[0x41]);
    delta = copy - overrides[0x41];
    jump = call_to(0x0543, 0x41, 0);
    assert(at_call(jump, delta) == unit->data + label_at(0x0543) + delta);
    /* The tail's COM: 0x40's step less. */
    com = call_to(0x051C, 0x40, 0);
    assert(is_name(at_call(com, delta), "Weevil"));
    assert(unit->data[com + delta - 1] == 0x30 - 21);
    /* 0x45's COM step is 0x18; Weevil's 21 fits. */
    copy = Text_Resolve(0x45, retail_dummy);
    com = call_to(0x051C, 0x45, 0);
    assert(unit->data[com + (copy - overrides[0x45]) - 1] == 0x18 - 21);
}

/* A name the small font has no letter for, a name that does not fit and a
 * player with no name are not shown: the translation's own words are. */
static void what_cannot_be_shown(void)
{
    const TextUnit *unit = the_unit();
    const unsigned char *copy;
    size_t you, com, won, k;
    you = call_to(0x050E, 0x44, 0);
    com = call_to(0x051C, 0x44, 0);
    won = call_to(0x0504, 0x44, 0);
    /* Sharp s: the font has none, and no plain letter for it. */
    set("Stra\xDF", "Joe", 0, 1);
    assert(Text_Resolve(0x44, retail_dummy) == overrides[0x44]);
    assert(is_label(at_call(com, 0), 0x051C));
    assert(is_label(at_call(you, 0), 0x050E));
    /* An accent past the font's: its plain letter. */
    set("Jo\xE3o", "Joe", 0, 1);
    copy = Text_Resolve(0x44, retail_dummy);
    assert(copy != overrides[0x44] && is_name(at_call(com, copy - overrides[0x44]), "Joao"));
    /* Nine letters need 42 pixels, and 0x45's step before COM is 24. */
    set("Alexander", "Joe", 0, 1);
    assert(Text_Resolve(0x44, retail_dummy) == overrides[0x44]);
    assert(is_label(at_call(com, 0), 0x051C));
    /* A name with a full stop: the player's is You, and You is no name. */
    set("Weevil", "Joe!", 0, 1);
    copy = Text_Resolve(0x44, retail_dummy);
    assert(is_label(at_call(you, copy - overrides[0x44]), 0x050E));
    assert(is_label(at_call(won, copy - overrides[0x44]), 0x0504));
    assert(is_name(at_call(com, copy - overrides[0x44]), "Weevil"));
    /* The computer won: its name, ending where COM did (three letters). */
    set("Weevil", "Joe", 1, 1);
    copy = Text_Resolve(0x44, retail_dummy);
    assert(is_name(at_call(won, copy - overrides[0x44]), "Weevil"));
    /* Three letters of COM, six of the name: 8 pixels each, from 0x18. */
    for (k = won; !(unit->data[k] == 0xF8 && unit->data[k + 1] == 0x02 && unit->data[k + 2] == 0x18); k--) {}
    assert(unit->data[k + 2 + (copy - overrides[0x44])] == 0);
}

/* With the setting off, or in a two-player duel, nothing is different. */
static void nothing_changes(void)
{
    size_t you = call_to(0x050E, 0x44, 0), com = call_to(0x051C, 0x44, 0), won = call_to(0x0504, 0x44, 0);
    set("Weevil", "Joe", 0, 0);
    assert(Text_Resolve(0x44, retail_dummy) == overrides[0x44]);
    assert(Text_Resolve(0x41, retail_dummy) == overrides[0x41]);
    assert(is_label(at_call(you, 0), 0x050E) && is_label(at_call(com, 0), 0x051C));
    assert(is_label(at_call(won, 0), 0x0504));
    set("Weevil", "Joe", 0, 1);
    opponent_id = 0;
    assert(Text_Resolve(0x44, retail_dummy) == overrides[0x44]);
    assert(is_label(at_call(com, 0), 0x051C) && is_label(at_call(you, 0), 0x050E));
}

/* A translation with result strings of its own and none for YOU, COM and
 * the winner calls the game's (labels it does not define are the retail
 * text at their place): the names go in, over words of three letters. */
static const char game_listing[] =
    "@bank dialog\n"
    "\n"
    "[0044]\n"
    "{f8 04 01}\n"
    "{f8 06 F8 FF}  {f8 02 30}{call L050E}{f8 02 30}{call L051C}{f8 01 20}{f8 02 18}VENCEDOR {call L0504}{end}\n";

static size_t call_to_game(unsigned at, int n)
{
    const TextUnit *unit = the_unit();
    size_t i;
    for (i = string_at(0x44); i + 2 < unit->size; i++) {
        unsigned operand = unit->data[i + 1] | unit->data[i + 2] << 8;
        if ((unit->data[i] != 0xFC && unit->data[i] != 0xFD) || operand >= (unsigned)unit->target_count) continue;
        if (unit->targets[operand] == (const unsigned char *)(uintptr_t)(0x801B0000u + at) && n-- == 0) return i;
    }
    assert(!"no such call");
    return 0;
}

static void the_games_own_words(void)
{
    const unsigned char *copy;
    size_t you, com, won;
    ptrdiff_t delta;
    set("Weevil", "Joe", 0, 1);
    copy = Text_Resolve(0x44, retail_dummy);
    assert(copy != overrides[0x44]);
    delta = copy - overrides[0x44];
    you = call_to_game(0x050E, 0);
    com = call_to_game(0x051C, 0);
    won = call_to_game(0x0504, 0);
    assert(is_name(at_call(you, delta), "Joe") && is_name(at_call(won, delta), "Joe"));
    assert(the_unit()->data[you + delta - 1] == 0x30);   /* three letters of YOU, three of Joe */
    assert(is_name(at_call(com, delta), "Weevil") && the_unit()->data[com + delta - 1] == 0x30 - 21);
    /* With the setting off, the game's own words stand. */
    set("Weevil", "Joe", 0, 0);
    assert(Text_Resolve(0x44, retail_dummy) == overrides[0x44]);
    assert(at_call(com, 0) == (const unsigned char *)(uintptr_t)(0x801B0000u + 0x051C));
}

int main(void)
{
    FILE *file = fopen("results_names_test.tmp", "wb");
    assert(file);
    fputs(listing, file);
    fclose(file);
    add_unit(0, "results_names_test.tmp", "results_names_test.tmp");
    remove("results_names_test.tmp");
    assert(unit_count == 1 && overrides[0x44] && overrides[0x3E] && overrides[0x40]);
    names_and_columns();
    jumps_stay_in_the_copy();
    what_cannot_be_shown();
    nothing_changes();
    /* Another translation, as a fresh start has it. */
    unit_count = 0;
    memset(overrides, 0, 0x10000 * sizeof(*overrides));
    memset(unit_results, 0, sizeof(unit_results));
    file = fopen("results_names_test.tmp", "wb");
    assert(file);
    fputs(game_listing, file);
    fclose(file);
    add_unit(0, "results_names_test.tmp", "results_names_test.tmp");
    remove("results_names_test.tmp");
    assert(unit_count == 1 && overrides[0x44] && !overrides[0x3E]);
    the_games_own_words();
    puts("results_names_test: ok");
    return 0;
}
