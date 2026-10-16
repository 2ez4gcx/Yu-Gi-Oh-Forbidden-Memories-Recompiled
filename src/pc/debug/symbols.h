#ifndef MEMORIES_PC_DEBUG_SYMBOLS_H
#define MEMORIES_PC_DEBUG_SYMBOLS_H
#include <stddef.h>
#include <stdint.h>

int Symbols_Load(void);
/* The game's own variables by name, where this build keeps them: the
 * build's symbols/<id>.vars.txt (tools/pc/build_game32.py), written for every
 * target, which holds the small data too. Not the retail addresses
 * (config/pc/guest_addresses.txt): a variable a game unit defines sits in the
 * executable's game_data or game_bss, only an undefined one is pinned to its
 * retail address. 0 with the address and extent (0: unknown) of the first
 * variable of that name; -1 when there is none; -2 when the build has no
 * variable table at all. */
int Symbols_FindVariable(const char *name, uintptr_t *address, uintptr_t *size);
const char *Symbols_Lookup(uintptr_t address, uintptr_t *offset);
/* Code loaded after startup (a mod's object file, src/pc/mods): its
 * functions, so crash and hang reports can name them. One call per object. */
typedef struct {
    uintptr_t address, size;
    const char *name;
} SymbolsEntry;
int Symbols_Add(const SymbolsEntry *entries, size_t count);

#endif
