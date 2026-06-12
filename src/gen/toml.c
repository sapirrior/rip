#include "ast.h"

static const char *toml_keywords[] = {
    "true", "false"
};

static const syntax_def_t toml_syntax_def = {
    .extension = ".toml",
    .keywords = toml_keywords,
    .num_keywords = sizeof(toml_keywords) / sizeof(toml_keywords[0]),
    .types = NULL,
    .num_types = 0,
    .line_comment = "#",
    .block_comment_start = NULL,
    .block_comment_end = NULL,
    .format_fn = NULL,
    .highlight_fn = NULL
};

const syntax_def_t* get_toml_syntax_def(void) {
    return &toml_syntax_def;
}
