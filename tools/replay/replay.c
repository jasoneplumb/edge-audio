/* Intent: replay — reproduce a sample window from a recorded frame stream (R-08).
 * Usage: replay <file.earc> --window <first>,<count>
 * Writes raw interleaved PCM_S16LE to stdout. Exit codes: 0 ok, 2 usage or
 * unreadable file, 3 window unavailable (reason on stderr; SPANS_GAP per
 * C-03/D-14 — never zero-filled). */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "earc_format.h"
#include "replay_lib.h"

int main(int argc, char **argv)
{
    if (argc != 4 || strcmp(argv[2], "--window") != 0) {
        fprintf(stderr,
                "usage: replay <file.earc> --window <first>,<count>\n");
        return 2;
    }
    uint64_t first = 0;
    uint32_t count = 0;
    if (sscanf(argv[3], "%" SCNu64 ",%" SCNu32, &first, &count) != 2 ||
        count == 0) {
        fprintf(stderr, "bad --window argument\n");
        return 2;
    }

    /* Channel count comes from the file header. */
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    ea_earc_header h;
    if (fread(&h, sizeof(h), 1, f) != 1 || h.magic != EA_EARC_MAGIC) {
        fclose(f);
        fprintf(stderr, "not an EARC file\n");
        return 2;
    }
    fclose(f);

    int16_t *buf = malloc((size_t)count * h.channels * sizeof(int16_t));
    if (!buf)
        return 2;
    ea_replay_result r = ea_replay_window(argv[1], first, count, buf);
    switch (r) {
    case EA_REPLAY_OK:
        fwrite(buf, sizeof(int16_t), (size_t)count * h.channels, stdout);
        free(buf);
        return 0;
    case EA_REPLAY_SPANS_GAP:
        fprintf(stderr, "UNAVAILABLE: SPANS_GAP\n");
        break;
    case EA_REPLAY_OUT_OF_RANGE:
        fprintf(stderr, "UNAVAILABLE: OUT_OF_RANGE\n");
        break;
    default:
        fprintf(stderr, "bad file\n");
        free(buf);
        return 2;
    }
    free(buf);
    return 3;
}
