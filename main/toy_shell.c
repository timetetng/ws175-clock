/* Deliberately NOT a shell: fixed command table, literal arguments and a
 * fictional read-only filesystem. No process, filesystem or network APIs. */
#include "toy_shell.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

toy_shell_result_t toy_shell_run(const char *line, const toy_shell_info_t *info,
                                char *out, size_t capacity)
{
    if (capacity) out[0] = '\0';
    if (!line) return TOY_SHELL_PRINT;
    char command[TOY_SHELL_COMMAND_MAX + 1];
    size_t length = 0;
    while (line[length] && length < sizeof(command)) ++length;
    if (length > TOY_SHELL_COMMAND_MAX) {
        snprintf(out, capacity, "toysh: command too long\n");
        return TOY_SHELL_PRINT;
    }
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)line[i];
        if ((c < 32 && c != '\t') || c > 126) {
            snprintf(out, capacity, "toysh: ASCII, single-line input only\n");
            return TOY_SHELL_PRINT;
        }
    }
    memcpy(command, line, length + 1);
    char *name = command;
    while (*name == ' ' || *name == '\t') ++name;
    if (!*name) return TOY_SHELL_PRINT;
    char *args = name;
    while (*args && *args != ' ' && *args != '\t') ++args;
    if (*args) *args++ = '\0';
    while (*args == ' ' || *args == '\t') ++args;
    if (!strcmp(name, "echo")) {
        snprintf(out, capacity, "%s\n", args); /* No quoting/expansion/execution. */
    } else if (!strcmp(name, "clear")) {
        return TOY_SHELL_CLEAR;
    } else if (!strcmp(name, "help")) {
        snprintf(out, capacity,
                 "TOY COMMANDS\n"
                 "fastfetch   board information\n"
                 "echo TEXT   literal text\n"
                 "clear       clear screen\n"
                 "uname       firmware/kernel\n"
                 "uptime      time since boot\n"
                 "date        device clock\n"
                 "pwd whoami  pretend identity\n"
                 "ls cat      virtual files only\n"
                 "No real Unix or command execution.\n");
    } else if (!strcmp(name, "pwd")) {
        snprintf(out, capacity, "/home/esp32  (virtual)\n");
    } else if (!strcmp(name, "whoami")) {
        snprintf(out, capacity, "esp32  (pretend user)\n");
    } else if (!strcmp(name, "ls")) {
        snprintf(out, capacity, "VIRTUAL FILES\n/etc/os-release\n/home/esp32/hello.txt\n/dev/display\n");
    } else if (!strcmp(name, "cat")) {
        if (!strcmp(args, "/etc/os-release")) {
            snprintf(out, capacity, "VIRTUAL FILE\nNAME=\"WS175 ToyShell\"\nKERNEL=\"FreeRTOS\"\nREAL_UNIX=no\n");
        } else if (!strcmp(args, "/home/esp32/hello.txt")) {
            snprintf(out, capacity, "Hello from a pretend terminal!\n");
        } else {
            snprintf(out, capacity, "cat: no such virtual file\n");
        }
    } else if (!strcmp(name, "fastfetch") || !strcmp(name, "uname") ||
               !strcmp(name, "uptime") || !strcmp(name, "date")) {
        if (!info) {
            snprintf(out, capacity, "Board information unavailable\n");
        } else if (!strcmp(name, "fastfetch")) {
            snprintf(out, capacity,
                     "Chip: ESP32-S3 rev %u.%u\n"
                     "Board: Waveshare 1.75C\n"
                     "CPU: %u cores / %u MHz\n"
                     "Flash: %u MiB\n"
                     "PSRAM: %u MiB\n"
                     "Free internal: %u KiB\n"
                     "Display: 466x466 AMOLED\n"
                     "Touch: CST9217\n"
                     "Audio: ES8311\n"
                     "PMU: AXP2101\n",
                     info->revision / 100, info->revision % 100,
                     info->cores, info->mhz, info->flash_mb, info->psram_mb,
                     info->free_ram_kb);
        } else if (!strcmp(name, "uname")) {
            snprintf(out, capacity, "WS175 ToyShell\nESP-IDF %s / FreeRTOS %s\n", info->idf, info->kernel);
        } else if (!strcmp(name, "uptime")) {
            uint64_t seconds = info->uptime_seconds;
            snprintf(out, capacity, "up %" PRIu64 "h %" PRIu64 "m %" PRIu64 "s\n",
                     seconds / 3600, seconds / 60 % 60, seconds % 60);
        } else {
            snprintf(out, capacity, "%s\n", info->date);
        }
    } else {
        snprintf(out, capacity, "toysh: %s: command not found\nTry help.\n", name);
    }
    return TOY_SHELL_PRINT;
}

void toy_shell_append(char *history, size_t capacity, const char *text)
{
    if (!capacity) return;
    size_t add = strlen(text), used = strlen(history);
    if (add >= capacity) {
        memcpy(history, text + add - (capacity - 1), capacity - 1);
        history[capacity - 1] = '\0';
        return;
    }
    if (used + add >= capacity) {
        size_t remove = used + add - capacity + 1;
        while (remove < used && history[remove - 1] != '\n') ++remove;
        memmove(history, history + remove, used - remove + 1);
        used -= remove;
    }
    memcpy(history + used, text, add + 1);
}
