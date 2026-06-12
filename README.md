# `some` — Scroll Or More Easily (less++)

> *"Some are less, but `some` is more!"*

`some` is a terminal pager written in C that behaves like GNU `less` but includes built-in, zero-config modular filters to automatically make reading terminal outputs much better.

## Core Features

- **Vim-style & GNU `less` Navigation**: Supports standard Vim movements (`h`/`j`/`k`/`l` for left/down/up/right) and classic navigation (`Space`/`b` page scroll, `/` search, `n`/`N` next/prev match, `q` quit).
- **Auto-Converter Pipeline**:
  - **JSON**: Sniffs and pretty-prints minified JSON files automatically.
  - **CSV / TSV**: Automatically parses CSV/TSV and aligns fields into clean, visually readable tables in the terminal.
- **Smart Word Wrapping**: Wraps long lines nicely at word boundaries while preserving the starting line's indentation (can be toggled interactively with the `w` key).
- **Interactive Line Filtering**: Press `&` to isolate matches by regular expression dynamically, with instant reflow rendering.
- **Auto-Colorization**:
  - **Code Highlights**: Adds syntax highlighting to C/C++ (`.c`, `.h`, `.cpp`, `.hpp`), Python (`.py`), JavaScript/TypeScript (`.js`, `.jsx`, `.ts`, `.tsx`, `.mjs`, `.cjs`), Rust (`.rs`), Go (`.go`), SQL (`.sql`), Shell/Bash (`.sh`, `.bash`, `.zsh`), YAML (`.yaml`, `.yml`), TOML (`.toml`), Markdown (`.md`, `.markdown`), Java (`.java`), C# (`.cs`), Ruby (`.rb`), PHP (`.php`), CSS (`.css`), and HTML/XML (`.html`, `.htm`, `.xml`, `.xhtml`).
  - **Log Level Colors**: Automatically highlights logs containing `INFO`, `WARN`, `DEBUG`, or `ERROR` in corresponding colors.
- **Search Highlighting**: Interactive regex-based search (`/` / `?`) with high-contrast background highlights.
- **Nested Help Pager**: Press `Ctrl+H` to access the interactive, beautifully colorized help menu loaded directly as scrollable pager content, supporting scroll, wraps, and search.
- **Git Diff Gutter**: Press `c` to toggle real-time Git change tracking indicators in the gutter (shows additions, deletions, and modifications with GitHub's exact styling).
- **Signal-Safe Resize**: Dynamic terminal window resize handling (`SIGWINCH`) recalculates word wraps and redraws cleanly.


---

## Build

Compile the project with:

```bash
make
```

To perform a clean rebuild:

```bash
make clean && make
```

---

## Usage

Simply run `some` by passing options, a file path, or piping output to it:

```bash
# Check version information
./some -v

# Show command-line help
./some -h

# View code with syntax highlighting
./some test_code.c

# View minified JSON pretty-printed
./some test.json

# View CSV files aligned in clean tables
./some test.csv

# View log levels highlighted in color
./some test.log

# Pipe command output directly
cat test_code.c | ./some
```

### Controls

| Key | Action |
|-----|--------|
| `j` / `↓` | Scroll down one line |
| `k` / `↑` | Scroll up one line |
| `Space` / `PgDn` | Scroll down one page |
| `b` / `PgUp` | Scroll up one page |
| `d` | Scroll down half page |
| `u` | Scroll up half page |
| `g` / `Home` | Jump to beginning of file (or line N if prefix N is typed) |
| `G` / `End` | Jump to end of file (or line N if prefix N is typed) |
| `w` | Toggle smart word wrapping |
| `L` | Toggle line numbers |
| `s` | Toggle syntax highlighting |
| `c` | Toggle Git diff changes gutter indicator (`┃` in bold green/red/purple) |
| `M` | Toggle verbose status bar prompt |
| `=` | Show detailed file statistics / status overlay |
| `h` / `l` / `←` / `→` | Scroll 4 columns left / right (chop mode) |
| `(` / `)` | Scroll half-page left / right (chop mode) |
| `/` / `?` | Regex search (forward / backward) |
| `&` | Regex line filter |
| `n` / `N` | Jump to next / previous search match |
| `R` | Reload current file from disk |
| `v` | Open current line in editor ($VISUAL or $EDITOR) |
| `!` | Run shell command in a subshell |
| `F` | Real-time follow mode (tail -f style) |
| `Ctrl+H` | Styled help screen |
| `q` | Quit / Close active help screen |

