#include "ast.h"

static const char *py_keywords[] = {
    "def", "class", "import", "from", "if", "elif", "else", "for", "while",
    "return", "print", "as", "in", "try", "except", "with", "self", "True",
    "False", "None", "pass", "and", "or", "not", "lambda", "global", "nonlocal",
    "assert", "break", "continue", "del", "finally", "is", "raise", "yield"
};

static const syntax_def_t py_syntax_def = {
    .extension = ".py",
    .keywords = py_keywords,
    .num_keywords = sizeof(py_keywords) / sizeof(py_keywords[0]),
    .types = NULL,
    .num_types = 0,
    .line_comment = "#",
    .block_comment_start = NULL,
    .block_comment_end = NULL,
    .format_fn = NULL,
    .highlight_fn = NULL
};

const syntax_def_t* get_py_syntax_def(void) {
    return &py_syntax_def;
}
