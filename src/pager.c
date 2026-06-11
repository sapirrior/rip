#include "some.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <poll.h>

/* ─────────────────────────────────────────────────────────────────────────── *
 *  Helpers                                                                    *
 * ─────────────────────────────────────────────────────────────────────────── */
static size_t get_max_top(some_state_t *state) {
    int rows = state->term_rows - 1;
    if (rows <= 0) return 0;
    if (state->num_display_lines > (size_t)rows)
        return state->num_display_lines - rows;
    return 0;
}

static void clamp_top(some_state_t *state) {
    size_t mx = get_max_top(state);
    if (state->top_line > mx) state->top_line = mx;
}

static void scroll_down(some_state_t *state, size_t n) {
    size_t mx = get_max_top(state);
    state->top_line = (state->top_line + n <= mx) ? state->top_line + n : mx;
}

static void scroll_up(some_state_t *state, size_t n) {
    state->top_line = (state->top_line >= n) ? state->top_line - n : 0;
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  Status bar                                                                 *
 * ─────────────────────────────────────────────────────────────────────────── */
static void render_status(some_state_t *state) {
    /* Move to last row, clear it, and reset formatting to prevent leaks */
    printf("\033[0m\033[%d;1H\033[K", state->term_rows);

    /* Temporary message takes priority and is displayed in reverse video */
    if (state->status_msg[0]) {
        printf("\033[7m %s \033[27m", state->status_msg);
        return;
    }

    int text_rows = state->term_rows - 1;
    size_t top  = state->top_line + 1;
    size_t bot  = state->top_line + text_rows;
    if (bot  > state->num_display_lines) bot  = state->num_display_lines;
    if (state->num_display_lines == 0)   top  = 0;

    /* Percentage */
    int pct = 0;
    if (state->num_display_lines > 0) {
        pct = (int)(100 * bot / state->num_display_lines);
        if (pct > 100) pct = 100;
    }

    /* Prefix string */
    char prefix_str[32] = "";
    if (state->num_prefix >= 0) {
        snprintf(prefix_str, sizeof(prefix_str), "%d", state->num_prefix);
    }

    /* Update match idx based on current top_line */
    if (state->num_search_matches > 0) {
        state->current_match_idx = -1;
        for (size_t i = 0; i < state->num_search_matches; i++) {
            if (state->search_matches[i] >= state->top_line) {
                state->current_match_idx = (int)i;
                break;
            }
        }
        if (state->current_match_idx == -1) {
            state->current_match_idx = (int)state->num_search_matches - 1;
        }
    }

    if (state->verbose_prompt) {
        /* Verbose prompt mode: filename  lines X-Y/Z  byte B/Total  (N%) */
        size_t byte_top = 0;
        if (state->num_display_lines > 0 && state->top_line < state->num_display_lines) {
            byte_top = state->display_lines[state->top_line].byte_offset;
        }

        const char *fname = state->filename ? state->filename : "stdin";

        /* Flag indicators */
        char flags[32] = "";
        {
            char tmp[32] = "[";
            if (state->wrap_enabled)          strcat(tmp, "W");
            if (state->search_case_insensitive) strcat(tmp, "I");
            if (state->line_numbers)          strcat(tmp, "#");
            if (!state->search_highlight)     strcat(tmp, "H");
            if (state->follow_mode)           strcat(tmp, "F");
            if (!state->syntax_highlighting)  strcat(tmp, "S");
            if (state->diff_enabled)          strcat(tmp, "D");
            if (state->filter_pattern[0])     strcat(tmp, "&");
            strcat(tmp, "]");
            if (strcmp(tmp, "[]") != 0) strcpy(flags, tmp);
        }

        printf("%s  lines %zu-%zu/%zu  byte %zu/%zu  (%d%%)",
               fname, top, bot, state->num_display_lines,
               byte_top, state->file_size, pct);

        if (bot >= state->num_display_lines && state->num_display_lines > 0) {
            printf(" \033[7m[END]\033[27m");
        }

        if (state->filter_pattern[0]) {
            printf("  [Filter: %s]", state->filter_pattern);
        }

        if (state->num_search_matches > 0) {
            printf("  [Match %d/%zu]", state->current_match_idx + 1, state->num_search_matches);
        }

        if (flags[0]) {
            printf("  %s", flags);
        }

        if (prefix_str[0]) {
            printf("  [%s]", prefix_str);
        }
    } else {
        /* Short prompt mode: : or (END) */
        if (state->filter_pattern[0]) {
            printf("[Filter: %s] ", state->filter_pattern);
        }

        if (state->num_search_matches > 0) {
            printf("[Match %d/%zu] ", state->current_match_idx + 1, state->num_search_matches);
        }

        if (bot >= state->num_display_lines && state->num_display_lines > 0) {
            printf("\033[7m[END]\033[27m");
        } else {
            printf(":");
        }

        if (prefix_str[0]) {
            printf("%s", prefix_str);
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  Print one line with search highlights (inline ANSI-safe)                  *
 * ─────────────────────────────────────────────────────────────────────────── */
static void print_line_safe(const char *line, regex_t *re, int has_re, int max_cols, int horiz) {
    size_t len = strlen(line);
    size_t offset = 0;
    int visual_col = 0;
    int printed_cols = 0;
    int hl_active = 0;

    regmatch_t matches[128];
    int num_matches = 0;
    if (has_re) {
        size_t search_off = 0;
        while (search_off < len && num_matches < 128) {
            regmatch_t m;
            if (regexec(re, line + search_off, 1, &m, 0) == 0) {
                size_t start_idx = search_off + m.rm_so;
                size_t end_idx = search_off + m.rm_eo;
                if (start_idx == end_idx) end_idx = start_idx + 1;
                matches[num_matches].rm_so = start_idx;
                matches[num_matches].rm_eo = end_idx;
                num_matches++;
                search_off = end_idx;
            } else {
                break;
            }
        }
    }

    while (offset < len && printed_cols < max_cols) {
        /* Check for ANSI escape sequences first to pass them through */
        if (line[offset] == '\033' && offset + 1 < len && line[offset + 1] == '[') {
            printf("\033[");
            offset += 2;
            while (offset < len && !(line[offset] >= 64 && line[offset] <= 126)) {
                putchar(line[offset++]);
            }
            if (offset < len) {
                putchar(line[offset++]);
            }
            continue;
        }

        /* Decode character */
        unsigned int ch;
        int clen = some_decode_utf8(line + offset, len - offset, &ch);
        int char_width = some_char_width(ch, visual_col);

        /* Determine highlighting state for the starting byte */
        int should_hl = 0;
        for (int i = 0; i < num_matches; i++) {
            if (offset >= (size_t)matches[i].rm_so && offset < (size_t)matches[i].rm_eo) {
                should_hl = 1;
                break;
            }
        }

        /* Print or skip columns */
        int cols_to_print = (visual_col + char_width) - horiz;
        if (cols_to_print > 0) {
            /* Manage reverse video highlighting */
            if (should_hl && !hl_active) {
                printf("\033[7m");
                hl_active = 1;
            } else if (!should_hl && hl_active) {
                printf("\033[27m");
                hl_active = 0;
            }

            if (visual_col >= horiz) {
                /* Full printing */
                if (ch == '\t') {
                    for (int i = 0; i < char_width && printed_cols < max_cols; i++) {
                        putchar(' ');
                        printed_cols++;
                    }
                } else if (ch < 32 || ch == 127) {
                    if (printed_cols < max_cols) { putchar('^'); printed_cols++; }
                    if (printed_cols < max_cols) { putchar(ch == 127 ? '?' : ch + '@'); printed_cols++; }
                } else {
                    /* Normal UTF-8 char */
                    if (char_width == 2 && printed_cols + 2 > max_cols) {
                        putchar(' ');
                        printed_cols++;
                    } else {
                        for (int i = 0; i < clen; i++) {
                            putchar(line[offset + i]);
                        }
                        printed_cols += char_width;
                    }
                }
            } else {
                /* Partial overlap printing */
                if (ch == '\t') {
                    if (cols_to_print > char_width) cols_to_print = char_width;
                    for (int i = 0; i < cols_to_print && printed_cols < max_cols; i++) {
                        putchar(' ');
                        printed_cols++;
                    }
                } else if (ch < 32 || ch == 127) {
                    if (cols_to_print == 2) {
                        if (printed_cols < max_cols) { putchar('^'); printed_cols++; }
                        if (printed_cols < max_cols) { putchar(ch == 127 ? '?' : ch + '@'); printed_cols++; }
                    } else if (cols_to_print == 1) {
                        if (printed_cols < max_cols) { putchar(ch == 127 ? '?' : ch + '@'); printed_cols++; }
                    }
                } else {
                    if (char_width == 2 && cols_to_print == 1) {
                        if (printed_cols < max_cols) { putchar(' '); printed_cols++; }
                    } else {
                        for (int i = 0; i < clen; i++) {
                            putchar(line[offset + i]);
                        }
                        printed_cols += char_width;
                    }
                }
            }
        }

        visual_col += char_width;
        offset += clen;
    }

    if (hl_active) {
        printf("\033[27m");
    }
    printf("\033[0m");
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  Main render                                                                *
 * ─────────────────────────────────────────────────────────────────────────── */
void some_load_diff_status(some_state_t *state) {
    if (state->diff_status) {
        free(state->diff_status);
        state->diff_status = NULL;
    }
    if (!state->diff_enabled || !state->filename || strcmp(state->filename, "stdin") == 0 || strcmp(state->filename, "Help Screen") == 0) {
        return;
    }

    state->diff_status = calloc(state->num_raw_lines, sizeof(char));
    if (!state->diff_status) return;

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "git diff -U0 -- '%s' 2>/dev/null", state->filename);
    FILE *fp = popen(cmd, "r");
    if (!fp) return;

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "@@ ", 3) == 0) {
            int old_len = 1;
            int new_start = 0, new_len = 1;
            char *plus = strchr(line, '+');
            if (plus) {
                char *minus = strchr(line, '-');
                if (minus) {
                    char *comma_min = strchr(minus, ',');
                    if (comma_min && comma_min < plus) {
                        old_len = atoi(comma_min + 1);
                    }
                }
                char *comma_plus = strchr(plus, ',');
                if (comma_plus) {
                    new_len = atoi(comma_plus + 1);
                }
                new_start = atoi(plus + 1);
            }

            if (new_start > 0) {
                if (new_len > 0) {
                    for (int l = 0; l < new_len; l++) {
                        size_t idx = (size_t)(new_start - 1 + l);
                        if (idx < state->num_raw_lines) {
                            if (old_len > 0) {
                                state->diff_status[idx] = 3; // MODIFIED
                            } else {
                                state->diff_status[idx] = 1; // ADDED
                            }
                        }
                    }
                } else {
                    size_t idx = (size_t)(new_start - 1);
                    if (idx < state->num_raw_lines) {
                        state->diff_status[idx] = 2; // DELETED_ABOVE
                    }
                }
            }
        }
    }
    pclose(fp);
}

int some_get_gutter_width(some_state_t *state) {
    int width = 0;
    if (state->line_numbers) {
        size_t total = state->num_raw_lines;
        size_t estimate = total * 2;
        if (estimate < 10) estimate = 10;
        int num_width = 1;
        while (estimate >= 10) {
            estimate /= 10;
            num_width++;
        }
        width += num_width + 2; /* space + pipe */
    }
    if (state->diff_enabled) {
        width += 2; /* diff char + space */
    }
    return width;
}

void some_render(some_state_t *state) {
    printf("\033[?25l\033[H"); /* hide cursor, go home */

    int text_rows = state->term_rows - 1;
    if (text_rows < 0) text_rows = 0;

    /* Compile search regex once per frame */
    regex_t re;
    int has_re = 0;
    if (state->search_pattern[0] && state->search_highlight) {
        int flags = REG_EXTENDED | (state->search_case_insensitive ? REG_ICASE : 0);
        has_re = (regcomp(&re, state->search_pattern, flags) == 0);
    }

    /* Compute line-number column width */
    int ln_width = some_get_gutter_width(state);

    int content_cols = state->term_cols - ln_width;
    if (content_cols < 1) content_cols = 1;

    for (int r = 0; r < text_rows; r++) {
        size_t idx = state->top_line + r;
        if (idx < state->num_display_lines) {
            const char *raw = state->display_lines[idx].data;
            size_t raw_len  = state->display_lines[idx].len;

            /* Line numbers */
            if (state->line_numbers) {
                if (idx > 0 && state->display_lines[idx].raw_line_idx == state->display_lines[idx - 1].raw_line_idx) {
                    int num_width = ln_width - (state->diff_enabled ? 2 : 0);
                    printf("\033[90m%*s│\033[0m", num_width - 1, "");
                } else {
                    int num_width = ln_width - (state->diff_enabled ? 2 : 0);
                    printf("\033[90m%*zu│\033[0m", num_width - 1, state->display_lines[idx].raw_line_idx + 1);
                }
            }

            /* Diff Gutter */
            if (state->diff_enabled) {
                size_t raw_idx = state->display_lines[idx].raw_line_idx;
                int diff_status = (state->diff_status && raw_idx < state->num_raw_lines) ? state->diff_status[raw_idx] : 0;
                if (diff_status == 1) {
                    printf("\033[1;38;2;63;185;80m┃\033[0m ");
                } else if (diff_status == 2) {
                    printf("\033[1;38;2;248;81;73m┃\033[0m ");
                } else if (diff_status == 3) {
                    printf("\033[1;38;2;210;168;255m┃\033[0m ");
                } else {
                    printf("  ");
                }
            }

            int horiz = state->wrap_enabled ? 0 : state->horiz_offset;
            int continues = 0;
            if (!state->wrap_enabled) {
                int vis_w = 0;
                size_t off = 0;
                while (off < raw_len) {
                    if (raw[off] == '\033' && off + 1 < raw_len && raw[off + 1] == '[') {
                        off += 2;
                        while (off < raw_len && !(raw[off] >= 64 && raw[off] <= 126)) {
                            off++;
                        }
                        if (off < raw_len) {
                            off++;
                        }
                        continue;
                    }
                    unsigned int ch;
                    int clen = some_decode_utf8(raw + off, raw_len - off, &ch);
                    vis_w += some_char_width(ch, vis_w);
                    off += clen;
                }
                if (horiz < vis_w && vis_w > horiz + content_cols) {
                    continues = 1;
                }
            }

            if (continues && content_cols > 1) {
                print_line_safe(raw, &re, has_re, content_cols - 1, horiz);
                printf("\033[7m$\033[27m");
            } else {
                print_line_safe(raw, &re, has_re, content_cols, horiz);
            }
        } else {
            if (state->line_numbers)
                printf("%*s", ln_width, "");
            printf("\033[90m~\033[0m");
        }

        printf("\033[0m\033[K\r\n");
    }

    if (has_re) regfree(&re);

    render_status(state);
    printf("\033[?25h");
    fflush(stdout);
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  Search                                                                     *
 * ─────────────────────────────────────────────────────────────────────────── */
static void do_search(some_state_t *state, int forward, int count) {
    if (state->num_search_matches == 0) {
        if (state->search_pattern[0]) {
            snprintf(state->status_msg, sizeof(state->status_msg), "Pattern not found: %s", state->search_pattern);
        }
        return;
    }
    if (count <= 0) count = 1;

    int idx = state->current_match_idx;
    if (idx < 0) {
        /* Find the first match at or after top_line */
        idx = 0;
        for (size_t i = 0; i < state->num_search_matches; i++) {
            if (state->search_matches[i] >= state->top_line) {
                idx = (int)i;
                break;
            }
        }
    }

    int wrapped = 0;
    if (forward) {
        /* Go forward count steps */
        int next_idx = idx + count;
        if (next_idx >= (int)state->num_search_matches) {
            next_idx = next_idx % (int)state->num_search_matches;
            wrapped = 1;
        }
        idx = next_idx;
    } else {
        /* Go backward count steps */
        int next_idx = idx - count;
        if (next_idx < 0) {
            next_idx = next_idx % (int)state->num_search_matches;
            if (next_idx < 0) next_idx += (int)state->num_search_matches;
            wrapped = 1;
        }
        idx = next_idx;
    }

    state->current_match_idx = idx;
    state->top_line = state->search_matches[idx];
    clamp_top(state);

    if (wrapped) {
        snprintf(state->status_msg, sizeof(state->status_msg), "(search wrapped)");
    }
}

static int validate_regex(const char *pattern, int case_insensitive, char *err_buf, size_t err_buf_len) {
    if (!pattern || !pattern[0]) return 0; /* empty regex is valid */
    regex_t re;
    int flags = REG_EXTENDED | REG_NOSUB | (case_insensitive ? REG_ICASE : 0);
    int err = regcomp(&re, pattern, flags);
    if (err != 0) {
        if (err_buf && err_buf_len > 0) {
            regerror(err, &re, err_buf, err_buf_len);
        }
        return -1;
    }
    regfree(&re);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  Search prompt                                                              *
 * ─────────────────────────────────────────────────────────────────────────── */
static void search_prompt(some_state_t *state, int forward) {
    char ch = forward ? '/' : '?';
    char buf[256] = {0};
    int  len = 0;
    int  hist_idx = state->search_history_count;
    char temp_typed[256] = {0};

    printf("\033[%d;1H\033[K%c", state->term_rows, ch);
    fflush(stdout);

    while (1) {
        int key = some_read_key(state);
        if (key == '\n' || key == '\r') break;
        if (key == 127 || key == '\b') {
            if (len > 0) {
                buf[--len] = '\0';
                printf("\033[%d;%dH\033[K", state->term_rows, len + 2);
                fflush(stdout);
            }
        } else if (key == '\033') {
            len = 0;
            break;
        } else if (key == KEY_ARROW_UP) {
            if (hist_idx > 0) {
                if (hist_idx == state->search_history_count) {
                    strcpy(temp_typed, buf);
                }
                hist_idx--;
                strcpy(buf, state->search_history[hist_idx]);
                len = strlen(buf);
                printf("\033[%d;1H\033[K%c%s", state->term_rows, ch, buf);
                fflush(stdout);
            }
        } else if (key == KEY_ARROW_DOWN) {
            if (hist_idx < state->search_history_count) {
                hist_idx++;
                if (hist_idx == state->search_history_count) {
                    strcpy(buf, temp_typed);
                } else {
                    strcpy(buf, state->search_history[hist_idx]);
                }
                len = strlen(buf);
                printf("\033[%d;1H\033[K%c%s", state->term_rows, ch, buf);
                fflush(stdout);
            }
        } else if (key == KEY_RESIZE) {
            printf("\033[2J");
            some_get_terminal_size(state);
            some_reflow_all(state);
            clamp_top(state);
            len = 0;
            break;
        } else if (key >= 32 && key < 127 && len < 255) {
            buf[len++] = (char)key;
            buf[len]   = '\0';
            putchar(key);
            fflush(stdout);
            hist_idx = state->search_history_count;
        }
    }

    if (len > 0) {
        char err_msg[256];
        if (validate_regex(buf, state->search_case_insensitive, err_msg, sizeof(err_msg)) != 0) {
            snprintf(state->status_msg, sizeof(state->status_msg), "Invalid regex: %s", err_msg);
            return;
        }
        memcpy(state->search_pattern, buf, len + 1);
        state->search_dir = forward ? 1 : -1;
        if (state->search_history_count > 0 && strcmp(state->search_history[state->search_history_count - 1], buf) == 0) {
            // Already last item
        } else {
            if (state->search_history_count < 16) {
                strcpy(state->search_history[state->search_history_count++], buf);
            } else {
                for (int i = 1; i < 16; i++) {
                    strcpy(state->search_history[i-1], state->search_history[i]);
                }
                strcpy(state->search_history[15], buf);
            }
        }
        some_update_search_matches(state);
        do_search(state, forward, 1);
    }
}

static void filter_prompt(some_state_t *state) {
    char buf[256] = {0};
    int  len = 0;
    int  hist_idx = state->filter_history_count;
    char temp_typed[256] = {0};

    printf("\033[%d;1H\033[K&", state->term_rows);
    fflush(stdout);

    while (1) {
        int key = some_read_key(state);
        if (key == '\n' || key == '\r') break;
        if (key == 127 || key == '\b') {
            if (len > 0) {
                buf[--len] = '\0';
                printf("\033[%d;%dH\033[K", state->term_rows, len + 2);
                fflush(stdout);
            }
        } else if (key == '\033') {
            return;
        } else if (key == KEY_ARROW_UP) {
            if (hist_idx > 0) {
                if (hist_idx == state->filter_history_count) {
                    strcpy(temp_typed, buf);
                }
                hist_idx--;
                strcpy(buf, state->filter_history[hist_idx]);
                len = strlen(buf);
                printf("\033[%d;1H\033[K&%s", state->term_rows, buf);
                fflush(stdout);
            }
        } else if (key == KEY_ARROW_DOWN) {
            if (hist_idx < state->filter_history_count) {
                hist_idx++;
                if (hist_idx == state->filter_history_count) {
                    strcpy(buf, temp_typed);
                } else {
                    strcpy(buf, state->filter_history[hist_idx]);
                }
                len = strlen(buf);
                printf("\033[%d;1H\033[K&%s", state->term_rows, buf);
                fflush(stdout);
            }
        } else if (key == KEY_RESIZE) {
            printf("\033[2J");
            some_get_terminal_size(state);
            some_reflow_all(state);
            clamp_top(state);
            return;
        } else if (key >= 32 && key < 127 && len < 255) {
            buf[len++] = (char)key;
            buf[len]   = '\0';
            putchar(key);
            fflush(stdout);
            hist_idx = state->filter_history_count;
        }
    }

    char err_msg[256];
    if (validate_regex(buf, state->search_case_insensitive, err_msg, sizeof(err_msg)) != 0) {
        snprintf(state->status_msg, sizeof(state->status_msg), "Invalid regex: %s", err_msg);
        return;
    }
    memcpy(state->filter_pattern, buf, len + 1);

    if (len > 0) {
        if (state->filter_history_count > 0 && strcmp(state->filter_history[state->filter_history_count - 1], buf) == 0) {
            // Already last item
        } else {
            if (state->filter_history_count < 16) {
                strcpy(state->filter_history[state->filter_history_count++], buf);
            } else {
                for (int i = 1; i < 16; i++) {
                    strcpy(state->filter_history[i-1], state->filter_history[i]);
                }
                strcpy(state->filter_history[15], buf);
            }
        }
    }

    some_reflow_all(state);
    state->top_line = 0;
    clamp_top(state);
    if (state->filter_pattern[0]) {
        snprintf(state->status_msg, sizeof(state->status_msg), "Filter active: %s", state->filter_pattern);
    } else {
        snprintf(state->status_msg, sizeof(state->status_msg), "Filter cleared.");
    }
}

static void show_info_overlay(some_state_t *state) {
    int w = 50;
    int h = 9;
    int start_row = (state->term_rows - h) / 2;
    int start_col = (state->term_cols - w) / 2;
    if (start_row < 1) start_row = 1;
    if (start_col < 1) start_col = 1;

    printf("\033[%d;%dH\033[1;36m┌", start_row, start_col);
    for (int i = 0; i < w - 2; i++) printf("─");
    printf("┐\033[0m");

    printf("\033[%d;%dH\033[1;36m│\033[0m\033[1;37m%-*s\033[1;36m│\033[0m", start_row + 1, start_col, w - 2, "                   FILE INFO");

    printf("\033[%d;%dH\033[1;36m├", start_row + 2, start_col);
    for (int i = 0; i < w - 2; i++) printf("─");
    printf("┤\033[0m");

    char buf[128];
    snprintf(buf, sizeof(buf), " File: %s", state->filename ? state->filename : "stdin");
    printf("\033[%d;%dH\033[1;36m│\033[0m%-*s\033[1;36m│\033[0m", start_row + 3, start_col, w - 2, buf);

    snprintf(buf, sizeof(buf), " Size: %zu bytes", state->file_size);
    printf("\033[%d;%dH\033[1;36m│\033[0m%-*s\033[1;36m│\033[0m", start_row + 4, start_col, w - 2, buf);

    snprintf(buf, sizeof(buf), " Lines: %zu raw / %zu display", state->num_raw_lines, state->num_display_lines);
    printf("\033[%d;%dH\033[1;36m│\033[0m%-*s\033[1;36m│\033[0m", start_row + 5, start_col, w - 2, buf);

    snprintf(buf, sizeof(buf), " Options: wrap=%s, case-insensitive=%s", state->wrap_enabled ? "ON" : "OFF", state->search_case_insensitive ? "ON" : "OFF");
    printf("\033[%d;%dH\033[1;36m│\033[0m%-*s\033[1;36m│\033[0m", start_row + 6, start_col, w - 2, buf);

    printf("\033[%d;%dH\033[1;36m│\033[0m\033[5m%-*s\033[0m\033[1;36m│\033[0m", start_row + 7, start_col, w - 2, "            [Press any key to close]");

    printf("\033[%d;%dH\033[1;36m└", start_row + 8, start_col);
    for (int i = 0; i < w - 2; i++) printf("─");
    printf("┘\033[0m");
    fflush(stdout);

    some_read_key(state);
    some_render(state);
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  Help screen                                                                *
 * ─────────────────────────────────────────────────────────────────────────── */
static void show_help(some_state_t *state) {
    some_state_t help_state;
    some_init_state(&help_state);

    help_state.tty_fd = state->tty_fd;
    help_state.orig_termios = state->orig_termios;
    help_state.raw_mode_enabled = state->raw_mode_enabled;
    help_state.term_rows = state->term_rows;
    help_state.term_cols = state->term_cols;

    help_state.filename = "Help Screen";
    help_state.is_stdin = 0;

    const char *help_lines[] = {
        "  \033[1;38;2;210;168;255msome — Scroll Or More Easily (less++)\033[0m",
        "  \033[38;2;139;148;158m====================================\033[0m",
        "",
        "  \033[1;38;2;255;123;114m[ NAVIGATION ]\033[0m",
        "    \033[1;38;2;121;192;255mj\033[0m / \033[1;38;2;121;192;255m↓\033[0m / \033[1;38;2;121;192;255mEnter\033[0m      Scroll down 1 line (prefix N repeats)",
        "    \033[1;38;2;121;192;255mk\033[0m / \033[1;38;2;121;192;255m↑\033[0m            Scroll up 1 line (prefix N repeats)",
        "    \033[1;38;2;121;192;255mSpace\033[0m / \033[1;38;2;121;192;255mf\033[0m / \033[1;38;2;121;192;255mPgDn\033[0m  Scroll down 1 page (prefix N repeats)",
        "    \033[1;38;2;121;192;255mb\033[0m / \033[1;38;2;121;192;255mPgUp\033[0m         Scroll up 1 page (prefix N repeats)",
        "    \033[1;38;2;121;192;255md\033[0m                  Scroll down half page (prefix N repeats)",
        "    \033[1;38;2;121;192;255mu\033[0m                  Scroll up half page (prefix N repeats)",
        "    \033[1;38;2;121;192;255mg\033[0m / \033[1;38;2;121;192;255m<\033[0m / \033[1;38;2;121;192;255mHome\033[0m       Go to line N (default: top)",
        "    \033[1;38;2;121;192;255mG\033[0m / \033[1;38;2;121;192;255m>\033[0m / \033[1;38;2;121;192;255mEnd\033[0m        Go to line N (default: bottom)",
        "    \033[1;38;2;121;192;255mNp\033[0m / \033[1;38;2;121;192;255mN%\033[0m            Go to N% of the file",
        "    \033[1;38;2;121;192;255mh\033[0m / \033[1;38;2;121;192;255ml\033[0m / \033[1;38;2;121;192;255m←\033[0m / \033[1;38;2;121;192;255m→\033[0m      Scroll 4 columns left/right (chop mode, prefix N)",
        "    \033[1;38;2;121;192;255m(\033[0m / \033[1;38;2;121;192;255m)\033[0m            Scroll half-page left/right (chop mode, prefix N)",
        "",
        "  \033[1;38;2;255;123;114m[ SEARCH & FILTER ]\033[0m",
        "    \033[1;38;2;121;192;255m/\033[0mpattern           Search forward (prefix N repeats)",
        "    \033[1;38;2;121;192;255m?\033[0mpattern           Search backward (prefix N repeats)",
        "    \033[1;38;2;121;192;255m&\033[0mpattern           Filter lines by regex pattern (Enter to clear)",
        "    \033[1;38;2;121;192;255mn\033[0m                  Next match (prefix N repeats)",
        "    \033[1;38;2;121;192;255mN\033[0m                  Previous match (prefix N repeats)",
        "    \033[1;38;2;121;192;255mi\033[0m                  Toggle case-insensitive matching",
        "    \033[1;38;2;121;192;255mH\033[0m                  Toggle search highlighting",
        "    \033[1;38;2;121;192;255mESC\033[0m                Clear search pattern, filters, and highlights",
        "",
        "  \033[1;38;2;255;123;114m[ DISPLAY OPTIONS ]\033[0m",
        "    \033[1;38;2;121;192;255mw\033[0m                  Toggle smart word wrap (preserving indentation)",
        "    \033[1;38;2;121;192;255mL\033[0m                  Toggle line numbers",
        "    \033[1;38;2;121;192;255ms\033[0m                  Toggle AST-based syntax highlighting",
        "    \033[1;38;2;121;192;255mc\033[0m                  Toggle Git diff gutter (added/deleted/modified indicators)",
        "    \033[1;38;2;121;192;255mM\033[0m                  Toggle verbose/short status bar prompt",
        "    \033[1;38;2;121;192;255m=\033[0m                  Show detailed file statistics / information",
        "    \033[1;38;2;121;192;255mr\033[0m / \033[1;38;2;121;192;255m^L\033[0m             Repaint the terminal window",
        "    \033[1;38;2;121;192;255mR\033[0m                  Reload the current file from disk",
        "",
        "  \033[1;38;2;255;123;114m[ SESSION & UTILITIES ]\033[0m",
        "    \033[1;38;2;121;192;255mv\033[0m                  Open current line in external editor ($VISUAL or $EDITOR)",
        "    \033[1;38;2;121;192;255m!cmd\033[0m               Execute a shell command inside a subshell",
        "    \033[1;38;2;121;192;255mF\033[0m                  Enter real-time follow mode (tail -f style). 'q' to exit.",
        "    \033[1;38;2;121;192;255mCtrl+H\033[0m             Open this interactive help screen",
        "    \033[1;38;2;121;192;255mq\033[0m / \033[1;38;2;121;192;255mQ\033[0m              Quit the current pager view / help menu",
        "",
        "  \033[1;38;2;255;123;114m[ STATUS FLAGS INDICATION ]\033[0m",
        "    \033[1;38;2;165;214;255m[W]\033[0m Word Wrap       \033[1;38;2;165;214;255m[I]\033[0m Case-Insensitive  \033[1;38;2;165;214;255m[#]\033[0m Line Numbers  \033[1;38;2;165;214;255m[H]\033[0m Search Highlights Off",
        "    \033[1;38;2;165;214;255m[F]\033[0m Follow Mode     \033[1;38;2;165;214;255m[S]\033[0m Syntax Highlighting Off  \033[1;38;2;165;214;255m[D]\033[0m Git Diff Active",
    };

    size_t num_lines = sizeof(help_lines) / sizeof(help_lines[0]);
    for (size_t i = 0; i < num_lines; i++) {
        some_add_raw_line(&help_state, help_lines[i], strlen(help_lines[i]));
        help_state.raw_lines[help_state.num_raw_lines - 1].byte_offset = help_state.file_size;
        help_state.file_size += strlen(help_lines[i]) + 1;
    }

    some_run(&help_state);

    help_state.raw_mode_enabled = 0;
    some_free_state(&help_state);
}

static size_t get_current_raw_line_num(some_state_t *state) {
    if (state->num_display_lines == 0 || state->top_line >= state->num_display_lines) {
        return 1;
    }
    size_t target_offset = state->display_lines[state->top_line].byte_offset;
    for (size_t i = 0; i < state->num_raw_lines; i++) {
        if (state->raw_lines[i].byte_offset >= target_offset) {
            return i + 1;
        }
    }
    return state->num_raw_lines > 0 ? state->num_raw_lines : 1;
}

static void open_in_editor(some_state_t *state) {
    if (!state->filename || strcmp(state->filename, "stdin") == 0 || strcmp(state->filename, "Help Screen") == 0) {
        snprintf(state->status_msg, sizeof(state->status_msg), "Cannot edit %s.", state->filename ? state->filename : "stdin");
        return;
    }

    size_t line_num = get_current_raw_line_num(state);
    const char *editor = getenv("VISUAL");
    if (!editor || !editor[0]) {
        editor = getenv("EDITOR");
    }

    some_disable_raw_mode(state);
    printf("\n");
    fflush(stdout);

    int ok = 0;
    char cmd[1024];

    if (editor && editor[0]) {
        snprintf(cmd, sizeof(cmd), "%s +%zu %s", editor, line_num, state->filename);
        int status = system(cmd);
        (void)status;
        ok = 1;
    } else {
        /* Try nano */
        snprintf(cmd, sizeof(cmd), "nano +%zu %s", line_num, state->filename);
        int status = system(cmd);
        if (status != -1 && WEXITSTATUS(status) != 127) {
            ok = 1;
        } else {
            /* Try vim */
            snprintf(cmd, sizeof(cmd), "vim +%zu %s", line_num, state->filename);
            status = system(cmd);
            if (status != -1 && WEXITSTATUS(status) != 127) {
                ok = 1;
            } else {
                /* Try vi */
                snprintf(cmd, sizeof(cmd), "vi +%zu %s", line_num, state->filename);
                status = system(cmd);
                if (status != -1 && WEXITSTATUS(status) != 127) {
                    ok = 1;
                }
            }
        }
    }

    some_enable_raw_mode(state);
    printf("\033[2J"); /* Clear editor screen remnants */
    some_get_terminal_size(state);
    some_reload(state);

    if (!ok) {
        snprintf(state->status_msg, sizeof(state->status_msg),
                 "No editor found. Set $EDITOR environment variable.");
    }
}

static void shell_command_prompt(some_state_t *state) {
    char buf[512] = {0};
    int len = 0;
    printf("\033[%d;1H\033[K!", state->term_rows);
    fflush(stdout);

    while (1) {
        int key = some_read_key(state);
        if (key == '\n' || key == '\r') break;
        if (key == 127 || key == '\b') {
            if (len > 0) {
                buf[--len] = '\0';
                printf("\033[%d;%dH\033[K", state->term_rows, len + 2);
                fflush(stdout);
            }
        } else if (key == '\033') {
            len = 0;
            break;
        } else if (key == KEY_RESIZE) {
            printf("\033[2J");
            some_get_terminal_size(state);
            some_reflow_all(state);
            clamp_top(state);
            len = 0;
            break;
        } else if (key >= 32 && key < 127 && len < 511) {
            buf[len++] = (char)key;
            buf[len]   = '\0';
            putchar(key);
            fflush(stdout);
        }
    }

    if (len > 0) {
        some_disable_raw_mode(state);
        printf("\n");
        fflush(stdout);

        int status = system(buf);
        (void)status;

        printf("\n[Press Enter to return to some]\n");
        fflush(stdout);

        while (1) {
            char c;
            if (read(state->tty_fd, &c, 1) == 1 && (c == '\n' || c == '\r')) {
                break;
            }
        }

        some_enable_raw_mode(state);
        printf("\033[2J"); /* Clear command output remnants */
        some_get_terminal_size(state);
        some_reflow_all(state);
    }
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  Follow mode                                                                *
 * ─────────────────────────────────────────────────────────────────────────── */
static void enter_follow_mode(some_state_t *state) {
    if (!state->filename || strcmp(state->filename, "stdin") == 0 || strcmp(state->filename, "Help Screen") == 0) {
        snprintf(state->status_msg, sizeof(state->status_msg),
                 "Follow mode not available for %s", state->filename ? state->filename : "stdin");
        return;
    }

    state->follow_mode = 1;
    /* Set tty_fd to non-blocking so we can poll */
    int saved_flags = fcntl(state->tty_fd, F_GETFL);
    fcntl(state->tty_fd, F_SETFL, saved_flags | O_NONBLOCK);

    FILE *fp = fopen(state->filename, "r");
    if (!fp) {
        state->follow_mode = 0;
        fcntl(state->tty_fd, F_SETFL, saved_flags);
        return;
    }
    /* seek to end */
    fseek(fp, (long)state->file_size, SEEK_SET);

    snprintf(state->status_msg, sizeof(state->status_msg),
             "Follow mode — press q or ^C to exit");

    while (state->follow_mode) {
        /* Read any new lines appended */
        char line_buf[4096];
        int got_new = 0;
        while (fgets(line_buf, sizeof(line_buf), fp)) {
            size_t ll = strlen(line_buf);
            if (ll > 0 && line_buf[ll-1] == '\n') { line_buf[--ll] = '\0'; }
            if (ll > 0 && line_buf[ll-1] == '\r') { line_buf[--ll] = '\0'; }
            size_t old_size = state->file_size;
            some_add_raw_line(state, line_buf, ll);
            state->raw_lines[state->num_raw_lines - 1].byte_offset = old_size;
            state->file_size += ll + 1;
            got_new = 1;
        }
        if (got_new) {
            some_reflow_all(state);
            state->top_line = get_max_top(state);
            some_render(state);
        }

        /* Check for keypress (non-blocking) */
        char c;
        ssize_t nr = read(state->tty_fd, &c, 1);
        if (nr == 1) {
            if (c == 'q' || c == 'Q' || c == 3 /* ^C */) {
                state->follow_mode = 0;
            }
        }

        usleep(200000); /* poll every 200ms */
    }

    fclose(fp);
    fcntl(state->tty_fd, F_SETFL, saved_flags);
    state->status_msg[0] = '\0';
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  Main pager event loop                                                      *
 * ─────────────────────────────────────────────────────────────────────────── */
void some_run(some_state_t *state) {
    some_get_terminal_size(state);
    some_reflow_all(state);
    some_update_search_matches(state);
    some_render(state);

    while (1) {
        struct pollfd fds[2];
        int nfds = 0;

        fds[nfds].fd = state->tty_fd;
        fds[nfds].events = POLLIN;
        int tty_idx = nfds++;

        int stdin_idx = -1;
        if (state->is_stdin && !state->stdin_eof) {
            fds[nfds].fd = STDIN_FILENO;
            fds[nfds].events = POLLIN;
            stdin_idx = nfds++;
        }

        int ret = poll(fds, nfds, -1);
        if (ret < 0) {
            if (errno == EINTR) {
                printf("\033[2J");
                some_get_terminal_size(state);
                some_reflow_all(state);
                some_update_search_matches(state);
                clamp_top(state);
                some_render(state);
                continue;
            }
            break;
        }

        /* 1. Handle stdin stream input */
        if (stdin_idx != -1 && (fds[stdin_idx].revents & (POLLIN | POLLHUP | POLLERR))) {
            char buf[4096];
            ssize_t nr = read(STDIN_FILENO, buf, sizeof(buf));
            if (nr > 0) {
                some_append_stream_data(state, buf, nr);
                some_reflow_all(state);
                /* Note: some_reflow_all calls some_update_search_matches internally */
                some_render(state);
            } else if (nr == 0) {
                state->stdin_eof = 1;
            } else {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    state->stdin_eof = 1;
                }
            }
        }

        /* 2. Handle keyboard input */
        if (fds[tty_idx].revents & POLLIN) {
            int key = some_read_key(state);
            if (key == -1) break;
            if (key == KEY_RESIZE) {
                printf("\033[2J");
                some_get_terminal_size(state);
                some_reflow_all(state);
                clamp_top(state);
                some_render(state);
                continue;
            }

            /* clear status message on any key */
            state->status_msg[0] = '\0';

            /* accumulate numeric prefix */
            if (key >= '1' && key <= '9') {
                if (state->num_prefix < 0) state->num_prefix = 0;
                state->num_prefix = state->num_prefix * 10 + (key - '0');
                some_render(state);
                continue;
            }
            if (key == '0' && state->num_prefix >= 0) {
                state->num_prefix = state->num_prefix * 10;
                some_render(state);
                continue;
            }

            /* consume prefix */
            int has_prefix = (state->num_prefix >= 0);
            int N = (state->num_prefix > 0) ? state->num_prefix : 1;
            state->num_prefix = -1;

            int half = (state->term_rows - 1) / 2;
            if (half < 1) half = 1;
            int page = (state->term_rows - 2);
            if (page < 1) page = 1;

            switch (key) {

            /* ── quit ── */
            case 'q': case 'Q':
                return;

            /* ── repaint ── */
            case 'r': case '\f': /* ^L */
                printf("\033[2J");
                break; /* just fall through to re-render */

            /* ── reload ── */
            case 'R':
                if (strcmp(state->filename, "Help Screen") == 0) break;
                some_reload(state);
                snprintf(state->status_msg, sizeof(state->status_msg), "File reloaded.");
                break;

            /* ── vertical navigation ── */
            case 'j': case '\n': case '\r': case KEY_ARROW_DOWN:
                scroll_down(state, N);
                break;
            case 'k': case KEY_ARROW_UP:
                scroll_up(state, N);
                break;
            case ' ': case 'f': case KEY_PAGE_DOWN:
                scroll_down(state, N * page);
                break;
            case 'b': case KEY_PAGE_UP:
                scroll_up(state, N * page);
                break;
            case 'd':
                scroll_down(state, N * half);
                break;
            case 'u':
                scroll_up(state, N * half);
                break;

            /* ── go to line / percent ── */
            case 'g': case '<': case KEY_HOME:
                if (has_prefix)
                    state->top_line = (size_t)(N - 1);
                else
                    state->top_line = 0;
                clamp_top(state);
                break;
            case 'G': case '>': case KEY_END:
                if (has_prefix)
                    state->top_line = (size_t)(N - 1);
                else
                    state->top_line = get_max_top(state);
                clamp_top(state);
                break;
            case 'p': case '%': {
                /* Np or N% → go to N% of file */
                if (N >= 0 && N <= 100) {
                    size_t target = (size_t)((double)state->num_display_lines * N / 100.0);
                    state->top_line = target;
                    clamp_top(state);
                }
                break;
            }

            /* ── horizontal scroll (chop mode only) ── */
            case 'l': case KEY_ARROW_RIGHT:
                if (!state->wrap_enabled) {
                    int ln_width = some_get_gutter_width(state);
                    int content_cols = state->term_cols - ln_width;
                    if (content_cols < 1) content_cols = 1;
                    int max_offset = state->max_line_width - content_cols;
                    if (max_offset < 0) max_offset = 0;

                    state->horiz_offset += N * 4;
                    if (state->horiz_offset > max_offset) state->horiz_offset = max_offset;
                }
                break;
            case 'h': case KEY_ARROW_LEFT:
                if (!state->wrap_enabled) {
                    state->horiz_offset -= N * 4;
                    if (state->horiz_offset < 0) state->horiz_offset = 0;
                }
                break;
            case ')':
                if (!state->wrap_enabled) {
                    int ln_width = some_get_gutter_width(state);
                    int content_cols = state->term_cols - ln_width;
                    if (content_cols < 1) content_cols = 1;
                    int max_offset = state->max_line_width - content_cols;
                    if (max_offset < 0) max_offset = 0;

                    state->horiz_offset += N * (state->term_cols / 2);
                    if (state->horiz_offset > max_offset) state->horiz_offset = max_offset;
                }
                break;
            case '(':
                if (!state->wrap_enabled) {
                    state->horiz_offset -= N * (state->term_cols / 2);
                    if (state->horiz_offset < 0) state->horiz_offset = 0;
                }
                break;

            /* ── search / filter ── */
            case '/':
                search_prompt(state, 1);
                break;
            case '?':
                search_prompt(state, 0);
                break;
            case '&':
                filter_prompt(state);
                break;
            case 'n':
                do_search(state, state->search_dir == 1, N);
                break;
            case 'N':
                do_search(state, state->search_dir != 1, N);
                break;

            /* ── Escape clears search/filter pattern ── */
            case '\033':
                state->search_pattern[0] = '\0';
                state->filter_pattern[0] = '\0';
                some_reflow_all(state);
                some_update_search_matches(state);
                snprintf(state->status_msg, sizeof(state->status_msg), "Search and filter patterns cleared.");
                break;

            /* ── edit current file ── */
            case 'v':
                open_in_editor(state);
                break;

            /* ── shell command execution ── */
            case '!':
                shell_command_prompt(state);
                break;

            /* ── search options ── */
            case 'i':
                state->search_case_insensitive ^= 1;
                some_update_search_matches(state);
                snprintf(state->status_msg, sizeof(state->status_msg),
                         "Case-insensitive search: %s",
                         state->search_case_insensitive ? "ON" : "OFF");
                break;
            case 'H':
                state->search_highlight ^= 1;
                snprintf(state->status_msg, sizeof(state->status_msg),
                         "Search highlights: %s",
                         state->search_highlight ? "ON" : "OFF");
                break;

             /* ── display toggles ── */
            case 'c': case 'C': {
                state->diff_enabled ^= 1;
                if (strcmp(state->filename, "stdin") == 0 || strcmp(state->filename, "Help Screen") == 0) {
                    snprintf(state->status_msg, sizeof(state->status_msg),
                             "Cannot toggle diff on stdin or help screen.");
                } else {
                    if (state->diff_enabled) {
                        some_load_diff_status(state);
                    } else {
                        if (state->diff_status) {
                            free(state->diff_status);
                            state->diff_status = NULL;
                        }
                    }
                    some_reflow_all(state);
                    clamp_top(state);
                    snprintf(state->status_msg, sizeof(state->status_msg),
                             "Diff gutter: %s", state->diff_enabled ? "ON" : "OFF");
                }
                break;
            }
            case 's': {
                state->syntax_highlighting ^= 1;
                if (strcmp(state->filename, "stdin") == 0) {
                    snprintf(state->status_msg, sizeof(state->status_msg),
                             "Cannot toggle syntax highlighting on stdin.");
                } else {
                    some_reload(state);
                    snprintf(state->status_msg, sizeof(state->status_msg),
                             "Syntax highlighting: %s", state->syntax_highlighting ? "ON" : "OFF");
                }
                break;
            }
            case 'w': case 'W': {
                state->wrap_enabled ^= 1;
                state->horiz_offset = 0;
                some_reflow_all(state);
                clamp_top(state);
                snprintf(state->status_msg, sizeof(state->status_msg),
                         "Word wrap: %s", state->wrap_enabled ? "ON" : "OFF");
                break;
            }
            case 'L':
                state->line_numbers ^= 1;
                some_reflow_all(state);
                clamp_top(state);
                snprintf(state->status_msg, sizeof(state->status_msg),
                         "Line numbers: %s", state->line_numbers ? "ON" : "OFF");
                break;
            case 'M':
                state->verbose_prompt ^= 1;
                snprintf(state->status_msg, sizeof(state->status_msg),
                         "Verbose prompt: %s", state->verbose_prompt ? "ON" : "OFF");
                break;

            /* ── file info ── */
            case '=': {
                show_info_overlay(state);
                break;
            }

            /* ── follow mode ── */
            case 'F':
                enter_follow_mode(state);
                break;

            /* ── help ── */
            case 'H' & 0x1f: /* Ctrl+H help */
                if (strcmp(state->filename, "Help Screen") == 0) {
                    return; /* exit help pager loop */
                }
                show_help(state);
                printf("\033[2J");
                break;

            /* ── terminal resize ── */
            case KEY_RESIZE:
                printf("\033[2J");
                some_get_terminal_size(state);
                some_reflow_all(state);
                clamp_top(state);
                break;

            default:
                break;
            }

            some_render(state);
        }
    }
}
