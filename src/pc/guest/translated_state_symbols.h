#ifndef MEMORIES_TRANSLATED_STATE_SYMBOLS_H
#define MEMORIES_TRANSLATED_STATE_SYMBOLS_H
/* Carrying a translated game's save state to another build by the two
 * builds' symbol tables (translated_state_symbols.c). */
#include <stddef.h>
#include <stdint.h>
/* Where the game's image is loaded (dladdr), or 0. */
uintptr_t Memories_NativeImageBase(void);
/* The running build's table (symbols/<id>.txt), malloc'd, or NULL. */
char *Memories_NativeSymbolsText(size_t *length);
/* Map offsets of the build whose table is `saved` to this one's: 0 when
 * both tables are there and read. */
int Memories_NativeSymbolsPrepare(const void *saved, size_t saved_length, uint64_t old_image_size,
                                  uint64_t new_image_size);
int Memories_NativeSymbolsActive(void);
void Memories_NativeSymbolsClear(void);
/* An offset into the old image, in the new one: 0, or -1 when it cannot be
 * carried (no such symbol, or one that changed under it). */
int Memories_NativeSymbolsMap(uint64_t offset, uint64_t *mapped);
/* How many words of `data` (at every byte) point into the old image where
 * Memories_NativeSymbolsMap cannot carry them. */
size_t Memories_NativeSymbolsScan(const uint8_t *data, size_t size, uint64_t old_image, uint64_t image_size);
#endif
