#include "ast.h"
#include <stdlib.h>
#include <string.h>

ast_node_t* ast_create_node(ast_node_type_t type, const char *start, size_t len) {
    ast_node_t *node = malloc(sizeof(ast_node_t));
    node->type = type;
    node->start = start;
    node->len = len;
    node->next = NULL;
    return node;
}

void ast_free_nodes(ast_node_t *head) {
    while (head) {
        ast_node_t *tmp = head->next;
        free(head);
        head = tmp;
    }
}

static void append_char(char **dest, size_t *di, size_t *cap, char c) {
    if (*di + 2 >= *cap) {
        *cap = *cap ? *cap * 2 : 1024;
        *dest = realloc(*dest, *cap);
    }
    (*dest)[(*di)++] = c;
}

static void append_str(char **dest, size_t *di, size_t *cap, const char *s) {
    size_t len = strlen(s);
    if (*di + len + 2 >= *cap) {
        while (*di + len + 2 >= *cap) {
            *cap = *cap ? *cap * 2 : 1024;
        }
        *dest = realloc(*dest, *cap);
    }
    memcpy(*dest + *di, s, len);
    *di += len;
}

char* ast_highlight_stream(ast_node_t *head, size_t *out_len) {
    size_t cap = 4096;
    char *dest = malloc(cap);
    size_t di = 0;

    ast_node_t *curr = head;
    while (curr) {
        if (curr->len > 0) {
            const char *esc = NULL;
            switch (curr->type) {
                case AST_NODE_KEYWORD:      esc = "\033[1;33m"; break;
                case AST_NODE_TYPE:         esc = "\033[1;36m"; break;
                case AST_NODE_STRING:       esc = "\033[31m";   break;
                case AST_NODE_COMMENT:      esc = "\033[32m";   break;
                case AST_NODE_NUMBER:       esc = "\033[35m";   break;
                case AST_NODE_PREPROCESSOR: esc = "\033[35m";   break;
                case AST_NODE_OPERATOR:     esc = "\033[33m";   break;
                case AST_NODE_ADDITION:     esc = "\033[32m";   break;
                case AST_NODE_DELETION:     esc = "\033[31m";   break;
                case AST_NODE_META:         esc = "\033[36m";   break;
                case AST_NODE_LOG_ERROR:    esc = "\033[31m";   break;
                case AST_NODE_LOG_WARN:     esc = "\033[33m";   break;
                case AST_NODE_LOG_INFO:     esc = "\033[32m";   break;
                case AST_NODE_LOG_DEBUG:    esc = "\033[34m";   break;
                default: break;
            }

            if (esc) {
                append_str(&dest, &di, &cap, esc);
            }
            for (size_t i = 0; i < curr->len; i++) {
                append_char(&dest, &di, &cap, curr->start[i]);
            }
            if (esc) {
                append_str(&dest, &di, &cap, "\033[0m");
            }
        }
        curr = curr->next;
    }
    dest[di] = '\0';
    *out_len = di;
    return dest;
}

char* ast_convert(const char *filename, const char *input, size_t input_len, size_t *out_len) {
    if (!filename) return NULL;

    const char *ext = strrchr(filename, '.');
    if (ext) {
        if (strcmp(ext, ".json") == 0) {
            return gen_json_format(input, input_len, out_len);
        } else if (strcmp(ext, ".csv") == 0) {
            return gen_csv_format(input, input_len, out_len, ',');
        } else if (strcmp(ext, ".tsv") == 0) {
            return gen_csv_format(input, input_len, out_len, '\t');
        } else if (strcmp(ext, ".xml") == 0 || strcmp(ext, ".html") == 0 || strcmp(ext, ".xhtml") == 0) {
            return gen_xml_format(input, input_len, out_len);
        } else if (strcmp(ext, ".diff") == 0 || strcmp(ext, ".patch") == 0) {
            ast_node_t *ast = parse_diff_to_ast(input, input_len);
            char *res = ast_highlight_stream(ast, out_len);
            ast_free_nodes(ast);
            return res;
        } else if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0 || strcmp(ext, ".cpp") == 0 || strcmp(ext, ".hpp") == 0) {
            ast_node_t *ast = parse_c_to_ast(input, input_len);
            char *res = ast_highlight_stream(ast, out_len);
            ast_free_nodes(ast);
            return res;
        } else if (strcmp(ext, ".log") == 0) {
            ast_node_t *ast = parse_log_to_ast(input, input_len);
            char *res = ast_highlight_stream(ast, out_len);
            ast_free_nodes(ast);
            return res;
        } else if (strcmp(ext, ".py") == 0) {
            ast_node_t *ast = parse_py_to_ast(input, input_len);
            char *res = ast_highlight_stream(ast, out_len);
            ast_free_nodes(ast);
            return res;
        }
    }

    size_t si = 0;
    while (si < input_len && (input[si] == ' ' || input[si] == '\t' || input[si] == '\n' || input[si] == '\r')) {
        si++;
    }
    if (si < input_len && (input[si] == '{' || input[si] == '[')) {
        return gen_json_format(input, input_len, out_len);
    }
    if (si < input_len && input[si] == '<') {
        return gen_xml_format(input, input_len, out_len);
    }

    ast_node_t *ast = parse_txt_to_ast(input, input_len);
    char *res = ast_highlight_stream(ast, out_len);
    ast_free_nodes(ast);
    return res;
}
