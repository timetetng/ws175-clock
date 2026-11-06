/* Small PCM WAV reader; no ESP dependencies so malformed inputs can be tested on host. */
#ifndef MUSIC_WAV_H
#define MUSIC_WAV_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t rate, bytes;
    long offset;
} music_wav_t;

static uint32_t music_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}

/* NULL = success; leaves f positioned at the first PCM sample. */
static const char *music_wav_read(FILE *f, music_wav_t *wav)
{
    uint8_t h[16];
    memset(wav, 0, sizeof(*wav));
    if (fseek(f, 0, SEEK_END)) return "WAV seek failed";
    long end = ftell(f);
    rewind(f);
    if (end < 12 || fread(h, 1, 12, f) != 12 ||
        memcmp(h, "RIFF", 4) || memcmp(h+8, "WAVE", 4)) return "Invalid WAV header";
    uint64_t riff_end = (uint64_t)music_le32(h+4) + 8;
    if (riff_end < 12 || riff_end > (uint64_t)end) return "Truncated WAV";
    int have_fmt = 0;
    while ((uint64_t)ftell(f) + 8 <= riff_end) {
        if (fread(h, 1, 8, f) != 8) return "Truncated chunk";
        uint32_t size = music_le32(h+4);
        long pos = ftell(f);
        uint64_t next = (uint64_t)pos + size + (size & 1);
        if (next > riff_end) return "Truncated chunk";
        if (!memcmp(h, "fmt ", 4)) {
            if (size < 16 || fread(h, 1, 16, f) != 16) return "Invalid WAV format";
            uint32_t rate = music_le32(h+4);
            if (h[0] != 1 || h[1] || h[2] != 1 || h[3] ||
                h[14] != 16 || h[15] || (rate != 22050 && rate != 44100) ||
                h[12] != 2 || h[13] || music_le32(h+8) != rate * 2)
                return "Need mono PCM16 22/44k";
            wav->rate = rate;
            have_fmt = 1;
        } else if (!memcmp(h, "data", 4)) {
            if (!have_fmt || !size || (size & 1)) return "Invalid PCM data";
            wav->bytes = size;
            wav->offset = pos;
            return NULL;
        }
        if (fseek(f, (long)next, SEEK_SET)) return "WAV seek failed";
    }
    return "Missing PCM data";
}
#endif
