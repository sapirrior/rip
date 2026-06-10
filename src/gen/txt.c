#include "ast.h"

static ast_node_t* parse_txt_to_ast(const char *input, size_t input_len) {
    return ast_create_node(AST_NODE_TEXT, input, input_len);
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
    .parse_fn = parse_txt_to_ast
};

const syntax_def_t* get_txt_syntax_def(void) {
    return &txt_syntax_def;
}
