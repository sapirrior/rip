#include "ast.h"

static const char *c_keywords[] = {
    "if", "else", "while", "for", "return", "switch", "case", "break", "continue",
    "struct", "union", "enum", "typedef", "sizeof", "static", "const", "extern",
    "volatile", "goto", "register", "inline", "restrict", "default"
};

static const char *c_types[] = {
    "int", "char", "void", "float", "double", "short", "long", "signed", "unsigned",
    "size_t", "ssize_t", "int8_t", "int16_t", "int32_t", "int64_t",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t", "bool", "FILE"
};

static const syntax_def_t c_syntax_def = {
    .extension = ".c",
    .keywords = c_keywords,
    .num_keywords = sizeof(c_keywords) / sizeof(c_keywords[0]),
    .types = c_types,
    .num_types = sizeof(c_types) / sizeof(c_types[0]),
    .line_comment = "//",
    .block_comment_start = "/*",
    .block_comment_end = "*/",
    .format_fn = NULL,
    .highlight_fn = NULL
};

const syntax_def_t* get_c_syntax_def(void) {
    return &c_syntax_def;
}
