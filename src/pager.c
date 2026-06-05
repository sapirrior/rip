#include "rip.h"
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
static size_t get_max_top(rip_state_t *state) {
    int rows = state->term_rows - 1;
    if (rows <= 0) return 0;
    if (state->num_display_lines > (size_t)rows)
        return state->num_display_lines - rows;
    return 0;
}

static void clamp_top(rip_state_t *state) {
    size_t mx = get_max_top(state);
    if (state->top_line > mx) state->top_line = mx;
}

static void scroll_down(rip_state_t *state, size_t n) {
    size_t mx = get_max_top(state);
    state->top_line = (state->top_line + n <= mx) ? state->top_line + n : mx;
}

static void scroll_up(rip_state_t *state, size_t n) {
    state->top_line = (state->top_line >= n) ? state->top_line - n : 0;
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  Status bar                                                                 *
 * ─────────────────────────────────────────────────────────────────────────── */
static void render_status(rip_state_t *state) {
    /* Move to last row, clear it */
    printf("\033[%d;1H\033[K", state->term_rows);

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
            if (state->filter_pattern[0])     strcat(tmp, "&");
            strcat(tmp, "]");
            if (strcmp(tmp, "[]") != 0) strcpy(flags, tmp);
        }

        printf("%s  lines %zu-%zu/%zu  byte %zu/%zu  (%d%%)",
               fname, top, bot, state->num_display_lines,
               byte_top, state->file_size, pct);

        if (bot >= state->num_display_lines && state->num_display_lines > 0) {
            printf(" (END)");
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
            printf("(END)");
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
    regoff_t ms = -1, me = -1;

    while (offset < len && printed_cols < max_cols) {
        /* Check if we need to query search match */
        if (has_re && (ms == -1 || offset >= (size_t)me)) {
            regmatch_t m;
            if (regexec(re, line + offset, 1, &m, 0) == 0) {
                ms = offset + m.rm_so;
                me = offset + m.rm_eo;
                /* If empty match, force at least 1 byte to prevent infinite loop */
                if (ms == me) me = ms + 1;
            } else {
                ms = -1;
                me = -1;
            }
        }

        /* Decode character */
        unsigned int ch;
        int clen = rip_decode_utf8(line + offset, len - offset, &ch);
        int char_width = rip_char_width(ch, visual_col);

        /* Determine highlighting state for the starting byte */
        int should_hl = (ms != -1 && offset >= (size_t)ms && offset < (size_t)me);

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
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  Main render                                                                *
 * ─────────────────────────────────────────────────────────────────────────── */
int rip_get_gutter_width(rip_state_t *state) {
    if (!state->line_numbers) return 0;
    size_t total = state->num_raw_lines;
    size_t estimate = total * 2;
    if (estimate < 10) estimate = 10;
    int width = 1;
    while (estimate >= 10) {
        estimate /= 10;
        width++;
    }
    return width + 2; /* space + pipe */
}

void rip_render(rip_state_t *state) {
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
    int ln_width = rip_get_gutter_width(state);

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
                    printf("\033[90m%*s│\033[0m", ln_width - 1, "");
                } else {
                    printf("\033[90m%*zu│\033[0m", ln_width - 1, state->display_lines[idx].raw_line_idx + 1);
                }
            }

            int horiz = state->wrap_enabled ? 0 : state->horiz_offset;
            int continues = 0;
            if (!state->wrap_enabled) {
                int vis_w = 0;
                size_t off = 0;
                while (off < raw_len) {
                    unsigned int ch;
                    off += rip_decode_utf8(raw + off, raw_len - off, &ch);
                    vis_w += rip_char_width(ch, vis_w);
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

        printf("\033[K\r\n");
    }

    if (has_re) regfree(&re);

    render_status(state);
    printf("\033[?25h");
    fflush(stdout);
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  Search                                                                     *
 * ─────────────────────────────────────────────────────────────────────────── */
static void do_search(rip_state_t *state, int forward, int count) {
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

/* ─────────────────────────────────────────────────────────────────────────── *
 *  Search prompt                                                              *
 * ─────────────────────────────────────────────────────────────────────────── */
static void search_prompt(rip_state_t *state, int forward) {
    char ch = forward ? '/' : '?';
    char buf[256] = {0};
    int  len = 0;

    printf("\033[%d;1H\033[K%c", state->term_rows, ch);
    fflush(stdout);

    while (1) {
        int key = rip_read_key(state);
        if (key == '\n' || key == '\r') break;
        if (key == 127 || key == '\b') {
            if (len > 0) {
                buf[--len] = '\0';
                printf("\033[%d;%dH\033[K", state->term_rows, len + 2);
                fflush(stdout);
            }
        } else if (key == '\033' || key == KEY_RESIZE) {
            len = 0;
            break;
        } else if (key >= 32 && key < 127 && len < 255) {
            buf[len++] = (char)key;
            buf[len]   = '\0';
            putchar(key);
            fflush(stdout);
        }
    }

    if (len > 0) {
        memcpy(state->search_pattern, buf, len + 1);
        state->search_dir = forward ? 1 : -1;
        rip_update_search_matches(state);
        do_search(state, forward, 1);
    }
}

static void filter_prompt(rip_state_t *state) {
    char buf[256] = {0};
    int  len = 0;

    printf("\033[%d;1H\033[K&", state->term_rows);
    fflush(stdout);

    while (1) {
        int key = rip_read_key(state);
        if (key == '\n' || key == '\r') break;
        if (key == 127 || key == '\b') {
            if (len > 0) {
                buf[--len] = '\0';
                printf("\033[%d;%dH\033[K", state->term_rows, len + 2);
                fflush(stdout);
            }
        } else if (key == '\033' || key == KEY_RESIZE) {
            return;
        } else if (key >= 32 && key < 127 && len < 255) {
            buf[len++] = (char)key;
            buf[len]   = '\0';
            putchar(key);
            fflush(stdout);
        }
    }

    /* Update filter pattern and reflow */
    memcpy(state->filter_pattern, buf, len + 1);
    rip_reflow_all(state);
    state->top_line = 0;
    clamp_top(state);
    if (state->filter_pattern[0]) {
        snprintf(state->status_msg, sizeof(state->status_msg), "Filter active: %s", state->filter_pattern);
    } else {
        snprintf(state->status_msg, sizeof(state->status_msg), "Filter cleared.");
    }
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  Help screen                                                                *
 * ─────────────────────────────────────────────────────────────────────────── */
static void show_help(rip_state_t *state) {
    printf("\033[H\033[2J"); /* clear screen */
    printf("\033[1mrip\033[0m — Read In Pager  (less++)\n\n");

    printf("\033[1mNAVIGATION\033[0m\n");
    printf("  j / ↓ / Enter    Scroll down 1 line\n");
    printf("  k / ↑            Scroll up 1 line\n");
    printf("  Space / f / PgDn Scroll down 1 page\n");
    printf("  b / PgUp         Scroll up 1 page\n");
    printf("  d                Scroll down half page\n");
    printf("  u                Scroll up half page\n");
    printf("  g / < / Home     Go to line N (default: top)\n");
    printf("  G / > / End      Go to line N (default: bottom)\n");
    printf("  Np / N%%          Go to N%% into file\n");
    printf("  ← / →            Scroll 4 columns left/right\n");
    printf("  ( / )            Scroll half-page left/right\n\n");

    printf("\033[1mSEARCH / FILTER\033[0m\n");
    printf("  /pattern         Search forward (prefix N repeats)\n");
    printf("  ?pattern         Search backward (prefix N repeats)\n");
    printf("  &pattern         Filter lines by regex pattern (Enter to clear)\n");
    printf("  n                Next match (prefix N repeats)\n");
    printf("  N                Previous match (prefix N repeats)\n");
    printf("  i                Toggle case-insensitive\n");
    printf("  H                Toggle search highlights\n");
    printf("  ESC              Clear search pattern & highlights\n\n");

    printf("\033[1mDISPLAY\033[0m\n");
    printf("  w                Toggle word wrap / chop\n");
    printf("  L                Toggle line numbers\n");
    printf("  M                Toggle verbose status bar\n");
    printf("  =                Show detailed file info\n");
    printf("  r                Repaint screen\n");
    printf("  R                Reload file from disk\n\n");

    printf("\033[1mSESSION\033[0m\n");
    printf("  v                Open editor at current line\n");
    printf("  !cmd             Run shell command\n");
    printf("  F                Follow mode (tail -f). q to exit.\n");
    printf("  h                This help screen\n");
    printf("  q / Q            Quit\n\n");

    printf("Status bar flags:  \033[7m[W]\033[0m wrap  \033[7m[I]\033[0m icase  \033[7m[#]\033[0m line-nums  \033[7m[H]\033[0m highlights off  \033[7m[F]\033[0m follow\n");
    printf("\n\033[90mPress any key to return...\033[0m");
    fflush(stdout);
    rip_read_key(state);
}

static size_t get_current_raw_line_num(rip_state_t *state) {
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

static void open_in_editor(rip_state_t *state) {
    if (!state->filename || strcmp(state->filename, "stdin") == 0) {
        snprintf(state->status_msg, sizeof(state->status_msg), "Cannot edit stdin.");
        return;
    }

    size_t line_num = get_current_raw_line_num(state);
    const char *editor = getenv("VISUAL");
    if (!editor || !editor[0]) {
        editor = getenv("EDITOR");
    }

    rip_disable_raw_mode(state);
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

    rip_enable_raw_mode(state);
    rip_get_terminal_size(state);
    rip_reload(state);

    if (!ok) {
        snprintf(state->status_msg, sizeof(state->status_msg),
                 "No editor found. Set $EDITOR environment variable.");
    }
}

static void shell_command_prompt(rip_state_t *state) {
    char buf[512] = {0};
    int len = 0;
    printf("\033[%d;1H\033[K!", state->term_rows);
    fflush(stdout);

    while (1) {
        int key = rip_read_key(state);
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
        } else if (key >= 32 && key < 127 && len < 511) {
            buf[len++] = (char)key;
            buf[len]   = '\0';
            putchar(key);
            fflush(stdout);
        }
    }

    if (len > 0) {
        rip_disable_raw_mode(state);
        printf("\n");
        fflush(stdout);

        int status = system(buf);
        (void)status;

        printf("\n[Press Enter to return to rip]\n");
        fflush(stdout);

        while (1) {
            char c;
            if (read(state->tty_fd, &c, 1) == 1 && (c == '\n' || c == '\r')) {
                break;
            }
        }

        rip_enable_raw_mode(state);
        rip_get_terminal_size(state);
        rip_reflow_all(state);
    }
}

/* ─────────────────────────────────────────────────────────────────────────── *
 *  Follow mode                                                                *
 * ─────────────────────────────────────────────────────────────────────────── */
static void enter_follow_mode(rip_state_t *state) {
    if (!state->filename || strcmp(state->filename, "stdin") == 0) {
        snprintf(state->status_msg, sizeof(state->status_msg),
                 "Follow mode not available for stdin");
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
            rip_add_raw_line(state, line_buf, ll);
            state->raw_lines[state->num_raw_lines - 1].byte_offset = old_size;
            state->file_size += ll + 1;
            got_new = 1;
        }
        if (got_new) {
            rip_reflow_all(state);
            state->top_line = get_max_top(state);
            rip_render(state);
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
void rip_run(rip_state_t *state) {
    rip_get_terminal_size(state);
    rip_reflow_all(state);
    rip_update_search_matches(state);
    rip_render(state);

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
                rip_get_terminal_size(state);
                rip_reflow_all(state);
                rip_update_search_matches(state);
                clamp_top(state);
                rip_render(state);
                continue;
            }
            break;
        }

        /* 1. Handle stdin stream input */
        if (stdin_idx != -1 && (fds[stdin_idx].revents & (POLLIN | POLLHUP | POLLERR))) {
            char buf[4096];
            ssize_t nr = read(STDIN_FILENO, buf, sizeof(buf));
            if (nr > 0) {
                rip_append_stream_data(state, buf, nr);
                rip_reflow_all(state);
                /* Note: rip_reflow_all calls rip_update_search_matches internally */
                rip_render(state);
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
            int key = rip_read_key(state);
            if (key == -1) break;
            if (key == KEY_RESIZE) {
                rip_get_terminal_size(state);
                rip_reflow_all(state);
                clamp_top(state);
                rip_render(state);
                continue;
            }

            /* clear status message on any key */
            state->status_msg[0] = '\0';

            /* accumulate numeric prefix */
            if (key >= '1' && key <= '9') {
                if (state->num_prefix < 0) state->num_prefix = 0;
                state->num_prefix = state->num_prefix * 10 + (key - '0');
                rip_render(state);
                continue;
            }
            if (key == '0' && state->num_prefix >= 0) {
                state->num_prefix = state->num_prefix * 10;
                rip_render(state);
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
                break; /* just fall through to re-render */

            /* ── reload ── */
            case 'R':
                rip_reload(state);
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
            case KEY_ARROW_RIGHT:
                if (!state->wrap_enabled) {
                    state->horiz_offset += N * 4;
                }
                break;
            case KEY_ARROW_LEFT:
                if (!state->wrap_enabled) {
                    state->horiz_offset -= N * 4;
                    if (state->horiz_offset < 0) state->horiz_offset = 0;
                }
                break;
            case ')':
                if (!state->wrap_enabled) {
                    state->horiz_offset += N * (state->term_cols / 2);
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
                rip_reflow_all(state);
                rip_update_search_matches(state);
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
                rip_update_search_matches(state);
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
            case 'w': case 'W': {
                state->wrap_enabled ^= 1;
                state->horiz_offset = 0;
                rip_reflow_all(state);
                clamp_top(state);
                snprintf(state->status_msg, sizeof(state->status_msg),
                         "Word wrap: %s", state->wrap_enabled ? "ON" : "OFF");
                break;
            }
            case 'L':
                state->line_numbers ^= 1;
                rip_reflow_all(state);
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
                int text_rows = state->term_rows - 1;
                size_t bot = state->top_line + text_rows;
                if (bot > state->num_display_lines) bot = state->num_display_lines;
                int pct = 0;
                if (state->num_display_lines > 0) {
                    pct = (int)(100 * bot / state->num_display_lines);
                    if (pct > 100) pct = 100;
                }
                size_t byte_bot = 0;
                if (state->num_display_lines > 0 && bot > 0) {
                    size_t line_idx = bot - 1;
                    byte_bot = state->display_lines[line_idx].byte_offset + state->display_lines[line_idx].len;
                }
                snprintf(state->status_msg, sizeof(state->status_msg),
                         "%s  line %zu/%zu  (%d%%)  byte %zu/%zu",
                         state->filename ? state->filename : "stdin",
                         bot,
                         state->num_display_lines,
                         pct,
                         byte_bot,
                         state->file_size);
                break;
            }

            /* ── follow mode ── */
            case 'F':
                enter_follow_mode(state);
                break;

            /* ── help ── */
            case 'h': case 'H' & 0x1f: /* also handle if not highlight toggle */
                show_help(state);
                break;

            /* ── terminal resize ── */
            case KEY_RESIZE:
                rip_get_terminal_size(state);
                rip_reflow_all(state);
                clamp_top(state);
                break;

            default:
                break;
            }

            rip_render(state);
        }
    }
}
