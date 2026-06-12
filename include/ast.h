#ifndef SOME_AST_H
#define SOME_AST_H

#include <stddef.h>

// Node helpers (renamed to state helpers since nodes are gone)
typedef struct {
    unsigned char context;       // 0 = default, 1 = block comment, 2 = string, 3 = py triple double quote, 4 = py triple single quote, 5 = xml comment, 6 = xml tag
    char string_quote;           // '\'' or '"'
    unsigned char brace_depth;
    unsigned char paren_depth;
    unsigned char bracket_depth;
} syntax_state_t;

typedef void (*highlight_fn_t)(const char *input, size_t input_len, syntax_state_t *state, char **dest, size_t *di, size_t *cap);

typedef struct {
    const char *extension;
    const char **keywords;
    size_t num_keywords;
    const char **types;
    size_t num_types;
    const char *line_comment;
    const char *block_comment_start;
    const char *block_comment_end;
    
    char* (*format_fn)(const char *input, size_t input_len, size_t *out_len);
    highlight_fn_t highlight_fn;
} syntax_def_t;

char* ast_convert(const char *filename, const char *input, size_t input_len, size_t *out_len, int enable_colors);

char* ast_highlight_line(const char *filename, const char *input, size_t input_len, syntax_state_t *state, size_t *out_len);
void ast_highlight_display_lines(void *some_state_ptr);

// Syntax definitions declarations
const syntax_def_t* get_c_syntax_def(void);
const syntax_def_t* get_py_syntax_def(void);
const syntax_def_t* get_json_syntax_def(void);
const syntax_def_t* get_csv_syntax_def(void);
const syntax_def_t* get_tsv_syntax_def(void);
const syntax_def_t* get_xml_syntax_def(void);
const syntax_def_t* get_diff_syntax_def(void);
const syntax_def_t* get_log_syntax_def(void);
const syntax_def_t* get_txt_syntax_def(void);
const syntax_def_t* get_js_syntax_def(void);
const syntax_def_t* get_rs_syntax_def(void);
const syntax_def_t* get_go_syntax_def(void);
const syntax_def_t* get_sql_syntax_def(void);
const syntax_def_t* get_sh_syntax_def(void);
const syntax_def_t* get_yaml_syntax_def(void);
const syntax_def_t* get_md_syntax_def(void);
const syntax_def_t* get_java_syntax_def(void);
const syntax_def_t* get_cs_syntax_def(void);
const syntax_def_t* get_rb_syntax_def(void);
const syntax_def_t* get_php_syntax_def(void);
const syntax_def_t* get_css_syntax_def(void);
const syntax_def_t* get_html_syntax_def(void);
const syntax_def_t* get_toml_syntax_def(void);

// Provide helper to append directly to a string buffer
void ast_append_char(char **dest, size_t *di, size_t *cap, char c);
void ast_append_str(char **dest, size_t *di, size_t *cap, const char *s);

#endif // SOME_AST_H
