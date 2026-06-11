#include "ast.h"

static const char *js_keywords[] = {
    "break", "case", "catch", "class", "const", "continue", "debugger", "default",
    "delete", "do", "else", "export", "extends", "finally", "for", "function",
    "if", "import", "in", "instanceof", "new", "return", "super", "switch",
    "this", "throw", "try", "typeof", "var", "void", "while", "with", "yield",
    "let", "static", "await", "async", "package", "private", "protected",
    "public", "interface", "type", "of", "from", "as", "get", "set"
};

static const char *js_types[] = {
    "true", "false", "null", "undefined", "NaN", "Infinity", "any", "string",
    "number", "boolean", "unknown", "never", "void", "Object", "Function",
    "Boolean", "Symbol", "Error", "Number", "BigInt", "Math", "Date", "String",
    "RegExp", "Array", "Map", "Set", "Promise", "JSON", "console", "window"
};

static const syntax_def_t js_syntax_def = {
    .extension = ".js",
    .keywords = js_keywords,
    .num_keywords = sizeof(js_keywords) / sizeof(js_keywords[0]),
    .types = js_types,
    .num_types = sizeof(js_types) / sizeof(js_types[0]),
    .line_comment = "//",
    .block_comment_start = "/*",
    .block_comment_end = "*/",
    .format_fn = NULL,
    .parse_fn = NULL
};

const syntax_def_t* get_js_syntax_def(void) {
    return &js_syntax_def;
}
