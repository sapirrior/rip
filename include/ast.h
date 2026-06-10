#ifndef SOME_AST_H
#define SOME_AST_H

#include <stddef.h>

typedef enum {
    AST_NODE_TEXT,
    AST_NODE_KEYWORD,
    AST_NODE_TYPE,
    AST_NODE_STRING,
    AST_NODE_COMMENT,
    AST_NODE_NUMBER,
    AST_NODE_PREPROCESSOR,
    AST_NODE_IDENTIFIER,
    AST_NODE_OPERATOR,
    AST_NODE_ADDITION,      // for diff +
    AST_NODE_DELETION,      // for diff -
    AST_NODE_META,          // for diff @
    AST_NODE_LOG_ERROR,     // for logs
    AST_NODE_LOG_WARN,
    AST_NODE_LOG_INFO,
    AST_NODE_LOG_DEBUG
} ast_node_type_t;

typedef struct ast_node {
    ast_node_type_t type;
    const char *start;
    size_t len;
    struct ast_node *next;
} ast_node_t;

char* ast_convert(const char *filename, const char *input, size_t input_len, size_t *out_len);

// Node helpers
ast_node_t* ast_create_node(ast_node_type_t type, const char *start, size_t len);
void ast_free_nodes(ast_node_t *head);
char* ast_highlight_stream(ast_node_t *head, size_t *out_len);

// Parser functions returning AST/Token node list
ast_node_t* parse_c_to_ast(const char *input, size_t input_len);
ast_node_t* parse_py_to_ast(const char *input, size_t input_len);
ast_node_t* parse_log_to_ast(const char *input, size_t input_len);
ast_node_t* parse_diff_to_ast(const char *input, size_t input_len);
ast_node_t* parse_txt_to_ast(const char *input, size_t input_len);

// Formatters
char* gen_json_format(const char *input, size_t input_len, size_t *out_len);
char* gen_csv_format(const char *input, size_t input_len, size_t *out_len, char separator);
char* gen_xml_format(const char *input, size_t input_len, size_t *out_len);

#endif // SOME_AST_H
