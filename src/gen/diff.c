#include "ast.h"
#include <string.h>

static ast_node_t* parse_diff_to_ast(const char *input, size_t input_len) {
    ast_node_t *head = NULL, *tail = NULL;
    size_t start = 0;

    for (size_t si = 0; si <= input_len; si++) {
        if (si == input_len || input[si] == '\n' || input[si] == '\r') {
            size_t len = si - start;
            if (len > 0) {
                ast_node_type_t type = AST_NODE_TEXT;
                if (input[start] == '+') {
                    type = AST_NODE_ADDITION;
                } else if (input[start] == '-') {
                    type = AST_NODE_DELETION;
                } else if (input[start] == '@') {
                    type = AST_NODE_META;
                }
                ast_node_t *node = ast_create_node(type, input + start, len);
                if (!head) {
                    head = node;
                } else {
                    tail->next = node;
                }
                tail = node;
            }
            if (si < input_len) {
                size_t nl_len = 1;
                if (input[si] == '\r' && si + 1 < input_len && input[si + 1] == '\n') {
                    nl_len = 2;
                }
                ast_node_t *node = ast_create_node(AST_NODE_TEXT, input + si, nl_len);
                if (!head) {
                    head = node;
                } else {
                    tail->next = node;
                }
                tail = node;
                si += nl_len - 1;
            }
            start = si + 1;
        }
    }
    return head;
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
    .parse_fn = parse_diff_to_ast
};

const syntax_def_t* get_diff_syntax_def(void) {
    return &diff_syntax_def;
}
