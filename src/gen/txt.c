#include "ast.h"

static void highlight_txt(const char *input, size_t input_len, syntax_state_t *state, char **dest, size_t *di, size_t *cap) {
    (void)state;
    for (size_t i = 0; i < input_len; i++) {
        ast_append_char(dest, di, cap, input[i]);
    }
}

static const syntax_def_t txt_syntax_def = {
    .extension = ".txt",
    .keywords = NULL,
    .num_keywords = 0,
    .types = NULL,
    .num_types = 0,
    .line_comment = NULL,
    .block_comment_start = NULL,
    .block_comment_end = NULL,
    .format_fn = NULL,
    .highlight_fn = highlight_txt
};

const syntax_def_t* get_txt_syntax_def(void) {
    return &txt_syntax_def;
}
