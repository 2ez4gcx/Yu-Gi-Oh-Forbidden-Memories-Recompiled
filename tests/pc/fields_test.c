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
    Json_Free(first);
    Json_Free(second);
    Fields_Clear();
    return 0;
}
