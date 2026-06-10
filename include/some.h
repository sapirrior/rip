#ifndef SOME_H
#define SOME_H

#ifndef SOME_VERSION
#define SOME_VERSION "v0.0.1"
#endif

#include <stddef.h>
#include <termios.h>

enum some_key {
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
    size_t raw_line_idx;
} some_line_t;

typedef struct {
    /* ── Raw & display line buffers ── */
    some_line_t *raw_lines;
    size_t num_raw_lines;
    size_t raw_lines_capacity;

    some_line_t *display_lines;
    size_t num_display_lines;
    size_t display_lines_capacity;

    /* ── Viewport ── */
    size_t top_line;        /* 0-indexed display line at top of screen      */
    int horiz_offset;       /* horizontal scroll offset (columns, chop mode) */
    int max_line_width;     /* visual width of the longest display line     */
    int term_rows;
    int term_cols;

    /* ── Terminal ── */
    int tty_fd;
    struct termios orig_termios;
    int raw_mode_enabled;

    /* ── Search / Filter ── */
    char search_pattern[256];
    char filter_pattern[256];
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
    int syntax_highlighting; /* 1 = syntax highlighting ON, 0 = OFF          */
    int diff_enabled;       /* 1 = show diff, 0 = hide diff                 */

    /* ── Status message (temporary overlay) ── */
    char status_msg[512];   /* if non-empty, show this instead of normal bar*/

    /* ── File info ── */
    const char *filename;
    int is_stdin;
    size_t file_size;       /* total bytes of raw file                      */
    char *diff_status;      /* array of size num_raw_lines containing diff_status_t */

    /* ── Search Cache ── */
    size_t *search_matches;
    size_t num_search_matches;
    size_t search_matches_capacity;
    int current_match_idx;

    /* ── Search/Filter History ── */
    char search_history[16][256];
    int search_history_count;
    char filter_history[16][256];
    int filter_history_count;

    /* ── Incremental loading state ── */
    int stdin_eof;
    int raw_last_has_newline;
} some_state_t;

/* Init / cleanup */
void some_init_state(some_state_t *state);
void some_free_state(some_state_t *state);

/* Input */
int  some_read_input(some_state_t *state, const char *path);
int  some_reload(some_state_t *state);
void some_add_raw_line(some_state_t *state, const char *data, size_t len);
void some_append_stream_data(some_state_t *state, const char *buf, size_t len);
void some_update_search_matches(some_state_t *state);
void some_load_diff_status(some_state_t *state);

/* Reflow */
void some_reflow_all(some_state_t *state);
void some_add_display_line(some_state_t *state, const char *data, size_t len, size_t raw_line_idx);
int  some_get_gutter_width(some_state_t *state);

/* Terminal */
int  some_enable_raw_mode(some_state_t *state);
void some_disable_raw_mode(some_state_t *state);
void some_get_terminal_size(some_state_t *state);
int  some_read_key(some_state_t *state);
int  some_decode_utf8(const char *str, size_t len, unsigned int *ch);
int  some_char_width(unsigned int ch, int visual_col);

/* Pager */
void some_render(some_state_t *state);
void some_run(some_state_t *state);

#endif /* SOME_H */
