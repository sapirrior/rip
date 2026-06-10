#include "some.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static some_state_t state;

static void handle_sigwinch(int sig) {
    (void)sig;
    /* EINTR will wake some_read_key which returns KEY_RESIZE */
}

static void handle_cleanup_signals(int sig) {
    if (state.raw_mode_enabled) {
        /* Restore alternate screen buffer cleanly using async-signal-safe write */
        const char *esc_restore = "\033[?1049l";
        (void)write(STDOUT_FILENO, esc_restore, 8);

        /* Restore raw terminal configurations */
        tcsetattr(state.tty_fd, TCSAFLUSH, &state.orig_termios);
    }
    signal(sig, SIG_DFL);
    kill(getpid(), sig);
}

static void print_help_cli(const char *prog) {
    printf("some — Scroll Or More Easily (less++)\n\n");
    printf("Usage: %s [file]  or  command | %s\n\n", prog, prog);
    printf("Navigation:\n");
    printf("  j/↓/Enter        Scroll down 1 line (prefix N repeats)\n");
    printf("  k/↑              Scroll up 1 line (prefix N repeats)\n");
    printf("  Space/f/PgDn     Scroll down 1 page (prefix N repeats)\n");
    printf("  b/PgUp           Scroll up 1 page (prefix N repeats)\n");
    printf("  d                Scroll down half page (prefix N repeats)\n");
    printf("  u                Scroll up half page (prefix N repeats)\n");
    printf("  g/Home           Go to line N (default: top, Ng)\n");
    printf("  G/End            Go to line N (default: bottom, NG)\n");
    printf("  Np/N%%           Go to N%% into file\n");
    printf("  h/l / ←/→        Scroll 4 columns left/right\n");
    printf("  (/)              Scroll half-page left/right\n\n");
    printf("Search & Filter:\n");
    printf("  /pattern         Forward search (prefix N repeats)\n");
    printf("  ?pattern         Backward search (prefix N repeats)\n");
    printf("  &pattern         Filter lines by regex pattern (Enter to clear)\n");
    printf("  n                Next match (prefix N repeats)\n");
    printf("  N                Previous match (prefix N repeats)\n");
    printf("  i                Toggle case-insensitive (default: ON)\n");
    printf("  H                Toggle search highlights\n");
    printf("  ESC              Clear search pattern & highlights\n\n");
    printf("Display:\n");
    printf("  w                Toggle word wrap / chop\n");
    printf("  L                Toggle line numbers\n");
    printf("  s                Toggle AST-based syntax highlighting\n");
    printf("  c                Toggle Git diff changes gutter\n");
    printf("  M                Toggle verbose status bar prompt\n");
    printf("  =                Show detailed file stats / info\n");
    printf("  r                Repaint screen\n");
    printf("  R                Reload file from disk\n\n");
    printf("Session:\n");
    printf("  v                Open editor at current line\n");
    printf("  !cmd             Run shell command\n");
    printf("  F                Follow mode (tail -f style). q to exit.\n");
    printf("  Ctrl+H           Help screen (inside some)\n");
    printf("  q / Q            Quit\n\n");
    printf("Status bar flags: [W]=wrap  [I]=icase  [#]=line-nums  [H]=hi-off  [F]=follow  [S]=syntax-off  [D]=diff-active\n");
}

int main(int argc, char *argv[]) {
    some_init_state(&state);

    const char *filepath = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("some version %s\n", SOME_VERSION);
            return 0;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "-?")    == 0) {
            print_help_cli(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            /* ignore unknown flags for now */
        } else {
            filepath = argv[i];
        }
    }

    if (some_read_input(&state, filepath) != 0) {
        some_free_state(&state);
        return 1;
    }

    signal(SIGINT,  handle_cleanup_signals);
    signal(SIGTERM, handle_cleanup_signals);
    signal(SIGSEGV, handle_cleanup_signals);

    if (some_enable_raw_mode(&state) != 0) {
        some_free_state(&state);
        return 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigwinch;
    sigaction(SIGWINCH, &sa, NULL);

    some_run(&state);

    some_disable_raw_mode(&state);
    some_free_state(&state);
    fflush(stdout);
    return 0;
}
