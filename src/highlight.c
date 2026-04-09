#include "highlight.h"
#include "types.h"

#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* ── Language keyword tables ── */

static const char *c_keywords[] = {
    "auto","break","case","const","continue","default","do","else","enum",
    "extern","for","goto","if","inline","register","restrict","return",
    "sizeof","static","struct","switch","typedef","union","volatile","while",
    "_Bool","_Complex","_Imaginary","_Alignas","_Alignof","_Atomic",
    "_Generic","_Noreturn","_Static_assert","_Thread_local",NULL
};
static const char *c_types[] = {
    "char","double","float","int","long","short","signed","unsigned","void",
    "int8_t","int16_t","int32_t","int64_t",
    "uint8_t","uint16_t","uint32_t","uint64_t",
    "size_t","ssize_t","ptrdiff_t","intptr_t","uintptr_t",
    "bool","true","false","NULL",
    "FILE","pid_t","off_t","time_t","wchar_t",NULL
};
static const char *c_builtins[] = {
    "printf","fprintf","sprintf","snprintf","scanf","sscanf",
    "malloc","calloc","realloc","free","memcpy","memmove","memset","memcmp",
    "strlen","strcpy","strncpy","strcmp","strncmp","strcat","strchr","strstr",
    "fopen","fclose","fread","fwrite","fgets","fputs","puts","putchar","getchar",
    "exit","abort","assert","perror","strerror",NULL
};

static const char *py_keywords[] = {
    "False","None","True","and","as","assert","async","await",
    "break","class","continue","def","del","elif","else","except",
    "finally","for","from","global","if","import","in","is","lambda",
    "nonlocal","not","or","pass","raise","return","try","while","with","yield",NULL
};
static const char *py_builtins[] = {
    "abs","all","any","bin","bool","breakpoint","bytearray","bytes",
    "callable","chr","compile","complex","delattr","dict","dir","divmod",
    "enumerate","eval","exec","filter","float","format","frozenset","getattr",
    "globals","hasattr","hash","help","hex","id","input","int","isinstance",
    "issubclass","iter","len","list","locals","map","max","memoryview","min",
    "next","object","oct","open","ord","pow","print","property","range","repr",
    "reversed","round","set","setattr","slice","sorted","staticmethod","str",
    "sum","super","tuple","type","vars","zip",NULL
};

static const char *bash_keywords[] = {
    "if","then","else","elif","fi","case","esac","for","select","while","until",
    "do","done","in","function","time","coproc","!","[[","]]",NULL
};
static const char *bash_builtins[] = {
    "echo","cd","ls","mkdir","rm","cp","mv","cat","grep","sed","awk","find",
    "export","source","alias","unset","local","read","return","exit","eval",
    "exec","set","shift","getopts","test","true","false","declare","typeset",
    "printf","let","((","))",NULL
};

/* ── Helpers ── */

static bool is_word_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

static bool word_in_list(const char *word, int wlen, const char **list) {
    for (int i = 0; list[i]; i++) {
        if ((int)strlen(list[i]) == wlen &&
            memcmp(list[i], word, wlen) == 0) return true;
    }
    return false;
}

/* Push a token if there is room */
static int push_tok(HlToken *out, int nt, int max,
                    int start, int len, HlType type) {
    if (nt >= max || len <= 0) return nt;
    out[nt].start = start;
    out[nt].len   = len;
    out[nt].type  = type;
    return nt + 1;
}

/* ── C / C++ highlighter ── */
static int hl_c(const char *text, int tlen, HlToken *out, int max) {
    int nt = 0;
    int i  = 0;

    /* Preprocessor line */
    if (tlen > 0 && text[0] == '#') {
        nt = push_tok(out, nt, max, 0, tlen, HL_PREPROCESSOR);
        return nt;
    }

    while (i < tlen && nt < max) {
        char c = text[i];

        /* Line comment */
        if (c == '/' && i + 1 < tlen && text[i+1] == '/') {
            nt = push_tok(out, nt, max, i, tlen - i, HL_COMMENT);
            break;
        }
        /* Block comment */
        if (c == '/' && i + 1 < tlen && text[i+1] == '*') {
            int j = i + 2;
            while (j + 1 < tlen && !(text[j] == '*' && text[j+1] == '/')) j++;
            if (j + 1 < tlen) j += 2;
            nt = push_tok(out, nt, max, i, j - i, HL_COMMENT);
            i = j; continue;
        }

        /* String */
        if (c == '"' || c == '\'') {
            char delim = c;
            int j = i + 1;
            while (j < tlen && text[j] != delim) {
                if (text[j] == '\\') j++;
                j++;
            }
            if (j < tlen) j++;
            nt = push_tok(out, nt, max, i, j - i, HL_STRING);
            i = j; continue;
        }

        /* Number */
        if (isdigit((unsigned char)c) ||
            (c == '.' && i+1 < tlen && isdigit((unsigned char)text[i+1]))) {
            int j = i;
            while (j < tlen && (isalnum((unsigned char)text[j]) || text[j] == '.')) j++;
            nt = push_tok(out, nt, max, i, j - i, HL_NUMBER);
            i = j; continue;
        }

        /* Identifier / keyword */
        if (isalpha((unsigned char)c) || c == '_') {
            int j = i;
            while (j < tlen && is_word_char(text[j])) j++;
            int wlen = j - i;
            HlType t = HL_NORMAL;
            if (word_in_list(text + i, wlen, c_keywords)) t = HL_KEYWORD;
            else if (word_in_list(text + i, wlen, c_types))    t = HL_TYPE;
            else if (word_in_list(text + i, wlen, c_builtins)) t = HL_BUILTIN;
            /* function call: followed by '(' */
            else if (j < tlen && text[j] == '(') t = HL_FUNCTION;
            nt = push_tok(out, nt, max, i, wlen, t);
            i = j; continue;
        }

        /* Operator / punctuation */
        if (strchr("{}[]()=!<>&|+-*/%^~.,;:", c)) {
            nt = push_tok(out, nt, max, i, 1, HL_OPERATOR);
        } else {
            nt = push_tok(out, nt, max, i, 1, HL_NORMAL);
        }
        i++;
    }
    return nt;
}

/* ── Python highlighter ── */
static int hl_python(const char *text, int tlen, HlToken *out, int max) {
    int nt = 0, i = 0;

    while (i < tlen && nt < max) {
        char c = text[i];

        /* Comment */
        if (c == '#') {
            nt = push_tok(out, nt, max, i, tlen - i, HL_COMMENT);
            break;
        }
        /* Triple-quoted string (start of) */
        if ((c == '"' || c == '\'') &&
            i + 2 < tlen && text[i+1] == c && text[i+2] == c) {
            char delim = c;
            int j = i + 3;
            while (j + 2 < tlen &&
                   !(text[j] == delim && text[j+1] == delim && text[j+2] == delim))
                j++;
            if (j + 2 < tlen) j += 3;
            else j = tlen;
            nt = push_tok(out, nt, max, i, j - i, HL_STRING);
            i = j; continue;
        }
        /* String */
        if (c == '"' || c == '\'') {
            char delim = c;
            int j = i + 1;
            while (j < tlen && text[j] != delim) {
                if (text[j] == '\\') j++;
                j++;
            }
            if (j < tlen) j++;
            nt = push_tok(out, nt, max, i, j - i, HL_STRING);
            i = j; continue;
        }
        /* Decorator */
        if (c == '@') {
            int j = i;
            while (j < tlen && (is_word_char(text[j]) || text[j] == '@' || text[j] == '.')) j++;
            nt = push_tok(out, nt, max, i, j - i, HL_DECORATOR);
            i = j; continue;
        }
        /* Number */
        if (isdigit((unsigned char)c) ||
            (c == '.' && i+1 < tlen && isdigit((unsigned char)text[i+1]))) {
            int j = i;
            while (j < tlen && (isalnum((unsigned char)text[j]) || text[j] == '.' || text[j] == '_')) j++;
            nt = push_tok(out, nt, max, i, j - i, HL_NUMBER);
            i = j; continue;
        }
        /* Identifier / keyword */
        if (isalpha((unsigned char)c) || c == '_') {
            int j = i;
            while (j < tlen && is_word_char(text[j])) j++;
            int wlen = j - i;
            HlType t = HL_NORMAL;
            if (word_in_list(text + i, wlen, py_keywords)) t = HL_KEYWORD;
            else if (word_in_list(text + i, wlen, py_builtins)) t = HL_BUILTIN;
            else if (j < tlen && text[j] == '(') t = HL_FUNCTION;
            nt = push_tok(out, nt, max, i, wlen, t);
            i = j; continue;
        }
        if (strchr("{}[]()=!<>&|+-*/%^~.,;:", c))
            nt = push_tok(out, nt, max, i, 1, HL_OPERATOR);
        else
            nt = push_tok(out, nt, max, i, 1, HL_NORMAL);
        i++;
    }
    return nt;
}

/* ── JSON highlighter ── */
static int hl_json(const char *text, int tlen, HlToken *out, int max) {
    int nt = 0, i = 0;
    bool expect_key = false;

    /* Detect if first non-space char is '{' or ',' => next string is a key */
    for (int k = 0; k < tlen; k++) {
        if (text[k] == '{' || text[k] == ',') { expect_key = true; break; }
        if (!isspace((unsigned char)text[k])) break;
    }

    while (i < tlen && nt < max) {
        char c = text[i];
        if (c == '"') {
            int j = i + 1;
            while (j < tlen && text[j] != '"') {
                if (text[j] == '\\') j++;
                j++;
            }
            if (j < tlen) j++;
            nt = push_tok(out, nt, max, i, j - i, expect_key ? HL_KEY : HL_STRING);
            i = j;
            expect_key = false;
            continue;
        }
        if (c == ':') {
            nt = push_tok(out, nt, max, i, 1, HL_OPERATOR);
            i++; continue;
        }
        if (c == ',' || c == '{' || c == '}' || c == '[' || c == ']') {
            nt = push_tok(out, nt, max, i, 1, HL_PUNCTUATION);
            if (c == ',') expect_key = true;
            i++; continue;
        }
        if (isdigit((unsigned char)c) || c == '-') {
            int j = i;
            while (j < tlen && (isdigit((unsigned char)text[j]) ||
                   text[j] == '.' || text[j] == 'e' || text[j] == 'E' ||
                   text[j] == '+' || text[j] == '-')) j++;
            nt = push_tok(out, nt, max, i, j - i, HL_NUMBER);
            i = j; continue;
        }
        /* true / false / null */
        if (isalpha((unsigned char)c)) {
            int j = i;
            while (j < tlen && isalpha((unsigned char)text[j])) j++;
            nt = push_tok(out, nt, max, i, j - i, HL_CONSTANT);
            i = j; continue;
        }
        nt = push_tok(out, nt, max, i, 1, HL_NORMAL);
        i++;
    }
    return nt;
}

/* ── Bash highlighter ── */
static int hl_bash(const char *text, int tlen, HlToken *out, int max) {
    int nt = 0, i = 0;

    /* Shebang */
    if (tlen >= 2 && text[0] == '#' && text[1] == '!') {
        return push_tok(out, nt, max, 0, tlen, HL_PREPROCESSOR);
    }
    /* Comment */
    while (i < tlen && isspace((unsigned char)text[i])) i++;
    if (i < tlen && text[i] == '#') {
        return push_tok(out, nt, max, i, tlen - i, HL_COMMENT);
    }
    i = 0;

    while (i < tlen && nt < max) {
        char c = text[i];
        if (c == '#') {
            nt = push_tok(out, nt, max, i, tlen - i, HL_COMMENT);
            break;
        }
        if (c == '"' || c == '\'') {
            char delim = c;
            int j = i + 1;
            while (j < tlen && text[j] != delim) {
                if (text[j] == '\\') j++;
                j++;
            }
            if (j < tlen) j++;
            nt = push_tok(out, nt, max, i, j - i, HL_STRING);
            i = j; continue;
        }
        if (c == '$') {
            int j = i + 1;
            if (j < tlen && text[j] == '{') {
                while (j < tlen && text[j] != '}') j++;
                if (j < tlen) j++;
            } else {
                while (j < tlen && is_word_char(text[j])) j++;
            }
            nt = push_tok(out, nt, max, i, j - i, HL_VARIABLE);
            i = j; continue;
        }
        if (isdigit((unsigned char)c)) {
            int j = i;
            while (j < tlen && isdigit((unsigned char)text[j])) j++;
            nt = push_tok(out, nt, max, i, j - i, HL_NUMBER);
            i = j; continue;
        }
        if (isalpha((unsigned char)c) || c == '_') {
            int j = i;
            while (j < tlen && (is_word_char(text[j]) || text[j] == '-')) j++;
            int wlen = j - i;
            HlType t = HL_NORMAL;
            if (word_in_list(text + i, wlen, bash_keywords)) t = HL_KEYWORD;
            else if (word_in_list(text + i, wlen, bash_builtins)) t = HL_BUILTIN;
            else if (j < tlen && text[j] == '(') t = HL_FUNCTION;
            nt = push_tok(out, nt, max, i, wlen, t);
            i = j; continue;
        }
        nt = push_tok(out, nt, max, i, 1, HL_NORMAL);
        i++;
    }
    return nt;
}

/* ── Dispatch ── */
int highlight_line(const char *lang, const char *text, int tlen,
                   HlToken *out, int max) {
    if (!text || tlen <= 0) {
        out[0].start = 0; out[0].len = 0; out[0].type = HL_NORMAL;
        return 0;
    }
    /* Fallback: one big normal token */
    if (!lang || !lang[0]) {
        out[0].start = 0; out[0].len = tlen; out[0].type = HL_NORMAL;
        return 1;
    }
    if (strcmp(lang, "c") == 0 || strcmp(lang, "cpp") == 0 ||
        strcmp(lang, "h") == 0)
        return hl_c(text, tlen, out, max);
    if (strcmp(lang, "python") == 0 || strcmp(lang, "py") == 0)
        return hl_python(text, tlen, out, max);
    if (strcmp(lang, "json") == 0)
        return hl_json(text, tlen, out, max);
    if (strcmp(lang, "bash") == 0 || strcmp(lang, "sh") == 0)
        return hl_bash(text, tlen, out, max);
    /* Unknown language */
    out[0].start = 0; out[0].len = tlen; out[0].type = HL_NORMAL;
    return 1;
}

/* ── Color / style ── */
const char *hl_token_color(HlType t) {
    switch (t) {
        case HL_KEYWORD:     return COL_FG_MAGENTA;
        case HL_TYPE:        return COL_FG_CYAN;
        case HL_BUILTIN:     return COL_FG_CYAN;
        case HL_COMMENT:     return COL_FG_DIM;
        case HL_STRING:      return COL_FG_GREEN;
        case HL_NUMBER:      return COL_FG_ORANGE;
        case HL_OPERATOR:    return COL_FG;
        case HL_PREPROCESSOR:return COL_FG_ACCENT;
        case HL_FUNCTION:    return COL_FG_ACCENT;
        case HL_VARIABLE:    return COL_FG_YELLOW;
        case HL_CONSTANT:    return COL_FG_ORANGE;
        case HL_PUNCTUATION: return COL_FG_DIM;
        case HL_DECORATOR:   return COL_FG_YELLOW;
        case HL_KEY:         return COL_FG_ACCENT;
        default:             return COL_FG;
    }
}

bool hl_token_bold(HlType t) {
    return (t == HL_KEYWORD || t == HL_TYPE || t == HL_PREPROCESSOR);
}

bool hl_token_italic(HlType t) {
    return (t == HL_COMMENT);
}

/* ── Language detection ── */
const char *hl_detect_lang(const char *filepath) {
    if (!filepath) return "";
    const char *dot = strrchr(filepath, '.');
    if (!dot) {
        /* Check basename for shebang-named files */
        const char *base = strrchr(filepath, '/');
        base = base ? base + 1 : filepath;
        if (strcmp(base, "Makefile") == 0) return "bash";
        return "";
    }
    dot++;
    if (strcmp(dot, "c") == 0)   return "c";
    if (strcmp(dot, "h") == 0)   return "c";
    if (strcmp(dot, "cpp") == 0 || strcmp(dot, "cc") == 0 ||
        strcmp(dot, "cxx") == 0) return "cpp";
    if (strcmp(dot, "py") == 0)  return "python";
    if (strcmp(dot, "json") == 0)return "json";
    if (strcmp(dot, "sh") == 0 || strcmp(dot, "bash") == 0 ||
        strcmp(dot, "zsh") == 0) return "bash";
    if (strcmp(dot, "js") == 0 || strcmp(dot, "ts") == 0) return "bash"; /* stub */
    if (strcmp(dot, "md") == 0)  return "";
    return "";
}
