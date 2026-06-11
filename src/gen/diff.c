#include "ast.h"
#include <string.h>

static void highlight_diff(const char *input, size_t input_len, syntax_state_t *state, char **dest, size_t *di, size_t *cap) {
    (void)state;
    size_t start = 0;
    for (size_t si = 0; si <= input_len; si++) {
        if (si == input_len || input[si] == '\n' || input[si] == '\r') {
            size_t len = si - start;
            if (len > 0) {
                const char *color = NULL;
                if (input[start] == '+') {
                    color = "\033[38;2;126;231;135m"; // green
                } else if (input[start] == '-') {
                    color = "\033[38;2;255;123;114m"; // red
                } else if (input[start] == '@') {
                    color = "\033[38;2;210;168;255m"; // purple
                }

                if (color) ast_append_str(dest, di, cap, color);
                for (size_t i = 0; i < len; i++) {
                    ast_append_char(dest, di, cap, input[start + i]);
                }
                if (color) ast_append_str(dest, di, cap, "\033[0m");
            }
            if (si < input_len) {
                size_t nl_len = 1;
                if (input[si] == '\r' && si + 1 < input_len && input[si + 1] == '\n') {
                    nl_len = 2;
                }
                for (size_t i = 0; i < nl_len; i++) {
                    ast_append_char(dest, di, cap, input[si + i]);
                }
                si += nl_len - 1;
            }
            start = si + 1;
        }
    }
}

static const syntax_def_t diff_syntax_def = {
    .extension = ".diff",
    .keywords = NULL,
    .num_keywords = 0,
    .types = NULL,
    .num_types = 0,
    .line_comment = NULL,
    .block_comment_start = NULL,
    .block_comment_end = NULL,
    .format_fn = NULL,
    .highlight_fn = highlight_diff
};

const syntax_def_t* get_diff_syntax_def(void) {
    return &diff_syntax_def;
}
