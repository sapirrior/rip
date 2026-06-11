#include "ast.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define AST_POOL_BLOCK_SIZE 4096

typedef struct ast_pool_block {
    struct ast_pool_block *next;
    size_t used;
    ast_node_t nodes[AST_POOL_BLOCK_SIZE];
} ast_pool_block_t;

static ast_pool_block_t *g_pool_head = NULL;
static ast_pool_block_t *g_pool_current = NULL;
static int g_pool_active = 0;

ast_node_t* ast_create_node(ast_node_type_t type, const char *start, size_t len) {
    if (g_pool_active) {
        if (!g_pool_current || g_pool_current->used >= AST_POOL_BLOCK_SIZE) {
            ast_pool_block_t *block = malloc(sizeof(ast_pool_block_t));
            block->next = NULL;
            block->used = 0;
            if (!g_pool_head) {
                g_pool_head = block;
            } else {
                g_pool_current->next = block;
            }
            g_pool_current = block;
        }
        ast_node_t *node = &g_pool_current->nodes[g_pool_current->used++];
        node->type = type;
        node->start = start;
        node->len = len;
        node->next = NULL;
        return node;
    }
    ast_node_t *node = malloc(sizeof(ast_node_t));
    node->type = type;
    node->start = start;
    node->len = len;
    node->next = NULL;
    return node;
}

void ast_free_nodes(ast_node_t *head) {
    if (g_pool_active) {
        // Pool will be freed in one go at the end of ast_convert
        return;
    }
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
                case AST_NODE_KEYWORD:      esc = "\033[38;2;255;123;114m"; break; // #ff7b72
                case AST_NODE_TYPE:         esc = "\033[38;2;255;123;114m"; break; // #ff7b72
                case AST_NODE_STRING:       esc = "\033[38;2;165;214;255m"; break; // #a5d6ff
                case AST_NODE_COMMENT:      esc = "\033[38;2;139;148;158m"; break; // #8b949e
                case AST_NODE_NUMBER:       esc = "\033[38;2;121;192;255m"; break; // #79c0ff
                case AST_NODE_PREPROCESSOR: esc = "\033[38;2;255;123;114m"; break; // #ff7b72
                case AST_NODE_FUNCTION:     esc = "\033[38;2;210;168;255m"; break; // #d2a8ff
                case AST_NODE_ADDITION:     esc = "\033[38;2;126;231;135m"; break; // #7ee787
                case AST_NODE_DELETION:     esc = "\033[38;2;255;123;114m"; break; // #ff7b72
                case AST_NODE_META:         esc = "\033[38;2;210;168;255m"; break; // #d2a8ff
                case AST_NODE_LOG_ERROR:    esc = "\033[38;2;255;123;114m"; break; // #ff7b72
                case AST_NODE_LOG_WARN:     esc = "\033[38;2;255;166;87m";  break; // #ffa657
                case AST_NODE_LOG_INFO:     esc = "\033[38;2;126;231;135m"; break; // #7ee787
                case AST_NODE_LOG_DEBUG:    esc = "\033[38;2;121;192;255m"; break; // #79c0ff
                default: break;
            }

            if (esc) {
                append_str(&dest, &di, &cap, esc);
            }
            for (size_t i = 0; i < curr->len; i++) {
                char c = curr->start[i];
                if (c == '\n' || c == '\r') {
                    if (esc) {
                        append_str(&dest, &di, &cap, "\033[0m");
                    }
                    append_char(&dest, &di, &cap, c);
                    if (c == '\r' && i + 1 < curr->len && curr->start[i+1] == '\n') {
                        append_char(&dest, &di, &cap, '\n');
                        i++;
                    }
                    if (esc) {
                        append_str(&dest, &di, &cap, esc);
                    }
                } else {
                    append_char(&dest, &di, &cap, c);
                }
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

static int is_word_in_list(const char *s, size_t len, const char **list, size_t count) {
    if (!list) return 0;
    for (size_t i = 0; i < count; i++) {
        if (strlen(list[i]) == len && strncmp(list[i], s, len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int is_all_caps(const char *s, size_t len) {
    if (len == 0) return 0;
    int has_letter = 0;
    for (size_t i = 0; i < len; i++) {
        if (isalpha((unsigned char)s[i])) {
            has_letter = 1;
            if (islower((unsigned char)s[i])) return 0;
        } else if (!isdigit((unsigned char)s[i]) && s[i] != '_') {
            return 0;
        }
    }
    return has_letter;
}

static ast_node_t* generic_lexer_to_ast(const char *input, size_t input_len, const syntax_def_t *def) {
    ast_node_t *head = NULL, *tail = NULL;
    size_t i = 0;

    int last_was_def_or_class = 0;
    int last_was_struct_or_union = 0;

    while (i < input_len) {
        // 1. Whitespace
        if (isspace((unsigned char)input[i])) {
            size_t ws_start = i;
            while (i < input_len && isspace((unsigned char)input[i])) {
                i++;
            }
            ast_node_t *node = ast_create_node(AST_NODE_TEXT, input + ws_start, i - ws_start);
            if (!head) head = node; else tail->next = node;
            tail = node;
            continue;
        }

        // 2. Single-line comment
        if (def->line_comment) {
            size_t lc_len = strlen(def->line_comment);
            if (i + lc_len <= input_len && strncmp(input + i, def->line_comment, lc_len) == 0) {
                size_t comment_start = i;
                while (i < input_len && input[i] != '\n' && input[i] != '\r') {
                    i++;
                }
                ast_node_t *node = ast_create_node(AST_NODE_COMMENT, input + comment_start, i - comment_start);
                if (!head) head = node; else tail->next = node;
                tail = node;
                continue;
            }
        }

        // 3. Multi-line block comment
        if (def->block_comment_start && def->block_comment_end) {
            size_t bcs_len = strlen(def->block_comment_start);
            size_t bce_len = strlen(def->block_comment_end);
            if (i + bcs_len <= input_len && strncmp(input + i, def->block_comment_start, bcs_len) == 0) {
                size_t comment_start = i;
                i += bcs_len;
                while (i < input_len) {
                    if (i + bce_len <= input_len && strncmp(input + i, def->block_comment_end, bce_len) == 0) {
                        i += bce_len;
                        break;
                    }
                    i++;
                }
                ast_node_t *node = ast_create_node(AST_NODE_COMMENT, input + comment_start, i - comment_start);
                if (!head) head = node; else tail->next = node;
                tail = node;
                continue;
            }
        }

        // 4. Preprocessor directive (if extension is C-based)
        if (input[i] == '#' && def->extension && (strcmp(def->extension, ".c") == 0 || strcmp(def->extension, ".cpp") == 0 || strcmp(def->extension, ".h") == 0 || strcmp(def->extension, ".hpp") == 0)) {
            size_t prep_start = i;
            while (i < input_len && input[i] != '\n' && input[i] != '\r') {
                if (input[i] == '\\' && i + 1 < input_len && (input[i+1] == '\n' || input[i+1] == '\r')) {
                    i += 2;
                    if (i < input_len && input[i-1] == '\r' && input[i] == '\n') i++;
                } else {
                    i++;
                }
            }
            ast_node_t *node = ast_create_node(AST_NODE_PREPROCESSOR, input + prep_start, i - prep_start);
            if (!head) head = node; else tail->next = node;
            tail = node;
            continue;
        }

        // 5. Python Decorators
        if (input[i] == '@' && def->extension && strcmp(def->extension, ".py") == 0) {
            size_t dec_start = i;
            i++;
            while (i < input_len && (isalnum((unsigned char)input[i]) || input[i] == '_' || input[i] == '.')) {
                i++;
            }
            ast_node_t *node = ast_create_node(AST_NODE_FUNCTION, input + dec_start, i - dec_start);
            if (!head) head = node; else tail->next = node;
            tail = node;
            continue;
        }

        // 6. Strings (including Python triple quotes)
        if (input[i] == '"' || input[i] == '\'') {
            char quote = input[i];
            size_t str_start = i;
            if (def->extension && strcmp(def->extension, ".py") == 0 && i + 2 < input_len && input[i+1] == quote && input[i+2] == quote) {
                i += 3;
                while (i + 2 < input_len) {
                    if (input[i] == quote && input[i+1] == quote && input[i+2] == quote) {
                        i += 3;
                        break;
                    }
                    if (input[i] == '\\' && i + 1 < input_len) {
                        i += 2;
                    } else {
                        i++;
                    }
                }
            } else {
                i++;
                while (i < input_len && input[i] != quote && input[i] != '\n' && input[i] != '\r') {
                    if (input[i] == '\\' && i + 1 < input_len) {
                        if (input[i+1] == '\r' && i + 2 < input_len && input[i+2] == '\n') {
                            i += 3;
                        } else {
                            i += 2;
                        }
                    } else {
                        i++;
                    }
                }
                if (i < input_len && input[i] == quote) i++;
            }
            ast_node_t *node = ast_create_node(AST_NODE_STRING, input + str_start, i - str_start);
            if (!head) head = node; else tail->next = node;
            tail = node;
            continue;
        }

        // 7. Numbers (hex, octal, float, etc.)
        if (isdigit((unsigned char)input[i]) || (input[i] == '.' && i + 1 < input_len && isdigit((unsigned char)input[i+1]))) {
            size_t num_start = i;
            if (input[i] == '0' && i + 1 < input_len && (input[i+1] == 'x' || input[i+1] == 'X')) {
                i += 2;
                while (i < input_len && (isxdigit((unsigned char)input[i]) || input[i] == '.' || input[i] == '_')) {
                    i++;
                }
            } else {
                while (i < input_len && (isalnum((unsigned char)input[i]) || input[i] == '.' || input[i] == '_')) {
                    i++;
                }
            }
            ast_node_t *node = ast_create_node(AST_NODE_NUMBER, input + num_start, i - num_start);
            if (!head) head = node; else tail->next = node;
            tail = node;
            continue;
        }

        // 8. Identifiers
        if (isalpha((unsigned char)input[i]) || input[i] == '_') {
            size_t id_start = i;
            while (i < input_len && (isalnum((unsigned char)input[i]) || input[i] == '_')) {
                i++;
            }
            size_t id_len = i - id_start;
            ast_node_type_t type = AST_NODE_IDENTIFIER;

            if (is_word_in_list(input + id_start, id_len, def->keywords, def->num_keywords)) {
                type = AST_NODE_KEYWORD;
                if (strncmp(input + id_start, "def", id_len) == 0 || strncmp(input + id_start, "class", id_len) == 0) {
                    last_was_def_or_class = 1;
                } else if (strncmp(input + id_start, "struct", id_len) == 0 || strncmp(input + id_start, "union", id_len) == 0 || strncmp(input + id_start, "enum", id_len) == 0) {
                    last_was_struct_or_union = 1;
                }
            } else if (is_word_in_list(input + id_start, id_len, def->types, def->num_types)) {
                type = AST_NODE_TYPE;
            } else if (last_was_def_or_class) {
                type = AST_NODE_FUNCTION;
                last_was_def_or_class = 0;
            } else if (last_was_struct_or_union) {
                type = AST_NODE_TYPE;
                last_was_struct_or_union = 0;
            } else if (is_all_caps(input + id_start, id_len)) {
                type = AST_NODE_NUMBER; // Map constants to number/constant color scheme
            } else {
                size_t next_idx = i;
                while (next_idx < input_len && isspace((unsigned char)input[next_idx])) {
                    next_idx++;
                }
                if (next_idx < input_len && input[next_idx] == '(') {
                    type = AST_NODE_FUNCTION;
                }
            }

            ast_node_t *node = ast_create_node(type, input + id_start, id_len);
            if (!head) head = node; else tail->next = node;
            tail = node;
            continue;
        }

        // 9. Operators
        if (strchr("+-*/%=&|^!~<>(){}[];:.,?", input[i])) {
            ast_node_t *node = ast_create_node(AST_NODE_OPERATOR, input + i, 1);
            if (!head) head = node; else tail->next = node;
            tail = node;
            i++;
            continue;
        }

        // 10. Catch-all text
        ast_node_t *node = ast_create_node(AST_NODE_TEXT, input + i, 1);
        if (!head) head = node; else tail->next = node;
        tail = node;
        i++;
    }

    return head;
}

char* ast_convert(const char *filename, const char *input, size_t input_len, size_t *out_len, int enable_colors) {
    if (!filename) return NULL;

    const syntax_def_t* def = NULL;
    const char *ext = strrchr(filename, '.');
    if (ext) {
        if (strcmp(ext, ".json") == 0) def = get_json_syntax_def();
        else if (strcmp(ext, ".csv") == 0) def = get_csv_syntax_def();
        else if (strcmp(ext, ".tsv") == 0) def = get_tsv_syntax_def();
        else if (strcmp(ext, ".xml") == 0 || strcmp(ext, ".html") == 0 || strcmp(ext, ".xhtml") == 0) def = get_xml_syntax_def();
        else if (strcmp(ext, ".diff") == 0 || strcmp(ext, ".patch") == 0) def = get_diff_syntax_def();
        else if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0 || strcmp(ext, ".cpp") == 0 || strcmp(ext, ".hpp") == 0) def = get_c_syntax_def();
        else if (strcmp(ext, ".log") == 0) def = get_log_syntax_def();
        else if (strcmp(ext, ".py") == 0) def = get_py_syntax_def();
        else if (strcmp(ext, ".js") == 0 || strcmp(ext, ".mjs") == 0 || strcmp(ext, ".cjs") == 0 ||
                 strcmp(ext, ".ts") == 0 || strcmp(ext, ".jsx") == 0 || strcmp(ext, ".tsx") == 0) {
            def = get_js_syntax_def();
        }
    }

    if (!def) {
        size_t si = 0;
        while (si < input_len && (input[si] == ' ' || input[si] == '\t' || input[si] == '\n' || input[si] == '\r')) {
            si++;
        }
        if (si < input_len && (input[si] == '{' || input[si] == '[')) {
            def = get_json_syntax_def();
        } else if (si < input_len && input[si] == '<') {
            def = get_xml_syntax_def();
        } else {
            def = get_txt_syntax_def();
        }
    }

    if (!enable_colors) {
        char *formatted_src = NULL;
        size_t formatted_len = input_len;
        if (def->format_fn) {
            formatted_src = def->format_fn(input, input_len, &formatted_len);
        }
        if (formatted_src) {
            *out_len = formatted_len;
            return formatted_src;
        } else {
            char *copy = malloc(input_len + 1);
            memcpy(copy, input, input_len);
            copy[input_len] = '\0';
            *out_len = input_len;
            return copy;
        }
    }

    g_pool_active = 1;

    char *formatted_src = NULL;
    size_t formatted_len = input_len;
    if (def->format_fn) {
        formatted_src = def->format_fn(input, input_len, &formatted_len);
    }

    const char *parse_src = formatted_src ? formatted_src : input;
    size_t parse_len = formatted_len;

    ast_node_t *ast = NULL;
    if (def->parse_fn) {
        ast = def->parse_fn(parse_src, parse_len);
    } else {
        ast = generic_lexer_to_ast(parse_src, parse_len, def);
    }

    char *res = ast_highlight_stream(ast, out_len);
    ast_free_nodes(ast);
    if (formatted_src) free(formatted_src);

    // Free pool blocks
    ast_pool_block_t *block = g_pool_head;
    while (block) {
        ast_pool_block_t *tmp = block->next;
        free(block);
        block = tmp;
    }
    g_pool_head = NULL;
    g_pool_current = NULL;
    g_pool_active = 0;

    return res;
}

