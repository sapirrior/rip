#include "some.h"
#include "ast.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void ast_append_char(char **dest, size_t *di, size_t *cap, char c) {
    if (*di + 2 >= *cap) {
        *cap = *cap ? *cap * 2 : 1024;
        *dest = realloc(*dest, *cap);
    }
    (*dest)[(*di)++] = c;
}

void ast_append_str(char **dest, size_t *di, size_t *cap, const char *s) {
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

static void apply_color(char **dest, size_t *di, size_t *cap, const char *color) {
    if (color) {
        ast_append_str(dest, di, cap, color);
    }
}

static void apply_reset(char **dest, size_t *di, size_t *cap) {
    ast_append_str(dest, di, cap, "\033[0m");
}

static void write_colored_text(const char *text, size_t len, const char *color, char **dest, size_t *di, size_t *cap) {
    if (len == 0) return;
    if (color) apply_color(dest, di, cap, color);
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        if (c == '\n' || c == '\r') {
            if (color) apply_reset(dest, di, cap);
            ast_append_char(dest, di, cap, c);
            if (c == '\r' && i + 1 < len && text[i+1] == '\n') {
                ast_append_char(dest, di, cap, '\n');
                i++;
            }
            if (color) apply_color(dest, di, cap, color);
        } else {
            ast_append_char(dest, di, cap, c);
        }
    }
    if (color) apply_reset(dest, di, cap);
}

void highlight_line(const char *input, size_t input_len, const syntax_def_t *def, syntax_state_t *state, char **dest, size_t *di, size_t *cap) {
    size_t i = 0;
    int last_was_def_or_class = 0;
    int last_was_struct_or_union = 0;

    while (i < input_len) {
        // Handle current state context
        if (state->context == 1) { // block comment
            size_t start = i;
            size_t bce_len = def->block_comment_end ? strlen(def->block_comment_end) : 0;
            while (i < input_len) {
                if (bce_len && i + bce_len <= input_len && strncmp(input + i, def->block_comment_end, bce_len) == 0) {
                    i += bce_len;
                    state->context = 0;
                    break;
                }
                i++;
            }
            write_colored_text(input + start, i - start, "\033[38;2;139;148;158m", dest, di, cap);
            continue;
        }

        if (state->context == 2) { // string
            size_t start = i;
            char quote = state->string_quote;
            while (i < input_len) {
                if (input[i] == quote) {
                    i++;
                    state->context = 0;
                    break;
                }
                if (input[i] == '\\' && i + 1 < input_len) {
                    i += 2;
                } else {
                    i++;
                }
            }
            write_colored_text(input + start, i - start, "\033[38;2;165;214;255m", dest, di, cap);
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
            if (!found) i = input_len;
            write_colored_text(input + start, i - start, "\033[38;2;165;214;255m", dest, di, cap);
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
            write_colored_text(input + start, i - start, "\033[38;2;139;148;158m", dest, di, cap);
            continue;
        }

        if (state->context == 6) { // xml tag
            size_t start = i;
            while (i < input_len) {
                if (input[i] == '>') {
                    i++;
                    state->context = 0;
                    break;
                }
                i++;
            }
            write_colored_text(input + start, i - start, NULL, dest, di, cap);
            continue;
        }

        // 1. Whitespace
        if (isspace((unsigned char)input[i])) {
            ast_append_char(dest, di, cap, input[i++]);
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
                write_colored_text(input + comment_start, i - comment_start, "\033[38;2;139;148;158m", dest, di, cap);
                continue;
            }
        }

        // 3. Multi-line block comment
        if (def->block_comment_start && def->block_comment_end) {
            size_t bcs_len = strlen(def->block_comment_start);
            if (i + bcs_len <= input_len && strncmp(input + i, def->block_comment_start, bcs_len) == 0) {
                state->context = 1;
                write_colored_text(input + i, bcs_len, "\033[38;2;139;148;158m", dest, di, cap);
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
            write_colored_text(input + prep_start, i - prep_start, "\033[38;2;255;123;114m", dest, di, cap);
            continue;
        }

        // 5. XML comments / tags
        if (def->extension && (strcmp(def->extension, ".xml") == 0 || strcmp(def->extension, ".html") == 0 || strcmp(def->extension, ".xhtml") == 0)) {
            if (i + 4 <= input_len && strncmp(input + i, "<!--", 4) == 0) {
                state->context = 5;
                write_colored_text(input + i, 4, "\033[38;2;139;148;158m", dest, di, cap);
                i += 4;
                continue;
            }
            if (input[i] == '<') {
                state->context = 6;
                write_colored_text(input + i, 1, NULL, dest, di, cap);
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
            write_colored_text(input + dec_start, i - dec_start, "\033[38;2;210;168;255m", dest, di, cap);
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
                write_colored_text(input + i, 1, "\033[38;2;165;214;255m", dest, di, cap);
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
            write_colored_text(input + num_start, i - num_start, "\033[38;2;121;192;255m", dest, di, cap);
            continue;
        }

        // 9. Identifiers
        if (isalpha((unsigned char)input[i]) || input[i] == '_') {
            size_t id_start = i;
            while (i < input_len && (isalnum((unsigned char)input[i]) || input[i] == '_')) {
                i++;
            }
            size_t id_len = i - id_start;
            const char *color = NULL;

            if (is_word_in_list(input + id_start, id_len, def->keywords, def->num_keywords)) {
                color = "\033[38;2;255;123;114m";
                if (strncmp(input + id_start, "def", id_len) == 0 || strncmp(input + id_start, "class", id_len) == 0) {
                    last_was_def_or_class = 1;
                } else if (strncmp(input + id_start, "struct", id_len) == 0 || strncmp(input + id_start, "union", id_len) == 0 || strncmp(input + id_start, "enum", id_len) == 0) {
                    last_was_struct_or_union = 1;
                }
            } else if (is_word_in_list(input + id_start, id_len, def->types, def->num_types)) {
                color = "\033[38;2;255;123;114m";
            } else if (last_was_def_or_class) {
                color = "\033[38;2;210;168;255m";
                last_was_def_or_class = 0;
            } else if (last_was_struct_or_union) {
                color = "\033[38;2;255;123;114m";
                last_was_struct_or_union = 0;
            } else if (is_all_caps(input + id_start, id_len)) {
                color = "\033[38;2;121;192;255m";
            } else {
                size_t next_idx = i;
                while (next_idx < input_len && isspace((unsigned char)input[next_idx])) {
                    next_idx++;
                }
                if (next_idx < input_len && input[next_idx] == '(') {
                    color = "\033[38;2;210;168;255m";
                }
            }

            write_colored_text(input + id_start, id_len, color, dest, di, cap);
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

            write_colored_text(input + i, 1, NULL, dest, di, cap);
            i++;
            continue;
        }

        // 11. Catch-all text
        write_colored_text(input + i, 1, NULL, dest, di, cap);
        i++;
    }
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

    char *formatted_src = NULL;
    size_t formatted_len = input_len;
    if (def->format_fn) {
        formatted_src = def->format_fn(input, input_len, &formatted_len);
    }

    if (!enable_colors) {
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

    const char *parse_src = formatted_src ? formatted_src : input;
    size_t parse_len = formatted_len;

    size_t cap = 1024;
    char *dest = malloc(cap);
    size_t di = 0;

    syntax_state_t state;
    memset(&state, 0, sizeof(state));

    if (def->highlight_fn) {
        def->highlight_fn(parse_src, parse_len, &state, &dest, &di, &cap);
    } else {
        highlight_line(parse_src, parse_len, def, &state, &dest, &di, &cap);
    }
    dest[di] = '\0';
    *out_len = di;

    if (formatted_src) free(formatted_src);

    return dest;
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

    syntax_state_t *raw_states = malloc(state->num_raw_lines * sizeof(syntax_state_t));
    syntax_state_t curr_state;
    memset(&curr_state, 0, sizeof(curr_state));

    for (size_t r = 0; r < state->num_raw_lines; r++) {
        raw_states[r] = curr_state;
        some_line_t *raw_line = &state->raw_lines[r];

        size_t dummy_cap = 16;
        char *dummy_dest = malloc(dummy_cap);
        size_t dummy_di = 0;

        if (def->highlight_fn) {
            def->highlight_fn(raw_line->data, raw_line->len, &curr_state, &dummy_dest, &dummy_di, &dummy_cap);
        } else {
            highlight_line(raw_line->data, raw_line->len, def, &curr_state, &dummy_dest, &dummy_di, &dummy_cap);
        }
        free(dummy_dest);
    }

    for (size_t i = 0; i < state->num_display_lines; i++) {
        some_line_t *line = &state->display_lines[i];
        size_t raw_idx = line->raw_line_idx;

        syntax_state_t line_start_state = (raw_idx < state->num_raw_lines) ? raw_states[raw_idx] : curr_state;

        if (raw_idx < state->num_raw_lines) {
            some_line_t *raw_line = &state->raw_lines[raw_idx];
            size_t inner_offset = line->byte_offset - raw_line->byte_offset;
            if (inner_offset > 0 && inner_offset <= raw_line->len) {
                size_t dummy_cap = 16;
                char *dummy_dest = malloc(dummy_cap);
                size_t dummy_di = 0;

                if (def->highlight_fn) {
                    def->highlight_fn(raw_line->data, inner_offset, &line_start_state, &dummy_dest, &dummy_di, &dummy_cap);
                } else {
                    highlight_line(raw_line->data, inner_offset, def, &line_start_state, &dummy_dest, &dummy_di, &dummy_cap);
                }
                free(dummy_dest);
            }
        }

        size_t cap = 1024;
        char *dest = malloc(cap);
        size_t di = 0;

        if (def->highlight_fn) {
            def->highlight_fn(line->data, line->len, &line_start_state, &dest, &di, &cap);
        } else {
            highlight_line(line->data, line->len, def, &line_start_state, &dest, &di, &cap);
        }
        dest[di] = '\0';

        free(line->data);
        line->data = dest;
        line->len = di;
    }

    free(raw_states);
}
