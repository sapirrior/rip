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

static char* gen_xml_format(const char *input, size_t input_len, size_t *out_len) {
    size_t cap = input_len * 2 + 1024;
    char *dest = malloc(cap);
    size_t di = 0;
    int indent = 0;

    for (size_t si = 0; si < input_len; si++) {
        if (input[si] == ' ' || input[si] == '\t' || input[si] == '\n' || input[si] == '\r') {
            continue;
        }

        if (input[si] == '<') {
            int is_closing = (si + 1 < input_len && input[si + 1] == '/');
            int is_inline_self_closing = 0;

            size_t tag_end = si;
            while (tag_end < input_len && input[tag_end] != '>') {
                tag_end++;
            }
            if (tag_end < input_len && input[tag_end - 1] == '/') {
                is_inline_self_closing = 1;
            }

            if (is_closing) {
                indent -= 2;
                if (indent < 0) indent = 0;
            }

            if (di > 0 && dest[di - 1] != '\n') {
                append_char(&dest, &di, &cap, '\n');
            }
            for (int j = 0; j < indent; j++) append_char(&dest, &di, &cap, ' ');

            while (si <= tag_end && si < input_len) {
                append_char(&dest, &di, &cap, input[si++]);
            }
            si--;

            if (!is_closing && !is_inline_self_closing) {
                indent += 2;
            }
            continue;
        }

        append_char(&dest, &di, &cap, input[si]);
    }
    dest[di] = '\0';
    *out_len = di;
    return dest;
}

static const syntax_def_t xml_syntax_def = {
    .extension = ".xml",
    .keywords = NULL,
    .num_keywords = 0,
    .types = NULL,
    .num_types = 0,
    .line_comment = NULL,
    .block_comment_start = NULL,
    .block_comment_end = NULL,
    .format_fn = gen_xml_format,
    .parse_fn = NULL
};

const syntax_def_t* get_xml_syntax_def(void) {
    return &xml_syntax_def;
}
