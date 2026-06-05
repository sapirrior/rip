#include "rip.h"
#include "modules.h"
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
void rip_init_state(rip_state_t *state) {
    state->raw_lines             = NULL;
    state->num_raw_lines         = 0;
    state->raw_lines_capacity    = 0;
    state->display_lines         = NULL;
    state->num_display_lines     = 0;
    state->display_lines_capacity= 0;
    state->top_line              = 0;
    state->horiz_offset          = 0;
    state->term_rows             = 0;
    state->term_cols             = 0;
    state->tty_fd                = -1;
    state->raw_mode_enabled      = 0;
    state->search_pattern[0]     = '\0';
    state->filter_pattern[0]     = '\0';
    state->search_dir            = 1;
    state->search_case_insensitive = 1; /* rip default: case-insensitive */
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
    state->stdin_nonblocking_set = 0;
    state->raw_last_has_newline  = 1;
}

void rip_free_state(rip_state_t *state) {
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
    rip_init_state(state);
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  Line buffer helpers                                                         *
 * ─────────────────────────────────────────────────────────────────────────── */
void rip_add_raw_line(rip_state_t *state, const char *data, size_t len) {
    if (state->num_raw_lines >= state->raw_lines_capacity) {
        state->raw_lines_capacity = state->raw_lines_capacity ? state->raw_lines_capacity * 2 : 128;
        state->raw_lines = realloc(state->raw_lines,
                                   state->raw_lines_capacity * sizeof(rip_line_t));
    }
    state->raw_lines[state->num_raw_lines].data = malloc(len + 1);
    memcpy(state->raw_lines[state->num_raw_lines].data, data, len);
    state->raw_lines[state->num_raw_lines].data[len] = '\0';
    state->raw_lines[state->num_raw_lines].len = len;
    state->num_raw_lines++;
}

void rip_add_display_line(rip_state_t *state, const char *data, size_t len, size_t raw_line_idx) {
    if (state->num_display_lines >= state->display_lines_capacity) {
        state->display_lines_capacity = state->display_lines_capacity
                                        ? state->display_lines_capacity * 2 : 128;
        state->display_lines = realloc(state->display_lines,
                                       state->display_lines_capacity * sizeof(rip_line_t));
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
static void wrap_line(rip_state_t *state, const char *line, size_t len, int width, size_t raw_offset, size_t raw_line_idx) {
    if (width <= 0) width = 80;

    /* Measure indent: count bytes and columns of leading whitespace */
    size_t indent_bytes = 0;
    int indent_cols = 0;
    while (indent_bytes < len) {
        unsigned int ch;
        int clen = rip_decode_utf8(line + indent_bytes, len - indent_bytes, &ch);
        if (ch == ' ') {
            indent_cols += 1;
            indent_bytes += clen;
        } else if (ch == '\t') {
            indent_cols += rip_char_width(ch, indent_cols);
            indent_bytes += clen;
        } else {
            break;
        }
    }

    /* We keep indent string to prepend to subsequent lines */
    char *indent_str = malloc(indent_bytes + 1);
    memcpy(indent_str, line, indent_bytes);
    indent_str[indent_bytes] = '\0';

    size_t byte_off = 0;
    int first_line = 1;

    while (byte_off < len) {
        /* Determine how many columns we have for this segment */
        int max_cols = first_line ? width : (width > indent_cols ? width - indent_cols : 1);

        /* Walk forward to find the split point (by columns) */
        size_t scan_byte = byte_off;
        int current_cols = 0;
        size_t last_space_byte = 0;

        while (scan_byte < len) {
            unsigned int ch;
            int clen = rip_decode_utf8(line + scan_byte, len - scan_byte, &ch);
            int w = rip_char_width(ch, current_cols);

            if (current_cols + w > max_cols) {
                break;
            }

            if (ch == ' ' || ch == '\t') {
                last_space_byte = scan_byte;
            }

            current_cols += w;
            scan_byte += clen;
        }

        /* If we reached the end of the line, wrap it all */
        if (scan_byte >= len) {
            size_t segment_len = len - byte_off;
            if (first_line) {
                rip_add_display_line(state, line + byte_off, segment_len, raw_line_idx);
                state->display_lines[state->num_display_lines - 1].byte_offset = raw_offset + byte_off;
            } else {
                char *buf = malloc(indent_bytes + segment_len + 1);
                memcpy(buf, indent_str, indent_bytes);
                memcpy(buf + indent_bytes, line + byte_off, segment_len);
                buf[indent_bytes + segment_len] = '\0';
                rip_add_display_line(state, buf, indent_bytes + segment_len, raw_line_idx);
                state->display_lines[state->num_display_lines - 1].byte_offset = raw_offset + byte_off;
                free(buf);
            }
            break;
        }

        /* Otherwise, we need to split. Try splitting at the last space */
        size_t split_byte = 0;
        if (last_space_byte > byte_off) {
            split_byte = last_space_byte;
        } else {
            split_byte = scan_byte;
        }

        /* Prevent infinite loop if no characters fit */
        if (split_byte == byte_off) {
            unsigned int ch;
            split_byte += rip_decode_utf8(line + byte_off, len - byte_off, &ch);
        }

        size_t segment_len = split_byte - byte_off;
        if (first_line) {
            rip_add_display_line(state, line + byte_off, segment_len, raw_line_idx);
            state->display_lines[state->num_display_lines - 1].byte_offset = raw_offset + byte_off;
            first_line = 0;
        } else {
            char *buf = malloc(indent_bytes + segment_len + 1);
            memcpy(buf, indent_str, indent_bytes);
            memcpy(buf + indent_bytes, line + byte_off, segment_len);
            buf[indent_bytes + segment_len] = '\0';
            rip_add_display_line(state, buf, indent_bytes + segment_len, raw_line_idx);
            state->display_lines[state->num_display_lines - 1].byte_offset = raw_offset + byte_off;
            free(buf);
        }

        byte_off = split_byte;
        /* skip trailing spaces/tabs after split point to avoid leading spaces on wrapped line */
        while (byte_off < len) {
            unsigned int ch;
            int clen = rip_decode_utf8(line + byte_off, len - byte_off, &ch);
            if (ch == ' ' || ch == '\t') {
                byte_off += clen;
            } else {
                break;
            }
        }
    }

    free(indent_str);
}

void rip_reflow_all(rip_state_t *state) {
    if (state->display_lines) {
        for (size_t i = 0; i < state->num_display_lines; i++)
            free(state->display_lines[i].data);
        free(state->display_lines);
        state->display_lines = NULL;
    }
    state->num_display_lines     = 0;
    state->display_lines_capacity= 0;

    int ln_width = rip_get_gutter_width(state);
    int wrap_width = state->term_cols - ln_width;
    if (wrap_width < 1) wrap_width = 1;

    regex_t filter_re;
    int has_filter = 0;
    if (state->filter_pattern[0]) {
        int flags = REG_EXTENDED | (state->search_case_insensitive ? REG_ICASE : 0);
        has_filter = (regcomp(&filter_re, state->filter_pattern, flags) == 0);
    }

    for (size_t i = 0; i < state->num_raw_lines; i++) {
        rip_line_t *r = &state->raw_lines[i];
        if (has_filter) {
            if (regexec(&filter_re, r->data, 0, NULL, 0) != 0) {
                continue;
            }
        }
        if (state->wrap_enabled) {
            wrap_line(state, r->data, r->len, wrap_width, r->byte_offset, i);
        } else {
            rip_add_display_line(state, r->data, r->len, i);
            state->display_lines[state->num_display_lines - 1].byte_offset = r->byte_offset;
        }
    }

    if (has_filter) {
        regfree(&filter_re);
    }

    rip_update_search_matches(state);
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  Read input (file or stdin)                                                 *
 * ─────────────────────────────────────────────────────────────────────────── */
static int load_from_fp(rip_state_t *state, FILE *fp) {
    state->stdin_eof = 1;
    state->stdin_nonblocking_set = 0;

    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);

    if (fp == stdin) {
        state->stdin_eof = 0;
        /* Set stdin (fd 0) to non-blocking */
        int flags = fcntl(STDIN_FILENO, F_GETFL);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
        state->stdin_nonblocking_set = 1;

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

    size_t out_len = len;
    char *converted = module_convert(state->filename, buf, len, &out_len);
    const char *src  = converted ? converted : buf;
    size_t src_len   = converted ? out_len   : len;

    if (src_len > 0) {
        state->raw_last_has_newline = (src[src_len - 1] == '\n' || src[src_len - 1] == '\r');
    } else {
        state->raw_last_has_newline = 1;
    }

    size_t start = 0;
    for (size_t i = 0; i <= src_len; i++) {
        if (i == src_len || src[i] == '\n' || src[i] == '\r') {
            rip_add_raw_line(state, src + start, i - start);
            state->raw_lines[state->num_raw_lines - 1].byte_offset = start;
            if (i < src_len && src[i] == '\r' && i+1 < src_len && src[i+1] == '\n') i++;
            start = i + 1;
        }
    }

    free(buf);
    if (converted) free(converted);
    return 0;
}

void rip_append_stream_data(rip_state_t *state, const char *buf, size_t len) {
    if (len == 0) return;

    size_t start = 0;
    int first_part = 1;

    for (size_t i = 0; i <= len; i++) {
        if (i == len || buf[i] == '\n' || buf[i] == '\r') {
            size_t segment_len = i - start;

            if (first_part && !state->raw_last_has_newline && state->num_raw_lines > 0) {
                /* Append to the last line */
                rip_line_t *last = &state->raw_lines[state->num_raw_lines - 1];
                last->data = realloc(last->data, last->len + segment_len + 1);
                memcpy(last->data + last->len, buf + start, segment_len);
                last->len += segment_len;
                last->data[last->len] = '\0';
            } else {
                /* Add as a new line */
                size_t old_size = state->file_size;
                rip_add_raw_line(state, buf + start, segment_len);
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

void rip_update_search_matches(rip_state_t *state) {
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

int rip_read_input(rip_state_t *state, const char *path) {
    if (!path || strcmp(path, "-") == 0) {
        if (isatty(STDIN_FILENO)) {
            fprintf(stderr, "Missing filename (\"rip --help\" for help)\n");
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
int rip_reload(rip_state_t *state) {
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

    rip_reflow_all(state);

    /* Restore position (clamped) */
    size_t max_top = 0;
    int text_rows = state->term_rows - 1;
    if (text_rows > 0 && state->num_display_lines > (size_t)text_rows)
        max_top = state->num_display_lines - text_rows;
    state->top_line = (saved_top <= max_top) ? saved_top : max_top;
    return 0;
}
