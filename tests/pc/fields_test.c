/* The mod-defined field registry: retail aliases, custom ids and their
 * picture paths. */
#include "../../src/pc/cards/fields.c"
#include <assert.h>
#include <stdarg.h>

static int notes;
void Mods_Note(const char *id, const char *format, ...)
{
    va_list args;
    (void)id;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    notes++;
}
int Mods_LoadedCount(void) { return 0; }
int Mods_Loaded(int index) { return index; }
int Mods_Active(int mod) { return mod >= 0; }
const char *Mods_Id(int mod) { (void)mod; return ""; }
const char *Mods_Directory(int mod) { (void)mod; return "."; }
const JsonValue *Mods_Manifest(int mod) { (void)mod; return NULL; }
int Paths_Contained(const char *path) { return path && path[0] != '/'; }
int TexturePack_AddDiscImageGroup(unsigned group, const char *file, const char *archive, unsigned offset, int words,
                                  int rows, int bpp, unsigned clut_offset, int clut_entries, int stride,
                                  int crop_left, int crop_width, int source_x, int source_y, int source_w,
                                  int source_h, unsigned rank)
{
    (void)group; (void)file; (void)archive; (void)offset; (void)words; (void)rows; (void)bpp; (void)clut_offset;
    (void)clut_entries; (void)stride; (void)crop_left; (void)crop_width; (void)source_x; (void)source_y;
    (void)source_w; (void)source_h; (void)rank;
    return 1;
}
void TexturePack_SelectGroup(unsigned group) { (void)group; }

int main(void)
{
    char error[128];
    JsonDocument *first = Json_Parse("{\"fields\":{\"fields\":[{\"replace\":\"Normal\"},{\"replace\":\"Forest\",\"name\":\"Grove\"},"
                                     "{\"name\":\"Volcano\"}]}}", error, sizeof(error));
    JsonDocument *second = Json_Parse("{\"fields\":{\"fields\":[{\"id\":8,\"name\":\"Volcano\"},"
                                      "{\"id\":256,\"name\":\"Sky\"}]}}", error, sizeof(error));
    assert(first && second);
    Fields_Clear();
    assert(Fields_Find("Normal") == 0 && Fields_Find("Forest") == 1 && Fields_Find("Sea") == 5);
    Fields_Add("one", Json_Root(first));
    assert(Fields_Find("Grove") == 1 && Fields_Find("Forest") == 1);
    assert(Fields_Find("Volcano") == 7 && Fields_Declared(7));
    assert(Fields_Value(Json_Member(Json_At(Json_Member(Json_Member(Json_Root(first), "fields"), "fields"), 1), "replace")) == 1);
    Fields_Add("two", Json_Root(second));
    assert(Fields_Find("Sky") == -1 && notes == 2);

    /* A bare numeric id colliding with one already declared (here id 7,
     * Volcano, from "first") is rejected rather than silently renaming it:
     * "replace" is the sanctioned way to retarget an id by name. */
    JsonDocument *third = Json_Parse("{\"fields\":{\"fields\":[{\"id\":7,\"name\":\"Lava\"}]}}", error, sizeof(error));
    assert(third);
    Fields_Add("three", Json_Root(third));
    assert(Fields_Find("Volcano") == 7 && Fields_Find("Lava") == -1 && notes == 3);
    Json_Free(third);

    Json_Free(first);
    Json_Free(second);

    /* change_field's own dispatch: Fields_Activate, then the retail terrain
     * effect it started lands and confirms it with Fields_Observe. The
     * field bank must still report the custom field, not fall back to -1
     * the moment its own carrier's effect resolves. */
    Fields_Activate(7);
    assert(Fields_Active() == 7);
    Fields_Observe();
    assert(Fields_Active() == 7);

    /* An ordinary retail field spell played after that (or anything else
     * that resolves DuelEffect_ApplyTerrain without going through
     * Fields_Activate first) must make the custom field stale rather than
     * leave the field bank still reporting Volcano. */
    Fields_Observe();
    assert(Fields_Active() == -1);

    /* A new duel (func_80024DC8's reset) must clear a field left active at
     * the end of the previous one, even mid-dispatch (active_pending set). */
    Fields_Activate(7);
    assert(Fields_Active() == 7);
    Fields_ResetActive();
    assert(Fields_Active() == -1);
    Fields_Observe();
    assert(Fields_Active() == -1);

    Fields_Clear();
    return 0;
}
