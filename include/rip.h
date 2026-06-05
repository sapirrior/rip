#ifndef RIP_H
#define RIP_H

#include <stddef.h>
#include <termios.h>

enum rip_key {
    KEY_ARROW_UP = 1000,
    KEY_ARROW_DOWN,
    KEY_ARROW_LEFT,
    KEY_ARROW_RIGHT,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN,
    KEY_HOME,
    KEY_END,
    KEY_RESIZE
};

typedef struct {
    char *data;
    size_t len;
    size_t byte_offset;
} rip_line_t;

typedef struct {
    /* ── Raw & display line buffers ── */
    rip_line_t *raw_lines;
    size_t num_raw_lines;
    size_t raw_lines_capacity;

    rip_line_t *display_lines;
    size_t num_display_lines;
    size_t display_lines_capacity;

    /* ── Viewport ── */
    size_t top_line;        /* 0-indexed display line at top of screen      */
    int horiz_offset;       /* horizontal scroll offset (columns, chop mode) */
    int term_rows;
    int term_cols;

    /* ── Terminal ── */
    int tty_fd;
    struct termios orig_termios;
    int raw_mode_enabled;

    /* ── Search ── */
    char search_pattern[256];
    int search_dir;         /* 1 = forward, -1 = backward                   */
    int search_case_insensitive; /* 1 = ignore case (default)               */
    int search_highlight;   /* 1 = highlight on (default)                   */

    /* ── Pending numeric prefix ── */
    int num_prefix;         /* -1 = no prefix typed yet                     */

    /* ── Display flags ── */
    int wrap_enabled;       /* 1 = word wrap, 0 = chop+hscroll              */
    int line_numbers;       /* 1 = show line numbers                        */
    int follow_mode;        /* 1 = tail-f mode active                       */
    int verbose_prompt;     /* 1 = verbose status bar, 0 = short prompt     */

    /* ── Status message (temporary overlay) ── */
    char status_msg[512];   /* if non-empty, show this instead of normal bar*/

    /* ── File info ── */
    const char *filename;
    int is_stdin;
    size_t file_size;       /* total bytes of raw file                      */

    /* ── Search Cache ── */
    size_t *search_matches;
    size_t num_search_matches;
    size_t search_matches_capacity;
    int current_match_idx;

    /* ── Incremental loading state ── */
    int stdin_eof;
    int stdin_nonblocking_set;
    int raw_last_has_newline;
} rip_state_t;

/* Init / cleanup */
void rip_init_state(rip_state_t *state);
void rip_free_state(rip_state_t *state);

/* Input */
int  rip_read_input(rip_state_t *state, const char *path);
int  rip_reload(rip_state_t *state);
void rip_add_raw_line(rip_state_t *state, const char *data, size_t len);
void rip_append_stream_data(rip_state_t *state, const char *buf, size_t len);
void rip_update_search_matches(rip_state_t *state);

/* Reflow */
void rip_reflow_all(rip_state_t *state);
void rip_add_display_line(rip_state_t *state, const char *data, size_t len);
int  rip_get_gutter_width(rip_state_t *state);

/* Terminal */
int  rip_enable_raw_mode(rip_state_t *state);
void rip_disable_raw_mode(rip_state_t *state);
void rip_get_terminal_size(rip_state_t *state);
int  rip_read_key(rip_state_t *state);
int  rip_decode_utf8(const char *str, size_t len, unsigned int *ch);
int  rip_char_width(unsigned int ch, int visual_col);

/* Pager */
void rip_render(rip_state_t *state);
void rip_run(rip_state_t *state);

#endif /* RIP_H */
