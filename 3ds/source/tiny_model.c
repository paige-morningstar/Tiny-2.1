/* tiny_model.c - TINY 2.1's transformer, with the screen and speaker taken out.
   This is the same network as TINY.C: 8-bit weights with one scale per row,
   letter by letter, read from a TNY2 file.  No DOS, no 3DS - it builds
   anywhere so it can be tested on a PC as well as on the 3DS. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "tiny_model.h"

#ifdef __3DS__
#include <3ds.h>
#else
#include <time.h>
#endif

#define VOCAB  97               /* 0 pad, 1 newline, 2.. the letters ' '..'~' */
#define CTX    128              /* longest question + answer, in letters     */
#define MAXD   128
#define MAXHID 512
#define MAXL   8

/* A table of weights: one byte per number, one scale per row. */
typedef struct {
    float *scale;
    signed char *w;
    int rows, cols;
} Table;

static int D, L, H, HD, HID;
static float att_div;                          /* sqrt(HD) */
static Table emb, pos;
static Table wq[MAXL], wk[MAXL], wv[MAXL], wo[MAXL], f1[MAXL], f2[MAXL];
static float *g1[MAXL], *g2[MAXL], *gf;

static int creativity = 5;      /* 0 = always the surest answer, 9 = wild */
static float last_conf;         /* how sure the network was of its letter */

static char errmsg[160];
static const char *load_error = "";

static int (*abort_check)(void);

static float layer_act[MAXL];                  /* how busy each layer was */

const char *tiny_error(void) { return load_error; }
float tiny_layer_act(int l) { return (l >= 0 && l < L) ? layer_act[l] : 0.0f; }
float tiny_conf(void) { return last_conf; }
int tiny_layers(void) { return L; }
int tiny_width(void) { return D; }

int tiny_weights(void)
{
    if (L == 0) return 0;
    return VOCAB * D + CTX * D + L * (4 * D * D + 2 * HID * D);
}

void tiny_set_creativity(int c)
{
    if (c < 0) c = 0;
    if (c > 9) c = 9;
    creativity = c;
}

int tiny_creativity(void) { return creativity; }

void tiny_set_abort(int (*fn)(void)) { abort_check = fn; }

/* ---- loading ------------------------------------------------------ */

static void bad(const char *msg)
{
    snprintf(errmsg, sizeof errmsg, "%s", msg);
    load_error = errmsg;
}

static void *get_memory(unsigned long n)
{
    void *p = malloc(n);
    if (p == NULL)
        bad("not enough memory.");
    return p;
}

static int damaged(void)
{
    bad("TINY.BIN is too short or damaged.");
    return 0;
}

static int load_table(FILE *fp, Table *t, int rows, int cols)
{
    int r;
    t->rows = rows;
    t->cols = cols;
    t->scale = (float *)get_memory((unsigned long)rows * sizeof(float));
    t->w = (signed char *)get_memory((unsigned long)rows * (unsigned long)cols);
    if (t->scale == NULL || t->w == NULL)
        return 0;
    for (r = 0; r < rows; r++)
        if (fread(&t->scale[r], 4, 1, fp) != 1 ||
            fread(t->w + (unsigned long)r * cols, 1, cols, fp) != (size_t)cols)
            return damaged();
    return 1;
}

static float *load_gains(FILE *fp)
{
    float *g = (float *)get_memory((unsigned long)D * sizeof(float));
    if (g == NULL)
        return NULL;
    if (fread(g, 4, D, fp) != (size_t)D) {
        damaged();
        return NULL;
    }
    return g;
}

static int read16(FILE *fp)
{
    int lo = fgetc(fp), hi = fgetc(fp);
    if (hi == EOF) {
        damaged();
        return -1;
    }
    return lo | (hi << 8);
}

static int load_model(const char *name, void (*on_progress)(int, int))
{
    FILE *fp;
    char magic[4];
    int l, i, step, steps;

    if (sizeof(float) != 4) {
        bad("this compiler has an unusual float size.");
        return 0;
    }
    fp = fopen(name, "rb");
    if (fp == NULL) {
        bad("cannot open TINY.BIN.");
        return 0;
    }
    if (fread(magic, 1, 4, fp) != 4 || memcmp(magic, "TNY2", 4) != 0) {
        fclose(fp);
        bad("TINY.BIN is not a version 2 network file.");
        return 0;
    }
    D = read16(fp); L = read16(fp); H = read16(fp); HID = read16(fp);
    if (D < 8 || D > MAXD || L < 1 || L > MAXL || H < 1 || D % H != 0 ||
        HID < 8 || HID > MAXHID) {
        fclose(fp);
        bad("TINY.BIN has a network size this program cannot handle.");
        return 0;
    }
    HD = D / H;
    att_div = (float)sqrt((double)HD);
    steps = 3 + 6 * L;
    step = 0;

    if (!load_table(fp, &emb, VOCAB, D)) goto fail;
    if (on_progress) on_progress(++step, steps);
    if (!load_table(fp, &pos, CTX, D)) goto fail;
    if (on_progress) on_progress(++step, steps);
    for (l = 0; l < L; l++) {
        if ((g1[l] = load_gains(fp)) == NULL) goto fail;
        if (!load_table(fp, &wq[l], D, D)) goto fail;
        if (on_progress) on_progress(++step, steps);
        if (!load_table(fp, &wk[l], D, D)) goto fail;
        if (on_progress) on_progress(++step, steps);
        if (!load_table(fp, &wv[l], D, D)) goto fail;
        if (on_progress) on_progress(++step, steps);
        if (!load_table(fp, &wo[l], D, D)) goto fail;
        if (on_progress) on_progress(++step, steps);
        if ((g2[l] = load_gains(fp)) == NULL) goto fail;
        if (!load_table(fp, &f1[l], HID, D)) goto fail;
        if (on_progress) on_progress(++step, steps);
        if (!load_table(fp, &f2[l], D, HID)) goto fail;
        if (on_progress) on_progress(++step, steps);
    }
    gf = load_gains(fp);
    if (gf == NULL) goto fail;
    if (fgetc(fp) != EOF) {
        fclose(fp);
        bad("TINY.BIN has the wrong size.");
        return 0;
    }
    (void)i;
    fclose(fp);
    return 1;

fail:
    fclose(fp);
    return 0;
}

/* ---- the network ------------------------------------------------- */

/* Memory of earlier letters: for every layer and position, the "key" and
   "value" lists of D numbers, kept as bytes with one scale per list. */
static signed char *kc[MAXL], *vc[MAXL];
static float *ks[MAXL], *vs[MAXL];

/* Work buffers (static, so they do not use a small stack). */
static float bx[MAXD], bh[MAXD], bq[MAXD], bt[MAXD], ba[MAXD], bm[MAXHID], bs[CTX];
static float score[VOCAB];

static int alloc_memory(void)
{
    int l;
    for (l = 0; l < L; l++) {
        kc[l] = (signed char *)get_memory((unsigned long)CTX * D);
        vc[l] = (signed char *)get_memory((unsigned long)CTX * D);
        ks[l] = (float *)get_memory(CTX * sizeof(float));
        vs[l] = (float *)get_memory(CTX * sizeof(float));
        if (!kc[l] || !vc[l] || !ks[l] || !vs[l])
            return 0;
    }
    return 1;
}

/* y = table * x */
static void matvec(const Table *t, const float *x, float *y)
{
    int r, c, cols = t->cols;
    const signed char *w = t->w;
    float s;
    for (r = 0; r < t->rows; r++) {
        s = 0.0f;
        for (c = 0; c < cols; c++)
            s += w[c] * x[c];
        y[r] = s * t->scale[r];
        w += cols;
    }
}

/* y = x scaled to a standard size, times the gains g */
static void norm(const float *x, const float *g, float *y)
{
    int i;
    float s = 0.0f;
    for (i = 0; i < D; i++)
        s += x[i] * x[i];
    s = (float)(1.0 / sqrt(s / D + 1e-5));
    for (i = 0; i < D; i++)
        y[i] = x[i] * s * g[i];
}

/* e to the power x, quietly 0 for very negative x */
static float safe_exp(float x)
{
    if (x < -60.0f)
        return 0.0f;
    return (float)exp((double)x);
}

/* keep a list of D numbers as bytes, returning the scale */
static float store(signed char *dst, const float *src)
{
    int i;
    float big = 0.0f, v, k;
    for (i = 0; i < D; i++) {
        v = src[i] < 0 ? -src[i] : src[i];
        if (v > big) big = v;
    }
    if (big < 1e-20f)
        big = 1e-20f;
    k = 127.0f / big;
    for (i = 0; i < D; i++) {
        v = src[i] * k;
        dst[i] = (signed char)(v < 0 ? v - 0.5f : v + 0.5f);
    }
    return big / 127.0f;
}

/* Feed letter number `tok` at position `p` (0, 1, 2, ...).
   Fills score[0..VOCAB-1]: how well each letter fits as the next one. */
static void step(int tok, int p, float *sc)
{
    int i, j, l, h;
    const signed char *e = emb.w + tok * D;
    const signed char *q = pos.w + p * D;
    const signed char *kp, *vp;
    float s, big, sum, es = emb.scale[tok], ps = pos.scale[p];

    for (i = 0; i < D; i++)
        bx[i] = e[i] * es + q[i] * ps;

    for (l = 0; l < L; l++) {
        /* look back at the earlier letters */
        norm(bx, g1[l], bh);
        matvec(&wk[l], bh, bt);
        ks[l][p] = store(kc[l] + p * D, bt);
        matvec(&wv[l], bh, bt);
        vs[l][p] = store(vc[l] + p * D, bt);
        matvec(&wq[l], bh, bq);
        for (h = 0; h < H; h++) {
            big = -1e30f;
            for (j = 0; j <= p; j++) {
                kp = kc[l] + j * D + h * HD;
                s = 0.0f;
                for (i = 0; i < HD; i++)
                    s += bq[h * HD + i] * kp[i];
                s = s * ks[l][j] / att_div;
                bs[j] = s;
                if (s > big) big = s;
            }
            sum = 0.0f;
            for (j = 0; j <= p; j++) {
                bs[j] = safe_exp(bs[j] - big);
                sum += bs[j];
            }
            for (i = 0; i < HD; i++)
                ba[h * HD + i] = 0.0f;
            for (j = 0; j <= p; j++) {
                if (bs[j] == 0.0f)
                    continue;
                vp = vc[l] + j * D + h * HD;
                s = bs[j] * vs[l][j] / sum;
                for (i = 0; i < HD; i++)
                    ba[h * HD + i] += s * vp[i];
            }
        }
        matvec(&wo[l], ba, bt);
        for (i = 0; i < D; i++)
            bx[i] += bt[i];

        /* think about it */
        norm(bx, g2[l], bh);
        matvec(&f1[l], bh, bm);
        s = 0.0f;
        for (i = 0; i < HID; i++) {
            if (bm[i] < 0.0f) bm[i] = 0.0f;
            else s += bm[i];
        }
        layer_act[l] = s / HID;                /* for the activity display */
        matvec(&f2[l], bm, bt);
        for (i = 0; i < D; i++)
            bx[i] += bt[i];
    }
    norm(bx, gf, bh);
    matvec(&emb, bh, sc);
}

/* ---- choosing letters -------------------------------------------- */

static int chosen(int tok)
{
    int i;
    float sum = 0.0f;
    for (i = 0; i < VOCAB; i++)
        sum += safe_exp(score[i] - score[tok]);
    last_conf = 1.0f / sum;
    return tok;
}

/* Pick the next letter. With creativity 0 it is always the best one.
   Otherwise choose at random among the letters that fit nearly as well,
   so the same question gets differently worded answers. */
static int pick(void)
{
    int i, best = 0;
    float t, sum = 0.0f, r;

    for (i = 1; i < VOCAB; i++)
        if (score[i] > score[best]) best = i;
    if (creativity <= 0)
        return chosen(best);
    t = creativity / 5.0f;
    for (i = 0; i < VOCAB; i++) {
        bs[i] = safe_exp((score[i] - score[best]) / t);
        if (bs[i] < 0.15f) bs[i] = 0.0f;       /* never pick unlikely letters */
        sum += bs[i];
    }
    r = sum * (float)(rand() % 10000) / 10000.0f;
    for (i = 0; i < VOCAB; i++) {
        r -= bs[i];
        if (r < 0.0f && bs[i] > 0.0f) return chosen(i);
    }
    return chosen(best);
}

static long now_ms(void)
{
#ifdef __3DS__
    return (long)(svcGetSystemTick() * 1000ULL / (u64)SYSCLOCK_ARM11);
#else
    return (long)((double)clock() * 1000.0 / (double)CLOCKS_PER_SEC);
#endif
}

int tiny_answer(const char *q, char *out, long *ms,
                void (*on_step)(int i, int n), void (*on_letter)(int c))
{
    int p = 0, n = (int)strlen(q), tok, letters = 0;
    long t0 = now_ms();

    for (; q[p]; p++) {
        if (abort_check && abort_check()) break;
        if (on_step) on_step(p, n);
        step(q[p] - 30, p, score);
    }
    if (!(abort_check && abort_check()))
        step(1, p++, score);                   /* end of the question */
    for (; p < CTX; p++) {
        if (abort_check && abort_check()) break;
        tok = pick();
        if (tok <= 1) break;                   /* end of the answer */
        out[letters++] = (char)(tok + 30);
        if (on_letter) on_letter(tok + 30);
        step(tok, p, score);
    }
    out[letters] = 0;
    *ms = now_ms() - t0;
    return letters;
}

/* Clean up a typed line: printable letters only, no trailing spaces,
   and CAPS LOCK text turned into lower case (the network knows it better). */
int tiny_clean(const char *line, char *q)
{
    int i, n = 0, lower = 0, upper = 0;
    for (i = 0; line[i] && n < TINY_MAXQ; i++)
        if (line[i] >= 32 && line[i] <= 126)
            q[n++] = line[i];
    while (n > 0 && q[n - 1] == ' ')
        n--;
    q[n] = 0;
    for (i = 0; i < n; i++) {
        if (q[i] >= 'a' && q[i] <= 'z') lower++;
        if (q[i] >= 'A' && q[i] <= 'Z') upper++;
    }
    if (upper > 0 && lower == 0)
        for (i = 0; i < n; i++)
            if (q[i] >= 'A' && q[i] <= 'Z') q[i] += 'a' - 'A';
    return n;
}

int tiny_is_word(const char *q, const char *w)
{
    int i;
    for (i = 0; q[i] && w[i]; i++)
        if ((q[i] | 32) != w[i]) return 0;
    return q[i] == 0 && w[i] == 0;
}

/* ---- entry point ------------------------------------------------- */

int tiny_load(const char *path, void (*on_progress)(int done, int total))
{
    if (!load_model(path, on_progress))
        return -1;
    if (!alloc_memory())
        return -1;
    return 0;
}
