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

/* Statistics for the log. */
void Port_TextLogStats(void);

#endif
