#ifndef MEMORIES_PC_FIELDS_H
#define MEMORIES_PC_FIELDS_H

/* The duel-field registry.  The disc owns ids 1-6; the byte which records a
 * field leaves 7-255 for data mods.  A field name is an identity, just like a
 * card type or attribute name, so tables and tower floors may use it rather
 * than an opaque number. */

struct JsonValue;

#define FIELDS_RETAIL 6
#define FIELDS_MAX 255

void Fields_Clear(void);
void Fields_Build(void);
void Fields_Add(const char *mod, const struct JsonValue *manifest);

int Fields_Value(const struct JsonValue *value);
int Fields_Find(const char *name);
int Fields_Declared(int field);
const char *Fields_Name(int field);
/* Whether Fields_Name(field) is a mod's own word rather than the retail
 * carrier's: always true for a declared added field, true for a replaced
 * retail field only when the mod gave it a "name" of its own. The duel
 * HUD's FIELD box (hd_text.c) shows this instead of the carrier's retail
 * word when it is. */
int Fields_HasCustomName(int field);
/* The absolute PNG path declared as "picture", or NULL when the field uses
 * its retail board/has no picture. */
const char *Fields_Picture(int field);
/* The logical mod field and the retail terrain package it borrows for art. */
void Fields_Activate(int field);
int Fields_Active(void);
int Fields_Carrier(int field);
/* The board renderer asks this while it emits a centre-floor polygon.  An
 * added field with a full board picture and `"mirror": false` opts out of
 * the retail model's mirrored centre strip. */
int Fields_UnmirrorCentre(void);
/* Applies that remap to one quad's four raw UV halfwords (u8 u, u8 v packed
 * low/high) in place, given each corner's model-space Z. A no-op unless
 * Fields_UnmirrorCentre and every corner's V lies in the retail centre
 * band. */
void Fields_UnmirrorQuadUV(unsigned short uv[4], const int z[4]);

#endif
