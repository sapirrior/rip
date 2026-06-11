#include "some.h"
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <regex.h>
#include <fcntl.h>
#include <errno.h>


/* ─────────────────────────────────────────────────────────────────────────── *
 *  State init / free                                                          *
 * ─────────────────────────────────────────────────────────────────────────── */
void some_init_state(some_state_t *state) {
    state->raw_lines             = NULL;
    state->num_raw_lines         = 0;
    state->raw_lines_capacity    = 0;
    state->display_lines         = NULL;
    state->num_display_lines     = 0;
    state->display_lines_capacity= 0;
    state->top_line              = 0;
    state->horiz_offset          = 0;
    state->max_line_width        = 0;
    state->term_rows             = 0;
    state->term_cols             = 0;
    state->tty_fd                = -1;
    state->raw_mode_enabled      = 0;
    state->search_pattern[0]     = '\0';
    state->filter_pattern[0]     = '\0';
    state->search_dir            = 1;
    state->search_case_insensitive = 1; /* some default: case-insensitive */
    state->search_highlight      = 1;
    state->num_prefix            = -1;
    state->wrap_enabled          = 1;
    state->line_numbers          = 0;
    state->follow_mode           = 0;
    state->status_msg[0]         = '\0';
    state->filename              = NULL;
    state->is_stdin              = 0;
    state->file_size             = 0;
    state->verbose_prompt        = 0;

    /* Search Cache */
    state->search_matches        = NULL;
    state->num_search_matches    = 0;
    state->search_matches_capacity = 0;
    state->current_match_idx     = -1;

    /* Incremental loading */
    state->stdin_eof             = 1;
    state->raw_last_has_newline  = 1;

    state->search_history_count  = 0;
    state->filter_history_count  = 0;
    state->syntax_highlighting   = 1;
    state->diff_enabled          = 0;
    state->diff_status           = NULL;
}

void some_free_state(some_state_t *state) {
    if (state->raw_lines) {
        for (size_t i = 0; i < state->num_raw_lines; i++)
            free(state->raw_lines[i].data);
        free(state->raw_lines);
    }
    if (state->display_lines) {
        for (size_t i = 0; i < state->num_display_lines; i++)
            free(state->display_lines[i].data);
        free(state->display_lines);
    }
    if (state->search_matches) {
        free(state->search_matches);
    }
    if (state->diff_status) {
        free(state->diff_status);
    }
    some_init_state(state);
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  Line buffer helpers                                                         *
 * ─────────────────────────────────────────────────────────────────────────── */
void some_add_raw_line(some_state_t *state, const char *data, size_t len) {
    if (state->num_raw_lines >= state->raw_lines_capacity) {
        state->raw_lines_capacity = state->raw_lines_capacity ? state->raw_lines_capacity * 2 : 128;
        state->raw_lines = realloc(state->raw_lines,
                                   state->raw_lines_capacity * sizeof(some_line_t));
    }
    state->raw_lines[state->num_raw_lines].data = malloc(len + 1);
    memcpy(state->raw_lines[state->num_raw_lines].data, data, len);
    state->raw_lines[state->num_raw_lines].data[len] = '\0';
    state->raw_lines[state->num_raw_lines].len = len;
    state->num_raw_lines++;
}

void some_add_display_line(some_state_t *state, const char *data, size_t len, size_t raw_line_idx) {
    if (state->num_display_lines >= state->display_lines_capacity) {
        state->display_lines_capacity = state->display_lines_capacity
                                        ? state->display_lines_capacity * 2 : 128;
        state->display_lines = realloc(state->display_lines,
                                       state->display_lines_capacity * sizeof(some_line_t));
    }
    state->display_lines[state->num_display_lines].data = malloc(len + 1);
    memcpy(state->display_lines[state->num_display_lines].data, data, len);
    state->display_lines[state->num_display_lines].data[len] = '\0';
    state->display_lines[state->num_display_lines].len = len;
    state->display_lines[state->num_display_lines].raw_line_idx = raw_line_idx;
    state->num_display_lines++;
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  Word-wrap (preserves leading indent)                                       *
 * ─────────────────────────────────────────────────────────────────────────── */
static void wrap_line(some_state_t *state, const char *line, size_t len, int width, size_t raw_offset, size_t raw_line_idx) {
    if (width <= 0) width = 80;

    /* Compute leading indent of the raw line (spaces and tabs) */
    size_t indent_bytes = 0;
    int indent_cols = 0;
    while (indent_bytes < len) {
        if (line[indent_bytes] == ' ') {
            indent_cols++;
            indent_bytes++;
        } else if (line[indent_bytes] == '\t') {
            indent_cols += 8;
            indent_bytes++;
        } else {
            break;
        }
    }

    /* Clamp indent columns to at most half the screen width to preserve space for text */
    if (indent_cols >= width / 2) {
        indent_cols = (width / 2) > 0 ? (width / 2) : 0;
        indent_bytes = indent_cols;
    }

    char *indent_str = malloc(indent_bytes + 1);
    if (indent_bytes > 0) {
        memcpy(indent_str, line, indent_bytes);
    }
    indent_str[indent_bytes] = '\0';

    size_t byte_off = 0;
    int first_line = 1;
    char active_ansi[128] = "";

    while (byte_off < len) {
        int max_cols = first_line ? width : (width > indent_cols ? width - indent_cols : 1);

        size_t scan_byte = byte_off;
        int current_cols = 0;
        size_t last_word_boundary_byte = 0;

        while (scan_byte < len) {
            if (line[scan_byte] == '\033' && scan_byte + 1 < len && line[scan_byte + 1] == '[') {
                scan_byte += 2;
                while (scan_byte < len && !(line[scan_byte] >= 64 && line[scan_byte] <= 126)) {
                    scan_byte++;
                }
                if (scan_byte < len) scan_byte++;
                continue;
            }

            unsigned int ch;
            int clen = some_decode_utf8(line + scan_byte, len - scan_byte, &ch);
            int w = some_char_width(ch, current_cols);

            if (current_cols + w > max_cols) {
                break;
            }

            if (ch == ' ' || ch == '\t' || ch == '-' || ch == '/' || ch == '\\') {
                last_word_boundary_byte = scan_byte;
            }

            current_cols += w;
            scan_byte += clen;
        }

        size_t split_byte = scan_byte;
        size_t next_line_start_byte = split_byte;

        if (split_byte < len) {
            if (last_word_boundary_byte > byte_off) {
                split_byte = last_word_boundary_byte;
                unsigned int boundary_ch;
                int boundary_len = some_decode_utf8(line + split_byte, len - split_byte, &boundary_ch);
                if (boundary_ch == ' ' || boundary_ch == '\t') {
                    next_line_start_byte = split_byte + boundary_len;
                } else {
                    split_byte += boundary_len;
                    next_line_start_byte = split_byte;
                }
            }
        }

        if (split_byte == byte_off) {
            unsigned int ch;
            int clen = some_decode_utf8(line + byte_off, len - byte_off, &ch);
            split_byte = byte_off + clen;
            next_line_start_byte = split_byte;
        }

        size_t segment_len = split_byte - byte_off;

        size_t traverse = byte_off;
        while (traverse < split_byte) {
            if (line[traverse] == '\033' && traverse + 1 < split_byte && line[traverse + 1] == '[') {
                size_t esc_start = traverse;
                traverse += 2;
                while (traverse < split_byte && !(line[traverse] >= 64 && line[traverse] <= 126)) {
                    traverse++;
                }
                if (traverse < split_byte) traverse++;
                size_t esc_len = traverse - esc_start;
                if (esc_len < sizeof(active_ansi)) {
                    if (esc_len == 4 && strncmp(line + esc_start, "\033[0m", 4) == 0) {
                        active_ansi[0] = '\0';
                    } else if (line[traverse - 1] == 'm') {
                        memcpy(active_ansi, line + esc_start, esc_len);
                        active_ansi[esc_len] = '\0';
                    }
                }
            } else {
                traverse++;
            }
        }

        size_t prefix_len = first_line ? 0 : indent_bytes;
        size_t ansi_len = active_ansi[0] ? strlen(active_ansi) : 0;
        size_t reset_len = active_ansi[0] ? 4 : 0;

        size_t total_len = prefix_len + ansi_len + segment_len + reset_len;
        char *buf = malloc(total_len + 1);
        char *p = buf;

        if (prefix_len) {
            memcpy(p, indent_str, prefix_len);
            p += prefix_len;
        }
        if (ansi_len) {
            memcpy(p, active_ansi, ansi_len);
            p += ansi_len;
        }
        if (segment_len) {
            memcpy(p, line + byte_off, segment_len);
            p += segment_len;
        }
        if (reset_len) {
            memcpy(p, "\033[0m", 4);
            p += 4;
        }
        *p = '\0';

        some_add_display_line(state, buf, total_len, raw_line_idx);
        state->display_lines[state->num_display_lines - 1].byte_offset = raw_offset + byte_off;
        free(buf);

        first_line = 0;
        byte_off = next_line_start_byte;
    }

    free(indent_str);
}

void some_reflow_all(some_state_t *state) {
    if (state->display_lines) {
        for (size_t i = 0; i < state->num_display_lines; i++)
            free(state->display_lines[i].data);
        free(state->display_lines);
        state->display_lines = NULL;
    }
    state->num_display_lines     = 0;
    state->display_lines_capacity= 0;

    int ln_width = some_get_gutter_width(state);
    int wrap_width = state->term_cols - ln_width;
    if (wrap_width < 1) wrap_width = 1;

    regex_t filter_re;
    int has_filter = 0;
    if (state->filter_pattern[0]) {
        int flags = REG_EXTENDED | (state->search_case_insensitive ? REG_ICASE : 0);
        has_filter = (regcomp(&filter_re, state->filter_pattern, flags) == 0);
    }

    for (size_t i = 0; i < state->num_raw_lines; i++) {
        some_line_t *r = &state->raw_lines[i];
        if (has_filter) {
            if (regexec(&filter_re, r->data, 0, NULL, 0) != 0) {
                continue;
            }
        }
        if (state->wrap_enabled) {
            wrap_line(state, r->data, r->len, wrap_width, r->byte_offset, i);
        } else {
            some_add_display_line(state, r->data, r->len, i);
            state->display_lines[state->num_display_lines - 1].byte_offset = r->byte_offset;
        }
    }

    if (has_filter) {
        regfree(&filter_re);
    }

    if (state->syntax_highlighting) {
        ast_highlight_display_lines(state);
    }

    /* Calculate the visual width of the longest display line */
    int max_w = 0;
    for (size_t i = 0; i < state->num_display_lines; i++) {
        some_line_t *d = &state->display_lines[i];
        int vis_w = 0;
        size_t off = 0;
        while (off < d->len) {
            if (d->data[off] == '\033' && off + 1 < d->len && d->data[off + 1] == '[') {
                off += 2;
                while (off < d->len && !(d->data[off] >= 64 && d->data[off] <= 126)) {
                    off++;
                }
                if (off < d->len) {
                    off++;
                }
                continue;
            }
            unsigned int ch;
            int clen = some_decode_utf8(d->data + off, d->len - off, &ch);
            vis_w += some_char_width(ch, vis_w);
            off += clen;
        }
        if (vis_w > max_w) {
            max_w = vis_w;
        }
    }
    state->max_line_width = max_w;

    some_update_search_matches(state);
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  Read input (file or stdin)                                                 *
 * ─────────────────────────────────────────────────────────────────────────── */
static int load_from_fp(some_state_t *state, FILE *fp) {
    state->stdin_eof = 1;

    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);

    if (fp == stdin) {
        state->stdin_eof = 0;
        /* Set stdin (fd 0) to non-blocking */
        int flags = fcntl(STDIN_FILENO, F_GETFL);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

        /* Read first chunk (up to 32KB) to show initial screen instantly */
        size_t target = 32768;
        while (len < target) {
            ssize_t nr = read(STDIN_FILENO, buf + len, cap - len - 1);
            if (nr > 0) {
                len += nr;
                if (len + 1024 >= cap) {
                    cap *= 2;
                    buf = realloc(buf, cap);
                }
            } else if (nr == 0) {
                state->stdin_eof = 1;
                break;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break; /* No more data available right now */
                }
                if (errno == EINTR) continue;
                state->stdin_eof = 1;
                break;
            }
        }
    } else {
        /* Read entire file synchronously */
        size_t nr;
        while ((nr = fread(buf + len, 1, cap - len - 1, fp)) > 0) {
            len += nr;
            if (len + 1024 >= cap) {
                cap *= 2;
                buf = realloc(buf, cap);
            }
        }
        state->stdin_eof = 1;
    }

    buf[len] = '\0';
    state->file_size = len;

    // Use formatting filter if present (e.g. CSV alignment or JSON pretty-print)
    const syntax_def_t* def = NULL;
    if (state->filename) {
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
    }

    char *formatted = NULL;
    size_t out_len = len;
    if (def && def->format_fn) {
        formatted = def->format_fn(buf, len, &out_len);
    }

    const char *src = formatted ? formatted : buf;
    size_t src_len = out_len;

    if (src_len > 0) {
        state->raw_last_has_newline = (src[src_len - 1] == '\n' || src[src_len - 1] == '\r');
    } else {
        state->raw_last_has_newline = 1;
    }

    size_t start = 0;
    for (size_t i = 0; i <= src_len; i++) {
        if (i == src_len || src[i] == '\n' || src[i] == '\r') {
            some_add_raw_line(state, src + start, i - start);
            state->raw_lines[state->num_raw_lines - 1].byte_offset = start;
            if (i < src_len && src[i] == '\r' && i+1 < src_len && src[i+1] == '\n') i++;
            start = i + 1;
        }
    }

    free(buf);
    if (formatted) free(formatted);

    if (state->diff_enabled) {
        some_load_diff_status(state);
    }
    return 0;
}

void some_append_stream_data(some_state_t *state, const char *buf, size_t len) {
    if (len == 0) return;

    size_t start = 0;
    int first_part = 1;

    for (size_t i = 0; i <= len; i++) {
        if (i == len || buf[i] == '\n' || buf[i] == '\r') {
            size_t segment_len = i - start;

            if (first_part && !state->raw_last_has_newline && state->num_raw_lines > 0) {
                /* Append to the last line */
                some_line_t *last = &state->raw_lines[state->num_raw_lines - 1];
                last->data = realloc(last->data, last->len + segment_len + 1);
                memcpy(last->data + last->len, buf + start, segment_len);
                last->len += segment_len;
                last->data[last->len] = '\0';
            } else {
                /* Add as a new line */
                size_t old_size = state->file_size;
                some_add_raw_line(state, buf + start, segment_len);
                state->raw_lines[state->num_raw_lines - 1].byte_offset = old_size + start;
            }

            if (i < len) {
                state->raw_last_has_newline = 1;
                if (buf[i] == '\r' && i + 1 < len && buf[i+1] == '\n') i++;
                start = i + 1;
            } else {
                state->raw_last_has_newline = 0;
            }
            first_part = 0;
        }
    }
    state->file_size += len;
}

void some_update_search_matches(some_state_t *state) {
    if (state->search_matches) {
        free(state->search_matches);
        state->search_matches = NULL;
    }
    state->num_search_matches = 0;
    state->search_matches_capacity = 0;
    state->current_match_idx = -1;

    if (!state->search_pattern[0]) return;

    int flags = REG_EXTENDED | REG_NOSUB | (state->search_case_insensitive ? REG_ICASE : 0);
    regex_t re;
    if (regcomp(&re, state->search_pattern, flags) != 0) {
        return;
    }

    for (size_t i = 0; i < state->num_display_lines; i++) {
        if (regexec(&re, state->display_lines[i].data, 0, NULL, 0) == 0) {
            if (state->num_search_matches >= state->search_matches_capacity) {
                state->search_matches_capacity = state->search_matches_capacity ? state->search_matches_capacity * 2 : 64;
                state->search_matches = realloc(state->search_matches, state->search_matches_capacity * sizeof(size_t));
            }
            state->search_matches[state->num_search_matches++] = i;
        }
    }

    regfree(&re);

    /* Update current_match_idx to the first match at or after top_line */
    if (state->num_search_matches > 0) {
        state->current_match_idx = 0;
        for (size_t i = 0; i < state->num_search_matches; i++) {
            if (state->search_matches[i] >= state->top_line) {
                state->current_match_idx = (int)i;
                break;
            }
        }
    }
}

int some_read_input(some_state_t *state, const char *path) {
    if (!path || strcmp(path, "-") == 0) {
        if (isatty(STDIN_FILENO)) {
            fprintf(stderr, "Missing filename (\"some --help\" for help)\n");
            return -1;
        }
        state->filename = "stdin";
        return load_from_fp(state, stdin);
    }
    FILE *fp = fopen(path, "r");
    if (!fp) { perror("fopen"); return -1; }
    state->filename = path;
    int ret = load_from_fp(state, fp);
    fclose(fp);
    return ret;
}

/* Reload: discard everything and re-read the same file */
int some_reload(some_state_t *state) {
    if (!state->filename || strcmp(state->filename, "stdin") == 0)
        return -1; /* can't reload stdin */

    /* Save current position */
    size_t saved_top = state->top_line;

    /* Free old data */
    if (state->raw_lines) {
        for (size_t i = 0; i < state->num_raw_lines; i++)
            free(state->raw_lines[i].data);
        free(state->raw_lines);
    }
    if (state->display_lines) {
        for (size_t i = 0; i < state->num_display_lines; i++)
            free(state->display_lines[i].data);
        free(state->display_lines);
    }
    state->raw_lines              = NULL;
    state->num_raw_lines          = 0;
    state->raw_lines_capacity     = 0;
    state->display_lines          = NULL;
    state->num_display_lines      = 0;
    state->display_lines_capacity = 0;

    FILE *fp = fopen(state->filename, "r");
    if (!fp) { perror("fopen"); return -1; }
    load_from_fp(state, fp);
    fclose(fp);

    some_reflow_all(state);

    /* Restore position (clamped) */
    size_t max_top = 0;
    int text_rows = state->term_rows - 1;
    if (text_rows > 0 && state->num_display_lines > (size_t)text_rows)
        max_top = state->num_display_lines - text_rows;
    state->top_line = (saved_top <= max_top) ? saved_top : max_top;
    return 0;
}
