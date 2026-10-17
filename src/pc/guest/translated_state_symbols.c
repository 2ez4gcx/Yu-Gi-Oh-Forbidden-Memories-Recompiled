/* Save states across builds of the translated game (the Android app's).
 * libgame.so is linked at 0, so the build's symbol table (symbols/<id>.txt,
 * build_game32.write_build_identity: "offset size name", functions and
 * data) holds offsets into the image. A state carries the table of the
 * build that saved it ("arm64-symbols"), and a state of another build is
 * carried over by name, as v0.3.0's fixed game carried its states:
 *
 * - an address inside a function or variable moves with it; inside one
 *   whose size changed only its start moves (a return address into a
 *   function that changed refuses the load);
 * - an address in the gap after a symbol (string literals, padding) moves
 *   with that symbol when the gap to the next one is the same size;
 * - anything else refuses the load before anything is applied
 *   (Memories_NativeSymbolsScan), never a word left pointing at old code.
 *
 * Data of the game's guest memory is guest-addressed and needs none of
 * this; the native regions are matched by their guest tokens
 * (translated_state_memory.c). Built in every game (it needs nothing of the
 * translated one) and used by the translated one only. */
#include "translated_state_symbols.h"
#include "state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <dlfcn.h>
#endif

typedef struct TableSymbol {
    uint64_t start, size;
    char name[72];
} TableSymbol;

#define VENEER "__CortexA53843419_"

typedef struct Table {
    TableSymbol *symbols;   /* by offset */
    TableSymbol *veneers;   /* ld.lld's __CortexA53843419_<patched offset>, by offset */
    size_t veneer_count;
    unsigned *by_name;      /* indexes, by name */
    size_t count;
    uint64_t image_size;
} Table;

static Table old_table, new_table;
static int active;

static const Table *sorting;
static int compare_names(const void *left, const void *right)
{
    return strcmp(sorting->symbols[*(const unsigned *)left].name, sorting->symbols[*(const unsigned *)right].name);
}

static void clear(Table *table)
{
    free(table->symbols);
    free(table->by_name);
    free(table->veneers);
    memset(table, 0, sizeof(*table));
}

static int parse(Table *table, const char *text, size_t length, uint64_t image_size)
{
    size_t at = 0, room = 0;
    clear(table);
    table->image_size = image_size;
    while (at < length) {
        char line[160];
        size_t end = at;
        unsigned long long start, size;
        TableSymbol symbol;
        while (end < length && text[end] != '\n') end++;
        if (end - at < sizeof(line)) {
            memcpy(line, text + at, end - at);
            line[end - at] = '\0';
            /* ld.lld's Cortex-A53 erratum veneers are named by the offset
             * of the instruction they stand in for, so no other build has
             * the same names: they are kept apart and found again by that
             * instruction (veneer_map). */
            if (sscanf(line, "%llx %llx %71s", &start, &size, symbol.name) == 3 &&
                !strncmp(symbol.name, VENEER, sizeof(VENEER) - 1)) {
                TableSymbol *grown = realloc(table->veneers, (table->veneer_count + 1) * sizeof(*grown));
                if (!grown) { clear(table); return -1; }
                table->veneers = grown;
                symbol.start = start;
                symbol.size = size;
                table->veneers[table->veneer_count++] = symbol;
            } else if (sscanf(line, "%llx %llx %71s", &start, &size, symbol.name) == 3) {
                if (start >= image_size || (table->count && start < table->symbols[table->count - 1].start)) {
                    clear(table);
                    return -1;   /* not offsets in order: not this format */
                }
                symbol.start = start;
                symbol.size = size;
                if (table->count == room) {
                    TableSymbol *grown;
                    room = room ? room * 2 : 4096;
                    grown = realloc(table->symbols, room * sizeof(*grown));
                    if (!grown) { clear(table); return -1; }
                    table->symbols = grown;
                }
                table->symbols[table->count++] = symbol;
            }
        }
        at = end + 1;
    }
    if (!table->count || !(table->by_name = malloc(table->count * sizeof(*table->by_name)))) {
        clear(table);
        return -1;
    }
    for (size_t i = 0; i < table->count; i++) table->by_name[i] = (unsigned)i;
    sorting = table;
    qsort(table->by_name, table->count, sizeof(*table->by_name), compare_names);
    return 0;
}

static const TableSymbol *named(const Table *table, const char *name)
{
    size_t low = 0, high = table->count;
    while (low < high) {
        size_t middle = (low + high) / 2;
        int order = strcmp(table->symbols[table->by_name[middle]].name, name);
        if (!order) return &table->symbols[table->by_name[middle]];
        if (order < 0) low = middle + 1;
        else high = middle;
    }
    return NULL;
}

uintptr_t Memories_NativeImageBase(void)
{
#ifndef _WIN32
    Dl_info info;
    if (dladdr((void *)Memories_NativeImageBase, &info) && info.dli_fbase) return (uintptr_t)info.dli_fbase;
#endif
    return 0;
}

char *Memories_NativeSymbolsText(size_t *length)
{
    char path[640];
    FILE *file;
    long size;
    char *text = NULL;
    *length = 0;
    if (Memories_SymbolTablePath(path, sizeof(path)) || !(file = fopen(path, "rb"))) return NULL;
    if (!fseek(file, 0, SEEK_END) && (size = ftell(file)) > 0 && !fseek(file, 0, SEEK_SET) &&
        (text = malloc((size_t)size)) && fread(text, 1, (size_t)size, file) == (size_t)size) {
        *length = (size_t)size;
    } else {
        free(text);
        text = NULL;
    }
    fclose(file);
    return text;
}

int Memories_NativeSymbolsPrepare(const void *saved, size_t saved_length, uint64_t old_image_size,
                                  uint64_t new_image_size)
{
    size_t length;
    char *text;
    Memories_NativeSymbolsClear();
    if (!saved || !saved_length) return -1;
    text = Memories_NativeSymbolsText(&length);
    if (!text) return -1;
    if (parse(&old_table, saved, saved_length, old_image_size) || parse(&new_table, text, length, new_image_size)) {
        free(text);
        Memories_NativeSymbolsClear();
        return -1;
    }
    free(text);
    active = 1;
    return 0;
}

int Memories_NativeSymbolsActive(void) { return active; }
uint64_t Memories_NativeSymbolsImageSize(void) { return new_table.image_size; }

void Memories_NativeSymbolsClear(void)
{
    clear(&old_table);
    clear(&new_table);
    active = 0;
}

static int map_symbol(uint64_t offset, uint64_t *mapped);

/* An address in an old erratum veneer: the same place in the new build's
 * veneer for the instruction the old one stood in for, wherever that went. */
static int veneer_map(uint64_t offset, uint64_t *mapped, int *found)
{
    size_t i;
    unsigned long long patched;
    uint64_t moved;
    char name[72];
    *found = 0;
    for (i = 0; i < old_table.veneer_count; i++) {
        const TableSymbol *old = &old_table.veneers[i];
        if (offset < old->start || offset >= old->start + (old->size ? old->size : 8)) continue;
        *found = 1;
        if (sscanf(old->name + sizeof(VENEER) - 1, "%llx", &patched) != 1 || map_symbol(patched, &moved)) return -1;
        snprintf(name, sizeof(name), VENEER "%llX", (unsigned long long)moved);
        for (size_t j = 0; j < new_table.veneer_count; j++) {
            if (!strcmp(new_table.veneers[j].name, name)) {
                *mapped = new_table.veneers[j].start + (offset - old->start);
                return 0;
            }
        }
        /* No veneer there now: the instruction runs in place, and the
         * veneer's second word (its branch back) is the one after it. */
        *mapped = moved + (offset - old->start);
        return 0;
    }
    return -1;
}

int Memories_NativeSymbolsMap(uint64_t offset, uint64_t *mapped)
{
    int found;
    if (!active) return -1;
    if (!veneer_map(offset, mapped, &found)) return 0;
    if (found) return -1;
    return map_symbol(offset, mapped);
}

static int map_symbol(uint64_t offset, uint64_t *mapped)
{
    size_t low = 0, high = old_table.count;
    const TableSymbol *old, *now;
    uint64_t gap_old, gap_new;
    if (!active) return -1;
    while (low < high) {
        size_t middle = (low + high) / 2;
        if (old_table.symbols[middle].start <= offset) low = middle + 1;
        else high = middle;
    }
    if (!low) return -1;
    old = &old_table.symbols[low - 1];
    now = named(&new_table, old->name);
    if (!now) return -1;
    if (offset == old->start) {
        *mapped = now->start;
        return 0;
    }
    if (offset < old->start + old->size) {
        if (old->size != now->size) return -1;
        *mapped = now->start + (offset - old->start);
        return 0;
    }
    /* The gap after it: the same size up to the next symbol, or the end. */
    if (old->size != now->size) return -1;
    if (low < old_table.count) {
        const TableSymbol *next = &old_table.symbols[low], *next_now = named(&new_table, next->name);
        if (!next_now) return -1;
        gap_old = next->start - old->start;
        gap_new = next_now->start - now->start;
    } else {
        gap_old = old_table.image_size - old->start;
        gap_new = new_table.image_size - now->start;
    }
    if (gap_old != gap_new) return -1;
    *mapped = now->start + (offset - old->start);
    return 0;
}

size_t Memories_NativeSymbolsScan(const uint8_t *data, size_t size, uint64_t old_image, uint64_t image_size)
{
    size_t missing = 0;
    for (size_t at = 0; at + sizeof(uint64_t) <= size; at++) {
        uint64_t word, mapped;
        memcpy(&word, data + at, sizeof(word));
        if (word >= old_image && word - old_image < image_size && Memories_NativeSymbolsMap(word - old_image, &mapped))
            missing++;
    }
    return missing;
}
