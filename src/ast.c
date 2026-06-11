#include "some.h"
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

static ast_node_t* generic_lexer_to_ast_state(const char *input, size_t input_len, const syntax_def_t *def, syntax_state_t *state) {
    ast_node_t *head = NULL, *tail = NULL;
    size_t i = 0;

    int last_was_def_or_class = 0;
    int last_was_struct_or_union = 0;

    while (i < input_len) {
        // Handle current state context
        if (state->context == 1) { // block comment
            size_t start = i;
            size_t bce_len = def->block_comment_end ? strlen(def->block_comment_end) : 0;
            int found = 0;
            while (i < input_len) {
                if (bce_len && i + bce_len <= input_len && strncmp(input + i, def->block_comment_end, bce_len) == 0) {
                    i += bce_len;
                    state->context = 0;
                    found = 1;
                    break;
                }
                i++;
            }
            ast_node_t *node = ast_create_node(AST_NODE_COMMENT, input + start, i - start);
            if (!head) head = node; else tail->next = node;
            tail = node;
            continue;
        }

        if (state->context == 2) { // string
            size_t start = i;
            char quote = state->string_quote;
            int found = 0;
            while (i < input_len) {
                if (input[i] == quote) {
                    i++;
                    state->context = 0;
                    found = 1;
                    break;
                }
                if (input[i] == '\\' && i + 1 < input_len) {
                    i += 2;
                } else {
                    i++;
                }
            }
            // If we reached the end of this display line segment without finding the closing quote,
            // we consume all remaining characters and keep state->context as 2.
            ast_node_t *node = ast_create_node(AST_NODE_STRING, input + start, i - start);
            if (!head) head = node; else tail->next = node;
            tail = node;
            continue;
        }

        if (state->context == 3 || state->context == 4) { // py triple double/single quote
            size_t start = i;
            char quote = (state->context == 3) ? '"' : '\'';
            int found = 0;
            while (i + 2 < input_len) {
                if (input[i] == quote && input[i+1] == quote && input[i+2] == quote) {
                    i += 3;
                    state->context = 0;
                    found = 1;
                    break;
                }
                if (input[i] == '\\' && i + 1 < input_len) {
                    i += 2;
                } else {
                    i++;
                }
            }
            if (!found) i = input_len; // Consume the rest of the line
            ast_node_t *node = ast_create_node(AST_NODE_STRING, input + start, i - start);
            if (!head) head = node; else tail->next = node;
            tail = node;
            continue;
        }

        if (state->context == 5) { // xml comment
            size_t start = i;
            int found = 0;
            while (i + 2 < input_len) {
                if (input[i] == '-' && input[i+1] == '-' && input[i+2] == '>') {
                    i += 3;
                    state->context = 0;
                    found = 1;
                    break;
                }
                i++;
            }
            if (!found) i = input_len;
            ast_node_t *node = ast_create_node(AST_NODE_COMMENT, input + start, i - start);
            if (!head) head = node; else tail->next = node;
            tail = node;
            continue;
        }

        if (state->context == 6) { // xml tag
            size_t start = i;
            int found = 0;
            while (i < input_len) {
                if (input[i] == '>') {
                    i++;
                    state->context = 0;
                    found = 1;
                    break;
                }
                i++;
            }
            ast_node_t *node = ast_create_node(AST_NODE_TEXT, input + start, i - start);
            if (!head) head = node; else tail->next = node;
            tail = node;
            continue;
        }

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
            if (i + bcs_len <= input_len && strncmp(input + i, def->block_comment_start, bcs_len) == 0) {
                state->context = 1;
                ast_node_t *node = ast_create_node(AST_NODE_COMMENT, input + i, bcs_len);
                if (!head) head = node; else tail->next = node;
                tail = node;
                i += bcs_len;
                continue;
            }
        }

        // 4. Preprocessor directive
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

        // 5. XML comments / tags
        if (def->extension && (strcmp(def->extension, ".xml") == 0 || strcmp(def->extension, ".html") == 0 || strcmp(def->extension, ".xhtml") == 0)) {
            if (i + 4 <= input_len && strncmp(input + i, "<!--", 4) == 0) {
                state->context = 5;
                ast_node_t *node = ast_create_node(AST_NODE_COMMENT, input + i, 4);
                if (!head) head = node; else tail->next = node;
                tail = node;
                i += 4;
                continue;
            }
            if (input[i] == '<') {
                state->context = 6;
                ast_node_t *node = ast_create_node(AST_NODE_TEXT, input + i, 1);
                if (!head) head = node; else tail->next = node;
                tail = node;
                i++;
                continue;
            }
        }

        // 6. Python Decorators
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

        // 7. Strings
        if (input[i] == '"' || input[i] == '\'') {
            char quote = input[i];
            if (def->extension && strcmp(def->extension, ".py") == 0 && i + 2 < input_len && input[i+1] == quote && input[i+2] == quote) {
                state->context = (quote == '"') ? 3 : 4;
                i += 3;
                continue;
            } else {
                state->context = 2;
                state->string_quote = quote;
                // We consume the opening quote here and let subsequent iterations or state handling handle the rest
                ast_node_t *node = ast_create_node(AST_NODE_STRING, input + i, 1);
                if (!head) head = node; else tail->next = node;
                tail = node;
                i++;
                continue;
            }
        }

        // 8. Numbers
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

        // 9. Identifiers
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
                type = AST_NODE_NUMBER;
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

        // 10. Operators
        if (strchr("+-*/%=&|^!~<>(){}[];:.,?", input[i])) {
            char op = input[i];
            if (op == '{') state->brace_depth++;
            else if (op == '}') { if (state->brace_depth > 0) state->brace_depth--; }
            else if (op == '(') state->paren_depth++;
            else if (op == ')') { if (state->paren_depth > 0) state->paren_depth--; }
            else if (op == '[') state->bracket_depth++;
            else if (op == ']') { if (state->bracket_depth > 0) state->bracket_depth--; }

            ast_node_t *node = ast_create_node(AST_NODE_OPERATOR, input + i, 1);
            if (!head) head = node; else tail->next = node;
            tail = node;
            i++;
            continue;
        }

        // 11. Catch-all text
        ast_node_t *node = ast_create_node(AST_NODE_TEXT, input + i, 1);
        if (!head) head = node; else tail->next = node;
        tail = node;
        i++;
    }

    return head;
}

static ast_node_t* generic_lexer_to_ast(const char *input, size_t input_len, const syntax_def_t *def) {
    syntax_state_t state;
    memset(&state, 0, sizeof(state));
    return generic_lexer_to_ast_state(input, input_len, def, &state);
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

void ast_highlight_display_lines(void *some_state_ptr) {
    some_state_t *state = (some_state_t *)some_state_ptr;
    if (!state->syntax_highlighting || !state->filename || state->num_display_lines == 0) return;

    const syntax_def_t* def = NULL;
    const char *ext = strrchr(state->filename, '.');
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
        // Check content fallback inside first line
        some_line_t *first = &state->display_lines[0];
        size_t si = 0;
        while (si < first->len && (first->data[si] == ' ' || first->data[si] == '\t')) si++;
        if (si < first->len && (first->data[si] == '{' || first->data[si] == '[')) {
            def = get_json_syntax_def();
        } else if (si < first->len && first->data[si] == '<') {
            def = get_xml_syntax_def();
        } else {
            def = get_txt_syntax_def();
        }
    }

    // 1. Compute state at the start of each RAW line sequentially.
    // This gives us a highly accurate AST context mapping of the document,
    // exactly like Treesitter / modern editors, regardless of visual wraps.
    syntax_state_t *raw_states = malloc(state->num_raw_lines * sizeof(syntax_state_t));
    syntax_state_t curr_state;
    memset(&curr_state, 0, sizeof(curr_state));

    g_pool_active = 1;

    for (size_t r = 0; r < state->num_raw_lines; r++) {
        raw_states[r] = curr_state;
        // Parse the raw line plain text to update the running compiler/AST state
        some_line_t *raw_line = &state->raw_lines[r];
        ast_node_t *ast = NULL;
        if (def->parse_fn) {
            ast = def->parse_fn(raw_line->data, raw_line->len);
        } else {
            ast = generic_lexer_to_ast_state(raw_line->data, raw_line->len, def, &curr_state);
        }
        if (ast) {
            ast_free_nodes(ast);
        }
    }

    // 2. Highlight each display line using the cached state of its corresponding raw line.
    for (size_t i = 0; i < state->num_display_lines; i++) {
        some_line_t *line = &state->display_lines[i];
        size_t raw_idx = line->raw_line_idx;
        
        // Retrieve the start state of the corresponding raw line
        syntax_state_t line_start_state = (raw_idx < state->num_raw_lines) ? raw_states[raw_idx] : curr_state;

        // However, if this display line is a wrapped continuation of the same raw line,
        // we must parse from the byte offset within the raw line. To achieve this,
        // we parse the raw line up to the byte offset to get the precise nested context.
        if (raw_idx < state->num_raw_lines) {
            some_line_t *raw_line = &state->raw_lines[raw_idx];
            size_t inner_offset = line->byte_offset - raw_line->byte_offset;
            if (inner_offset > 0 && inner_offset <= raw_line->len) {
                // Determine the parser state exactly at the wrap split boundary
                ast_node_t *prefix_ast = NULL;
                if (!def->parse_fn) {
                    prefix_ast = generic_lexer_to_ast_state(raw_line->data, inner_offset, def, &line_start_state);
                    if (prefix_ast) {
                        ast_free_nodes(prefix_ast);
                    }
                }
            }
        }

        ast_node_t *ast = NULL;
        if (def->parse_fn) {
            ast = def->parse_fn(line->data, line->len);
        } else {
            ast = generic_lexer_to_ast_state(line->data, line->len, def, &line_start_state);
        }

        size_t highlighted_len = 0;
        char *highlighted = ast_highlight_stream(ast, &highlighted_len);
        ast_free_nodes(ast);

        free(line->data);
        line->data = highlighted;
        line->len = highlighted_len;
    }

    free(raw_states);

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
}

