#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../main/toy_shell.h"
int main(void)
{
    toy_shell_info_t info = {
        .idf = "v5.5.4", .kernel = "10.5.1", .date = "2026-11-07 03:00:00",
        .cores = 2, .mhz = 240, .revision = 2, .flash_mb = 32, .psram_mb = 8,
        .free_ram_kb = 24, .uptime_seconds = 3661, .brightness = 30, .volume = 15,
    };
    char out[1024];
    assert(toy_shell_run("fastfetch", &info, out, sizeof(out)) == TOY_SHELL_PRINT);
    assert(strstr(out, "rev 0.2") && strstr(out, "32 MiB") && strstr(out, "8 MiB"));
    assert(strstr(out, "466x466 AMOLED") && !strstr(out, "( o  o )") && !strstr(out, "Shell:"));
    toy_shell_run("  echo hello $USER; reboot | sh  ", &info, out, sizeof(out));
    assert(!strcmp(out, "hello $USER; reboot | sh  \n"));
    toy_shell_run("echo %n $(rm -rf /)", &info, out, sizeof(out));
    assert(!strcmp(out, "%n $(rm -rf /)\n"));
    toy_shell_run("uptime", &info, out, sizeof(out));
    assert(!strcmp(out, "up 1h 1m 1s\n"));
    toy_shell_run("date", &info, out, sizeof(out));
    assert(strstr(out, info.date));
    toy_shell_run("uname -a", &info, out, sizeof(out));
    assert(strstr(out, "FreeRTOS"));
    toy_shell_run("cat /etc/os-release", &info, out, sizeof(out));
    assert(strstr(out, "VIRTUAL FILE") && strstr(out, "REAL_UNIX=no"));
    toy_shell_run("cat /etc/passwd", &info, out, sizeof(out));
    assert(strstr(out, "no such virtual file"));
    toy_shell_run("reboot", &info, out, sizeof(out));
    assert(strstr(out, "command not found"));
    assert(toy_shell_run("clear", &info, out, sizeof(out)) == TOY_SHELL_CLEAR && !out[0]);
    toy_shell_run("   ", &info, out, sizeof(out)); assert(!out[0]);
    toy_shell_run("echo test\nreboot", &info, out, sizeof(out)); assert(strstr(out, "single-line"));
    char large[256]; memset(large, 'x', sizeof(large) - 1); large[255] = 0;
    toy_shell_run(large, &info, out, sizeof(out)); assert(strstr(out, "too long"));
    toy_shell_run("fastfetch", NULL, out, sizeof(out)); assert(strstr(out, "unavailable"));
    char tiny[2] = {1, 1}; toy_shell_run("fastfetch", &info, tiny, sizeof(tiny)); assert(tiny[1] == 0);
    toy_shell_run("help", &info, NULL, 0);
    char history[24] = "";
    for (int i = 0; i < 10000; ++i) {
        toy_shell_append(history, sizeof(history), "hello\n");
        assert(strlen(history) < sizeof(history));
    }
    assert(!strcmp(history, "hello\nhello\nhello\n"));
    toy_shell_append(history, sizeof(history), large); assert(strlen(history) == 23);
    char single[1] = {0}; toy_shell_append(single, 1, "abcdef"); assert(single[0] == 0);
    toy_shell_append(NULL, 0, "ignored");
    puts("toy shell: PASS");
}
