/* 호스트(리눅스) 검증용: 파일 → 디코더 → DSP → WAV 저장, 탐색도 시험 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/source.h"
#include "../src/dsp.h"

static void w32(FILE *f, unsigned v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc(v >> 24, f); }
static void w16(FILE *f, unsigned v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }

int main(int argc, char **argv)
{
    Source s; Dsp d; DspParams p;
    short in[1152 * 2], out[4096 * 2];
    FILE *fo; long frames = 0; int n;
    if (argc < 6) { fprintf(stderr, "usage: in out.wav rate ch level [hpf] [seekms]\n"); return 1; }
    if (!src_open(&s, argv[1])) { fprintf(stderr, "open fail\n"); return 2; }
    printf("type=%d rate=%d ch=%d kbps=%d dur=%lums\n", s.type, s.rate, s.channels, s.bitrate, s.durationMs);
    p.outRate = atoi(argv[3]); p.outCh = atoi(argv[4]); p.levelPct = atoi(argv[5]);
    p.hpfHz = argc > 6 ? atoi(argv[6]) : 0; p.lpfHz = 0;
    if (argc > 7) { src_seek_ms(&s, (unsigned long)atol(argv[7])); printf("seek -> pos %lums\n", src_pos_ms(&s)); }
    dsp_init(&d, s.rate, s.channels, &p);
    fo = fopen(argv[2], "wb");
    fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, fo); w32(fo, 16); w16(fo, 1); w16(fo, p.outCh);
    w32(fo, p.outRate); w32(fo, p.outRate * 2 * p.outCh); w16(fo, 2 * p.outCh); w16(fo, 16);
    fwrite("data\0\0\0\0", 1, 8, fo);
    while ((n = src_read(&s, in, 1152)) > 0) {
        int off = 0;
        while (off < n) {
            int used, o = dsp_process(&d, in + off * s.channels, n - off, &used, out, 4096);
            fwrite(out, 2 * p.outCh, (size_t)o, fo);
            frames += o; off += used;
            if (!used && !o) break;
        }
    }
    { long sz = frames * 2 * p.outCh; fseek(fo, 4, SEEK_SET); w32(fo, (unsigned)(36 + sz)); fseek(fo, 40, SEEK_SET); w32(fo, (unsigned)sz); }
    fclose(fo);
    printf("out frames=%ld (%.2fs) limiter_hits=%lu endpos=%lums\n", frames, (double)frames / p.outRate, d.clipCount, src_pos_ms(&s));
    src_close(&s);
    return 0;
}
