#ifndef RIP_MODULES_H
#define RIP_MODULES_H

#include <stddef.h>

// Converter: pretty-prints JSON or aligns CSV. Returns a newly allocated string or NULL if no change.
char* module_convert(const char *filename, const char *input, size_t input_len, size_t *out_len);

#endif // RIP_MODULES_H

