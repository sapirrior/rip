#include "ast.h"
#include <string.h>
#include <stdlib.h>

static void highlight_log(const char *input, size_t input_len, syntax_state_t *state, char **dest, size_t *di, size_t *cap) {
    (void)state;
    size_t start = 0;
    for (size_t si = 0; si <= input_len; si++) {
        if (si == input_len || input[si] == '\n' || input[si] == '\r') {
            size_t len = si - start;
            if (len > 0) {
                char *line_copy = malloc(len + 1);
                memcpy(line_copy, input + start, len);
                line_copy[len] = '\0';

                const char *color = NULL;
                if (strstr(line_copy, "ERROR") || strstr(line_copy, "CRITICAL")) {
                    color = "\033[38;2;255;123;114m"; // red
                } else if (strstr(line_copy, "WARN") || strstr(line_copy, "WARNING")) {
                    color = "\033[38;2;255;166;87m"; // orange
                } else if (strstr(line_copy, "INFO")) {
                    color = "\033[38;2;126;231;135m"; // green
                } else if (strstr(line_copy, "DEBUG")) {
                    color = "\033[38;2;121;192;255m"; // blue
                }
                free(line_copy);

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

static const syntax_def_t log_syntax_def = {
    .extension = ".log",
    .keywords = NULL,
    .num_keywords = 0,
    .types = NULL,
    .num_types = 0,
    .line_comment = NULL,
    .block_comment_start = NULL,
    .block_comment_end = NULL,
    .format_fn = NULL,
    .highlight_fn = highlight_log
};

const syntax_def_t* get_log_syntax_def(void) {
    return &log_syntax_def;
}
