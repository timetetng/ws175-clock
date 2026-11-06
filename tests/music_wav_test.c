#include <assert.h>
#include "../main/music_wav.h"
static const unsigned char valid[] = {
 'R','I','F','F',40,0,0,0,'W','A','V','E',
 'f','m','t',' ',16,0,0,0,1,0,1,0,0x44,0xac,0,0,
 0x88,0x58,1,0,2,0,16,0,'d','a','t','a',4,0,0,0,0,0,0,0
};
static const char *parse(const void *bytes, size_t n, music_wav_t *w)
{
    FILE *f = tmpfile();
    assert(f);
    assert(fwrite(bytes, 1, n, f) == n);
    const char *err = music_wav_read(f, w);
    if (!err) assert(ftell(f) == w->offset);
    fclose(f);
    return err;
}
int main(void)
{
    music_wav_t w;
    assert(!parse(valid, sizeof(valid), &w));
    assert(w.rate == 44100 && w.bytes == 4 && w.offset == 44);
    for (size_t n = 0; n < sizeof(valid); ++n) assert(parse(valid, n, &w));
    unsigned char bad[sizeof(valid)];
    memcpy(bad, valid, sizeof(bad)); bad[0]='X'; assert(parse(bad,sizeof(bad),&w));
    memcpy(bad, valid, sizeof(bad)); bad[22]=2; assert(parse(bad,sizeof(bad),&w));
    memcpy(bad, valid, sizeof(bad)); bad[20]=3; assert(parse(bad,sizeof(bad),&w));
    memcpy(bad, valid, sizeof(bad)); bad[34]=8; assert(parse(bad,sizeof(bad),&w));
    memcpy(bad, valid, sizeof(bad)); bad[16]=15; assert(parse(bad,sizeof(bad),&w));
    memcpy(bad, valid, sizeof(bad)); bad[40]=255; assert(parse(bad,sizeof(bad),&w));
    /* Odd-sized ancillary chunk and extended fmt chunks must be skipped with padding. */
    unsigned char odd[sizeof(valid)+10];
    memcpy(odd,valid,12); odd[4]=50;
    memcpy(odd+12,"JUNK\1\0\0\0x\0",10);
    memcpy(odd+22,valid+12,sizeof(valid)-12);
    assert(!parse(odd,sizeof(odd),&w) && w.offset==54);
    unsigned char ext[sizeof(valid)+2];
    memcpy(ext,valid,36); ext[4]=42; ext[16]=18; ext[36]=0; ext[37]=0;
    memcpy(ext+38,valid+36,sizeof(valid)-36);
    assert(!parse(ext,sizeof(ext),&w) && w.offset==46);
    FILE *real = fopen("spiffs/demo60.wav","rb");
    assert(real && !music_wav_read(real,&w));
    assert(w.bytes == w.rate * 2 * 60);
    fclose(real);
    puts("music WAV tests passed (valid, truncation sweep, bad formats, chunk padding, real 60s demo)");
}
