# `rip` — Read In Pager (less++)

`rip` is a terminal pager written in C that behaves like GNU `less` but includes built-in, zero-config modular filters to automatically make reading terminal outputs much better.

## Core Features

- **GNU `less` Compatible Navigation**: Fully supports familiar keybindings (`j`/`k` scroll, `Space`/`b` page scroll, `/` search, `n`/`N` next/prev match, `q` quit).
- **Auto-Converter Pipeline**:
  - **JSON**: Sniffs and pretty-prints minified JSON files automatically.
  - **CSV / TSV**: Automatically parses CSV/TSV and aligns fields into clean, visually readable tables in the terminal.
- **Smart Word Wrapping**: Wraps long lines nicely at word boundaries while preserving the starting line's indentation (can be toggled interactively with the `w` key).
- **Auto-Colorization**:
  - **Code Highlights**: Adds syntax highlighting to C/C++ files (`.c`, `.h`, `.cpp`).
  - **Log Level Colors**: Automatically highlights logs containing `INFO`, `WARN`, `DEBUG`, or `ERROR` in corresponding colors.
- **Search Highlighting**: Interactive regex-based search (`/`) with high-contrast background highlights.
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

Simply run `rip` by passing a file path or piping output to it:

```bash
# View code with syntax highlighting
./rip test_code.c

# View minified JSON pretty-printed
./rip test.json

# View CSV files aligned in clean tables
./rip test.csv

# View log levels highlighted in color
./rip test.log

# Pipe command output directly
cat test_code.c | ./rip
```

### Controls

| Key | Action |
|-----|--------|
| `j` / `↓` | Scroll down one line |
| `k` / `↑` | Scroll up one line |
| `Space` / `PgDn` | Scroll down one page |
| `b` / `PgUp` | Scroll up one page |
| `g` / `Home` | Jump to beginning of file |
| `G` / `End` | Jump to end of file |
| `w` | Toggle smart word wrapping |
| `/` | Regex search |
| `n` / `N` | Jump to next / previous search match |
| `q` | Quit |
