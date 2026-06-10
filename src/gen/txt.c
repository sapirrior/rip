#include "ast.h"

ast_node_t* parse_txt_to_ast(const char *input, size_t input_len) {
    return ast_create_node(AST_NODE_TEXT, input, input_len);
}
