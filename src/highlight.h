#ifndef YOTTA_HIGHLIGHT_H
#define YOTTA_HIGHLIGHT_H

#include <stdbool.h>

typedef enum {
    HL_NORMAL = 0,
    HL_KEYWORD,
    HL_TYPE,
    HL_BUILTIN,
    HL_COMMENT,
    HL_STRING,
    HL_NUMBER,
    HL_OPERATOR,
    HL_PREPROCESSOR,
    HL_FUNCTION,
    HL_VARIABLE,
    HL_CONSTANT,
    HL_PUNCTUATION,
    HL_DECORATOR,
    HL_KEY,      /* JSON key */
    HL_COUNT
} HlType;

typedef struct {
    int    start;  /* byte offset in line */
    int    len;    /* byte length */
    HlType type;
} HlToken;

/* Tokenize a line of code for language `lang`.
   Returns the number of tokens written into `out` (up to `max_tokens`). */
int  highlight_line(const char *lang, const char *text, int text_len,
                    HlToken *out, int max_tokens);

/* Colour / style for a token type */
const char *hl_token_color(HlType t);
bool        hl_token_bold(HlType t);
bool        hl_token_italic(HlType t);

/* Detect language from file extension */
const char *hl_detect_lang(const char *filepath);

#endif /* YOTTA_HIGHLIGHT_H */
