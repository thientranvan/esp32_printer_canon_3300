/* SPDX-License-Identifier: GPL-3.0-or-later
 * Links captdriver (Alexey Galakhov et al.); see upstream COPYING.
 * stdin: exactly 592*6774 bytes, black=1, A4 printable area at 600 dpi.
 * stdout: D0A4 payload followed by u32 count and (u32 size, band) records.
 */
#include "hiscoa-compress.h"
#include "hiscoa-decompress.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void u32(unsigned n) {
    unsigned char b[4] = {n, n >> 8, n >> 16, n >> 24};
    if (fwrite(b, 1, 4, stdout) != 4) exit(1);
}
int main(void) {
    unsigned char input[592*70], output[592*70*2+64], decoded[592*70], params[8];
    hiscoa_format_params(params, 8, &hiscoa_default_params);
    if (fwrite(params, 1, 8, stdout) != 8) return 1;
    u32((6774+69)/70);
    for (unsigned y = 0; y < 6774; y += 70) {
        unsigned lines = 6774-y < 70 ? 6774-y : 70, size = lines*592;
        if (fread(input, 1, size, stdin) != size) return 2;
        size_t n = hiscoa_compress_band(output, sizeof(output), input, 592, lines,
            y+lines == 6774 ? HISCOA_EOB_LAST : HISCOA_EOB_NORMAL, &hiscoa_default_params);
        /* Verify every compressed band before handing it to hardware. */
        const void *p = output; size_t remaining = n, length = sizeof(decoded);
        hiscoa_decompress_band(&p, &remaining, decoded, &length, 592, &hiscoa_default_params);
        if (remaining || length != size || memcmp(input, decoded, size)) return 3;
        u32(n);
        if (fwrite(output, 1, n, stdout) != n) return 1;
    }
    if (getchar() != EOF) return 2;
    return fflush(stdout) ? 1 : 0;
}
