/* Game text in the other languages of the European ROM.
 *
 * The port runs the US code, whose strings are English (in the binary and
 * in the data blocks). The European ROM carries five languages: most
 * sentences lie behind the English one as a group (EN, DE, FR, IT, ES),
 * names and descriptions lie in parallel pointer tables. Port_Text() maps
 * an English string to its counterpart by content: a one-time index of the
 * ROM's strings and string pointers, built lazily. */
#ifndef PORT_TEXT_H
#define PORT_TEXT_H

enum {
    PORT_LANG_UNSET = -1,
    PORT_LANG_EN = 0,
    PORT_LANG_DE = 1,
    PORT_LANG_FR = 2,
    PORT_LANG_IT = 3,
    PORT_LANG_ES = 4,
    PORT_LANG_COUNT = 5
};

/* 1 when the loaded ROM has several languages (European version). */
int Port_TextMultilingual(void);

/* The string to use in place of s (s itself when there is no translation). */
const unsigned char *Port_Text(const unsigned char *s);

/* Descriptions by index (the European tables by ID): the text of the
 * language for entry id of kind, s itself for English or when unknown. */
enum {
    PORT_TEXT_ITEM_DESC = 0, /* GetItemDescription */
    PORT_TEXT_MOVE_DESC = 1, /* MoveDataEntry.description */
    PORT_TEXT_ID_KINDS = 2
};
#define PORT_TEXT_ITEM_COUNT 240 /* NUMBER_OF_ITEM_IDS */
#define PORT_TEXT_MOVE_COUNT 413 /* NUM_MOVE_IDS */
const unsigned char *Port_TextById(int kind, int id, const unsigned char *s);

/* The floor in the European style of the language (0 for English): the
 * text before the number and the sign that goes with it ("U" "", "E. " "-"). */
int Port_FloorStyle(int up, const char **prefix, const char **sign);

/* Statistics for the log. */
void Port_TextLogStats(void);

#endif
