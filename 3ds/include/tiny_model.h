/* tiny_model.h - the neural network from TINY.C, without the DOS screen code.
   Plain ANSI C, so it builds for the 3DS and for the host (for testing). */

#ifndef TINY_MODEL_H
#define TINY_MODEL_H

#define TINY_MAXQ 72            /* longest question, in letters */

/* load the network; 0 on success, -1 on failure (see tiny_error) */
int tiny_load(const char *path, void (*on_progress)(int done, int total));

/* called between letters; return non-zero to stop generating an answer */
void tiny_set_abort(int (*fn)(void));

/* ask `q`; fills `out` and returns its length. `*ms` gets the time taken. */
int tiny_answer(const char *q, char *out, long *ms,
                void (*on_step)(int i, int n), void (*on_letter)(int c));

int tiny_clean(const char *line, char *q);
int tiny_is_word(const char *a, const char *b);

void tiny_set_creativity(int c);
int tiny_creativity(void);

int tiny_layers(void);
int tiny_width(void);
int tiny_weights(void);
float tiny_conf(void);
float tiny_layer_act(int l);
const char *tiny_error(void);

#endif
