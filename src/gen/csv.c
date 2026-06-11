#include "ast.h"
#include <stdlib.h>
#include <string.h>

static void append_char(char **dest, size_t *di, size_t *cap, char c) {
    if (*di + 2 >= *cap) {
        *cap = *cap ? *cap * 2 : 1024;
        *dest = realloc(*dest, *cap);
    }
    (*dest)[(*di)++] = c;
}

static void append_str(char **dest, size_t *di, size_t *cap, const char *s) {
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

static char* align_csv(const char *input, size_t input_len, size_t *out_len, char separator) {
    int rows_count = 0;
    for (size_t i = 0; i < input_len; i++) {
        if (input[i] == '\n') rows_count++;
    }
    rows_count += 2;

    char ***table = calloc(rows_count, sizeof(char**));
    int *row_cols = calloc(rows_count, sizeof(int));
    int max_cols = 0;

    size_t start = 0;
    int row_idx = 0;
    for (size_t i = 0; i <= input_len; i++) {
        if (i == input_len || input[i] == '\n' || input[i] == '\r') {
            size_t line_len = i - start;
            if (line_len > 0 || i == input_len) {
                char *line = malloc(line_len + 1);
                memcpy(line, input + start, line_len);
                line[line_len] = '\0';

                int fields_cap = 128;
                char **fields = calloc(fields_cap, sizeof(char*));
                int cols = 0;
                size_t si = 0;
                while (si < line_len) {
                    if (cols >= fields_cap) {
                        fields_cap *= 2;
                        fields = realloc(fields, fields_cap * sizeof(char*));
                        memset(fields + cols, 0, (fields_cap - cols) * sizeof(char*));
                    }
                    char *field = malloc(line_len + 1);
                    size_t fi = 0;
                    if (line[si] == '"') {
                        si++;
                        while (si < line_len) {
                            if (line[si] == '"' && si + 1 < line_len && line[si + 1] == '"') {
                                field[fi++] = '"';
                                si += 2;
                            } else if (line[si] == '"') {
                                si++;
                                break;
                            } else {
                                field[fi++] = line[si++];
                            }
                        }
                    } else {
                        while (si < line_len && line[si] != separator) {
                            field[fi++] = line[si++];
                        }
                    }
                    field[fi] = '\0';
                    fields[cols++] = field;
                    if (line[si] == separator) si++;
                }

                table[row_idx] = fields;
                row_cols[row_idx] = cols;
                if (cols > max_cols) max_cols = cols;
                row_idx++;

                free(line);
            }
            if (i < input_len && input[i] == '\r' && input[i + 1] == '\n') i++;
            start = i + 1;
        }
    }

    int *col_widths = NULL;
    if (max_cols > 0) {
        col_widths = calloc(max_cols, sizeof(int));
        for (int r = 0; r < row_idx; r++) {
            for (int c = 0; c < row_cols[r]; c++) {
                int len = strlen(table[r][c]);
                if (len > col_widths[c]) {
                    col_widths[c] = len;
                }
            }
        }
    }

    size_t out_cap = input_len + 1024;
    char *dest = malloc(out_cap);
    size_t di = 0;

    for (int r = 0; r < row_idx; r++) {
        for (int c = 0; c < row_cols[r]; c++) {
            const char *field = table[r][c];
            append_str(&dest, &di, &out_cap, field);

            if (c < row_cols[r] - 1 && col_widths) {
                int pad = col_widths[c] - strlen(field) + 2;
                for (int p = 0; p < pad; p++) {
                    append_char(&dest, &di, &out_cap, ' ');
                }
            }
        }
        append_char(&dest, &di, &out_cap, '\n');

        for (int c = 0; c < row_cols[r]; c++) {
            free(table[r][c]);
        }
        free(table[r]);
    }
    dest[di] = '\0';

    free(table);
    free(row_cols);
    if (col_widths) free(col_widths);

    *out_len = di;
    return dest;
}

static char* gen_csv_format_comma(const char *input, size_t input_len, size_t *out_len) {
    return align_csv(input, input_len, out_len, ',');
}

static char* gen_csv_format_tab(const char *input, size_t input_len, size_t *out_len) {
    return align_csv(input, input_len, out_len, '\t');
}

static const syntax_def_t csv_syntax_def = {
    .extension = ".csv",
    .keywords = NULL,
    .num_keywords = 0,
    .types = NULL,
    .num_types = 0,
    .line_comment = NULL,
    .block_comment_start = NULL,
    .block_comment_end = NULL,
    .format_fn = gen_csv_format_comma,
    .highlight_fn = NULL
};

static const syntax_def_t tsv_syntax_def = {
    .extension = ".tsv",
    .keywords = NULL,
    .num_keywords = 0,
    .types = NULL,
    .num_types = 0,
    .line_comment = NULL,
    .block_comment_start = NULL,
    .block_comment_end = NULL,
    .format_fn = gen_csv_format_tab,
    .highlight_fn = NULL
};

const syntax_def_t* get_csv_syntax_def(void) {
    return &csv_syntax_def;
}

const syntax_def_t* get_tsv_syntax_def(void) {
    return &tsv_syntax_def;
}
