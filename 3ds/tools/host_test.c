/* host_test.c - builds and runs the 3DS model code on a PC, so the port can
   be checked without a console:
       cc -O2 -Iinclude -o tiny_host source/tiny_model.c tools/host_test.c -lm
       ./tiny_host romfs/TINY.BIN
*/

#include <stdio.h>
#include <string.h>

#include "tiny_model.h"

static const char *questions[] = {
    "what is the capital of peru",
    "is a whale a fish",
    "what is 7 times 8",
    "spell elephant",
    "what is the capital of texas",
    "how many legs does a spider have",
    "what is the symbol for gold",
    "past tense of go",
    NULL
};

int main(int argc, char **argv)
{
    char out[160], q[TINY_MAXQ + 1];
    long ms;
    int i, n, bad = 0;

    if (tiny_load(argc > 1 ? argv[1] : "TINY.BIN", NULL) != 0) {
        fprintf(stderr, "load failed: %s\n", tiny_error());
        return 1;
    }
    printf("loaded %d layers, %d wide, %d weights\n\n",
           tiny_layers(), tiny_width(), tiny_weights());

    tiny_set_creativity(0);           /* surest answer, so the test repeats */

    for (i = 0; questions[i]; i++) {
        tiny_clean(questions[i], q);
        n = tiny_answer(q, out, &ms, NULL, NULL);
        printf("%-32s -> %-40s [%d letters, %ld ms]\n", q, out, n, ms);
        if (n <= 0) {
            fprintf(stderr, "  empty answer for: %s\n", q);
            bad = 1;
        }
    }
    return bad;
}
