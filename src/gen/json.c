#include "ast.h"
#include <stdlib.h>
#include <string.h>

static void append_char(char **dest, size_t *di, size_t *cap, char c) {
    if (*di + 2 >= *cap) {
        *cap = *cap ? *cap * 2 : 1024;
        *dest = realloc(*dest, *cap);
    }
    (*dest)[(*di)++] = c;
}

static char* gen_json_format(const char *input, size_t input_len, size_t *out_len) {
    size_t cap = input_len * 2 + 1024;
    char *dest = malloc(cap);
    size_t di = 0;
    int indent = 0;
    int in_string = 0;

    for (size_t si = 0; si < input_len; si++) {
        char c = input[si];

        if (in_string) {
            append_char(&dest, &di, &cap, c);
            if (c == '"' && input[si - 1] != '\\') {
                in_string = 0;
            }
            continue;
        }

        if (c == '"') {
            in_string = 1;
            append_char(&dest, &di, &cap, c);
            continue;
        }

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            continue;
        }

        if (c == '{' || c == '[') {
            append_char(&dest, &di, &cap, c);
            indent += 2;
            append_char(&dest, &di, &cap, '\n');
            for (int j = 0; j < indent; j++) append_char(&dest, &di, &cap, ' ');
            continue;
        }

        if (c == '}' || c == ']') {
            indent -= 2;
            if (indent < 0) indent = 0;
            append_char(&dest, &di, &cap, '\n');
            for (int j = 0; j < indent; j++) append_char(&dest, &di, &cap, ' ');
            append_char(&dest, &di, &cap, c);
            continue;
        }

        if (c == ',') {
            append_char(&dest, &di, &cap, c);
            append_char(&dest, &di, &cap, '\n');
            for (int j = 0; j < indent; j++) append_char(&dest, &di, &cap, ' ');
            continue;
        }

        if (c == ':') {
            append_char(&dest, &di, &cap, c);
            append_char(&dest, &di, &cap, ' ');
            continue;
        }

        append_char(&dest, &di, &cap, c);
    }
    dest[di] = '\0';
    *out_len = di;
    return dest;
}

static const syntax_def_t json_syntax_def = {
    .extension = ".json",
    .keywords = NULL,
    .num_keywords = 0,
    .types = NULL,
    .num_types = 0,
    .line_comment = NULL,
    .block_comment_start = NULL,
    .block_comment_end = NULL,
    .format_fn = gen_json_format,
    .parse_fn = NULL
};

const syntax_def_t* get_json_syntax_def(void) {
    return &json_syntax_def;
}
