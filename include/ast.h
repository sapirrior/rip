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
    AST_NODE_FUNCTION,      // for functions/classes
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

typedef struct {
    const char *extension;
    const char **keywords;
    size_t num_keywords;
    const char **types;
    size_t num_types;
    const char *line_comment;
    const char *block_comment_start;
    const char *block_comment_end;
    
    char* (*format_fn)(const char *input, size_t input_len, size_t *out_len);
    ast_node_t* (*parse_fn)(const char *input, size_t input_len);
} syntax_def_t;

char* ast_convert(const char *filename, const char *input, size_t input_len, size_t *out_len, int enable_colors);

// Node helpers
ast_node_t* ast_create_node(ast_node_type_t type, const char *start, size_t len);
void ast_free_nodes(ast_node_t *head);
char* ast_highlight_stream(ast_node_t *head, size_t *out_len);

// Syntax definitions declarations
const syntax_def_t* get_c_syntax_def(void);
const syntax_def_t* get_py_syntax_def(void);
const syntax_def_t* get_json_syntax_def(void);
const syntax_def_t* get_csv_syntax_def(void);
const syntax_def_t* get_tsv_syntax_def(void);
const syntax_def_t* get_xml_syntax_def(void);
const syntax_def_t* get_diff_syntax_def(void);
const syntax_def_t* get_log_syntax_def(void);
const syntax_def_t* get_txt_syntax_def(void);

#endif // SOME_AST_H
