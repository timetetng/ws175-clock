#ifndef WS175_TOY_SHELL_H
#define WS175_TOY_SHELL_H
#include <stddef.h>
#include <stdint.h>
#define TOY_SHELL_COMMAND_MAX 96
typedef struct {
    const char *idf, *kernel, *date;
    uint64_t uptime_seconds;
    unsigned cores, mhz, revision, flash_mb, psram_mb, free_ram_kb;
    int brightness, volume;
} toy_shell_info_t;
typedef enum { TOY_SHELL_PRINT, TOY_SHELL_CLEAR } toy_shell_result_t;
toy_shell_result_t toy_shell_run(const char *line, const toy_shell_info_t *info,
                                char *out, size_t capacity);
void toy_shell_append(char *history, size_t capacity, const char *text);
#endif
