/*
 * The Python Imaging Library
 * $Id$
 *
 * image quantizer
 *
 * history:
 * 1998-09-10 tjs  Contributed
 * 1998-12-29 fl   Added to PIL 1.0b1
 * 2004-02-21 fl   Fixed bogus free() on quantization error
 * 2005-02-07 fl   Limit number of colors to 256
 *
 * Written by Toby J Sargeant <tjs@longford.cs.monash.edu.au>.
 *
 * Copyright (c) 1998 by Toby J Sargeant
 * Copyright (c) 1998-2004 by Secret Labs AB.  All rights reserved.
 *
 * See the README file for information on usage and redistribution.
 */
/* (Ultimate No Mercy, firstrun 2026-11-17) Pillow 12.3.0's src/libImaging/Quant.c, only the median cut's palette: the lines
 * from the file's own definitions to annotate_hash_table, and compute_palette_from_median_cut, as they are (no line
 * changed); Imaging.h and the other quantizers left out. LICENSE.txt beside this file is Pillow's licence. Used by the game's
 * first start to make one stage-set picture exactly as mod/gen/arenas_stages.py's make_picture made it with
 * PIL's Image.quantize(colors, method=MEDIANCUT): see firstrun_median_cut at the end. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "QuantTypes.h"
#include "QuantHash.h"
#include "QuantHeap.h"

/* MSVC9.0 */
#ifndef UINT32_MAX
#define UINT32_MAX 0xffffffff
#endif

// #define DEBUG
// #define TEST_NEAREST_NEIGHBOUR

typedef struct {
    uint32_t scale;
} PixelHashData;

typedef struct _PixelList {
    struct _PixelList *next[3], *prev[3];
    Pixel p;
    unsigned int flag : 1;
    int count;
} PixelList;

typedef struct _BoxNode {
    struct _BoxNode *l, *r;
    PixelList *head[3], *tail[3];
    int axis;
    int volume;
    uint32_t pixelCount;
} BoxNode;

#define _SQR(x) ((x) * (x))
#define _DISTSQR(p1, p2)                            \
    _SQR((int)((p1)->c.r) - (int)((p2)->c.r)) +     \
        _SQR((int)((p1)->c.g) - (int)((p2)->c.g)) + \
        _SQR((int)((p1)->c.b) - (int)((p2)->c.b))

#define MAX_HASH_ENTRIES 65536

#define PIXEL_HASH(r, g, b)                                         \
    (((unsigned int)(r)) * 463 ^ ((unsigned int)(g) << 8) * 10069 ^ \
     ((unsigned int)(b) << 16) * 64997)

#define PIXEL_UNSCALE(p, q, s)                                  \
    ((q)->c.r = (p)->c.r << (s)), ((q)->c.g = (p)->c.g << (s)), \
        ((q)->c.b = (p)->c.b << (s))

#define PIXEL_SCALE(p, q, s)                                    \
    ((q)->c.r = (p)->c.r >> (s)), ((q)->c.g = (p)->c.g >> (s)), \
        ((q)->c.b = (p)->c.b >> (s))

static uint32_t
unshifted_pixel_hash(const HashTable *h, const Pixel pixel) {
    return PIXEL_HASH(pixel.c.r, pixel.c.g, pixel.c.b);
}

static int
unshifted_pixel_cmp(const HashTable *h, const Pixel pixel1, const Pixel pixel2) {
    if (pixel1.c.r == pixel2.c.r) {
        if (pixel1.c.g == pixel2.c.g) {
            if (pixel1.c.b == pixel2.c.b) {
                return 0;
            } else {
                return (int)(pixel1.c.b) - (int)(pixel2.c.b);
            }
        } else {
            return (int)(pixel1.c.g) - (int)(pixel2.c.g);
        }
    } else {
        return (int)(pixel1.c.r) - (int)(pixel2.c.r);
    }
}

static uint32_t
pixel_hash(const HashTable *h, const Pixel pixel) {
    PixelHashData *d = (PixelHashData *)hashtable_get_user_data(h);
    return PIXEL_HASH(
        pixel.c.r >> d->scale, pixel.c.g >> d->scale, pixel.c.b >> d->scale
    );
}

static int
pixel_cmp(const HashTable *h, const Pixel pixel1, const Pixel pixel2) {
    PixelHashData *d = (PixelHashData *)hashtable_get_user_data(h);
    uint32_t A, B;
    A = PIXEL_HASH(
        pixel1.c.r >> d->scale, pixel1.c.g >> d->scale, pixel1.c.b >> d->scale
    );
    B = PIXEL_HASH(
        pixel2.c.r >> d->scale, pixel2.c.g >> d->scale, pixel2.c.b >> d->scale
    );
    return (A == B) ? 0 : ((A < B) ? -1 : 1);
}

static void
exists_count_func(const HashTable *h, const Pixel key, uint32_t *val) {
    *val += 1;
}

static void
new_count_func(const HashTable *h, const Pixel key, uint32_t *val) {
    *val = 1;
}

static void
rehash_collide(
    const HashTable *h, Pixel *keyp, uint32_t *valp, Pixel newkey, uint32_t newval
) {
    *valp += newval;
}

/* %% */

static HashTable *
create_pixel_hash(Pixel *pixelData, uint32_t nPixels) {
    PixelHashData *d;
    HashTable *hash;
    uint32_t i;
#ifdef DEBUG
    uint32_t timer, timer2, timer3;
#endif

    /* malloc check ok, small constant allocation */
    d = malloc(sizeof(PixelHashData));
    if (!d) {
        return NULL;
    }
    hash = hashtable_new(pixel_hash, pixel_cmp);
    hashtable_set_user_data(hash, d);
    d->scale = 0;
#ifdef DEBUG
    timer = timer3 = clock();
#endif
    for (i = 0; i < nPixels; i++) {
        if (!hashtable_insert_or_update_computed(
                hash, pixelData[i], new_count_func, exists_count_func
            )) {
            ;
        }
        while (hashtable_get_count(hash) > MAX_HASH_ENTRIES) {
            d->scale++;
#ifdef DEBUG
            printf("rehashing - new scale: %d\n", (int)d->scale);
            timer2 = clock();
#endif
            hashtable_rehash_compute(hash, rehash_collide);
#ifdef DEBUG
            timer2 = clock() - timer2;
            printf("rehash took %f sec\n", timer2 / (double)CLOCKS_PER_SEC);
            timer += timer2;
#endif
        }
    }
#ifdef DEBUG
    printf("inserts took %f sec\n", (clock() - timer) / (double)CLOCKS_PER_SEC);
#endif
#ifdef DEBUG
    printf("total        %f sec\n", (clock() - timer3) / (double)CLOCKS_PER_SEC);
#endif
    return hash;
}

static void
destroy_pixel_hash(HashTable *hash) {
    PixelHashData *d = (PixelHashData *)hashtable_get_user_data(hash);
    if (d) {
        free(d);
    }
    hashtable_free(hash);
}

/* 1. hash quantized pixels.                                                  */
/* 2. create R,G,B lists of sorted quantized pixels.                          */
/* 3. median cut.                                                             */
/* 4. build hash table from median cut boxes.                                 */
/* 5. for each pixel, compute entry in hash table, and hence median cut box.  */
/* 6. compute median cut box pixel averages.                                  */
/* 7. map each pixel to nearest average.                                      */

static int
compute_box_volume(BoxNode *b) {
    unsigned char rl, rh, gl, gh, bl, bh;
    if (b->volume >= 0) {
        return b->volume;
    }
    if (!b->head[0]) {
        b->volume = 0;
    } else {
        rh = b->head[0]->p.c.r;
        rl = b->tail[0]->p.c.r;
        gh = b->head[1]->p.c.g;
        gl = b->tail[1]->p.c.g;
        bh = b->head[2]->p.c.b;
        bl = b->tail[2]->p.c.b;
        b->volume = (rh - rl + 1) * (gh - gl + 1) * (bh - bl + 1);
    }
    return b->volume;
}

static void
hash_to_list(const HashTable *h, const Pixel pixel, const uint32_t count, void *u) {
    PixelHashData *d = (PixelHashData *)hashtable_get_user_data(h);
    PixelList **pl = (PixelList **)u;
    PixelList *p;
    int i;
    Pixel q;

    PIXEL_SCALE(&pixel, &q, d->scale);

    /* malloc check ok, small constant allocation */
    p = malloc(sizeof(PixelList));
    if (!p) {
        return;
    }

    p->flag = 0;
    p->p = q;
    p->count = count;
    for (i = 0; i < 3; i++) {
        p->next[i] = pl[i];
        p->prev[i] = NULL;
        if (pl[i]) {
            pl[i]->prev[i] = p;
        }
        pl[i] = p;
    }
}

static PixelList *
mergesort_pixels(PixelList *head, int i) {
    PixelList *c, *t, *a, *b, *p;
    if (!head || !head->next[i]) {
        if (head) {
            head->next[i] = NULL;
            head->prev[i] = NULL;
        }
        return head;
    }
    for (c = t = head; c && t;
         c = c->next[i], t = (t->next[i]) ? t->next[i]->next[i] : NULL);
    if (c) {
        if (c->prev[i]) {
            c->prev[i]->next[i] = NULL;
        }
        c->prev[i] = NULL;
    }
    a = mergesort_pixels(head, i);
    b = mergesort_pixels(c, i);
    head = NULL;
    p = NULL;
    while (a && b) {
        if (a->p.a.v[i] > b->p.a.v[i]) {
            c = a;
            a = a->next[i];
        } else {
            c = b;
            b = b->next[i];
        }
        c->prev[i] = p;
        c->next[i] = NULL;
        if (p) {
            p->next[i] = c;
        }
        p = c;
        if (!head) {
            head = c;
        }
    }
    if (a) {
        c->next[i] = a;
        a->prev[i] = c;
    } else if (b) {
        c->next[i] = b;
        b->prev[i] = c;
    }
    return head;
}

#ifdef DEBUG
static int
test_sorted(PixelList *pl[3]) {
    int i, l;
    PixelList *t;

    for (i = 0; i < 3; i++) {
        l = 256;
        for (t = pl[i]; t; t = t->next[i]) {
            if (l < t->p.a.v[i]) {
                return 0;
            }
            l = t->p.a.v[i];
        }
    }
    return 1;
}
#endif

static int
box_heap_cmp(const Heap *h, const void *A, const void *B) {
    BoxNode *a = (BoxNode *)A;
    BoxNode *b = (BoxNode *)B;
    return (int)a->pixelCount - (int)b->pixelCount;
}

#define LUMINANCE(p) (77 * (p)->c.r + 150 * (p)->c.g + 29 * (p)->c.b)

static int
splitlists(
    PixelList *h[3],
    PixelList *t[3],
    PixelList *nh[2][3],
    PixelList *nt[2][3],
    uint32_t nCount[2],
    int axis,
    uint32_t pixelCount
) {
    uint32_t left;

    PixelList *l, *r, *c, *n;
    int i;
    int nRight;
#ifdef DEBUG
    int nLeft;
#endif
    int splitColourVal;

#ifdef DEBUG
    {
        PixelList *_prevTest, *_nextTest;
        int _i, _nextCount[3], _prevCount[3];
        for (_i = 0; _i < 3; _i++) {
            for (_nextCount[_i] = 0, _nextTest = h[_i];
                 _nextTest && _nextTest->next[_i];
                 _nextTest = _nextTest->next[_i], _nextCount[_i]++);
            for (_prevCount[_i] = 0, _prevTest = t[_i];
                 _prevTest && _prevTest->prev[_i];
                 _prevTest = _prevTest->prev[_i], _prevCount[_i]++);
            if (_nextTest != t[_i]) {
                printf("next-list of axis %d does not end at tail\n", _i);
                exit(1);
            }
            if (_prevTest != h[_i]) {
                printf("prev-list of axis %d does not end at head\n", _i);
                exit(1);
            }
            for (; _nextTest && _nextTest->prev[_i]; _nextTest = _nextTest->prev[_i]);
            for (; _prevTest && _prevTest->next[_i]; _prevTest = _prevTest->next[_i]);
            if (_nextTest != h[_i]) {
                printf("next-list of axis %d does not loop back to head\n", _i);
                exit(1);
            }
            if (_prevTest != t[_i]) {
                printf("prev-list of axis %d does not loop back to tail\n", _i);
                exit(1);
            }
        }
        for (_i = 1; _i < 3; _i++) {
            if (_prevCount[_i] != _prevCount[_i - 1] ||
                _nextCount[_i] != _nextCount[_i - 1] ||
                _prevCount[_i] != _nextCount[_i]) {
                printf(
                    "{%d %d %d} {%d %d %d}\n",
                    _prevCount[0],
                    _prevCount[1],
                    _prevCount[2],
                    _nextCount[0],
                    _nextCount[1],
                    _nextCount[2]
                );
                exit(1);
            }
        }
    }
#endif
    nCount[0] = nCount[1] = 0;
    nRight = 0;
#ifdef DEBUG
    nLeft = 0;
#endif
    for (left = 0, c = h[axis]; c;) {
        left = left + c->count;
        nCount[0] += c->count;
        c->flag = 0;
#ifdef DEBUG
        nLeft++;
#endif
        c = c->next[axis];
        if (left * 2 > pixelCount) {
            break;
        }
    }
    if (c) {
        splitColourVal = c->prev[axis]->p.a.v[axis];
        for (; c; c = c->next[axis]) {
            if (splitColourVal != c->p.a.v[axis]) {
                break;
            }
            c->flag = 0;
#ifdef DEBUG
            nLeft++;
#endif
            nCount[0] += c->count;
        }
    }
    for (; c; c = c->next[axis]) {
        c->flag = 1;
        nRight++;
        nCount[1] += c->count;
    }
    if (!nRight) {
        for (c = t[axis], splitColourVal = t[axis]->p.a.v[axis]; c; c = c->prev[axis]) {
            if (splitColourVal != c->p.a.v[axis]) {
                break;
            }
            c->flag = 1;
            nRight++;
#ifdef DEBUG
            nLeft--;
#endif
            nCount[0] -= c->count;
            nCount[1] += c->count;
        }
    }
#ifdef DEBUG
    if (!nLeft) {
        for (c = h[axis]; c; c = c->next[axis]) {
            printf("[%d %d %d]\n", c->p.c.r, c->p.c.g, c->p.c.b);
        }
        printf("warning... trivial split\n");
    }
#endif

    for (i = 0; i < 3; i++) {
        l = r = NULL;
        nh[0][i] = nt[0][i] = NULL;
        nh[1][i] = nt[1][i] = NULL;
        for (c = h[i]; c; c = n) {
            n = c->next[i];
            if (c->flag) { /* move pixel to right  list*/
                if (r) {
                    r->next[i] = c;
                } else {
                    nh[1][i] = c;
                }
                c->prev[i] = r;
                r = c;
            } else { /* move pixel to left list */
                if (l) {
                    l->next[i] = c;
                } else {
                    nh[0][i] = c;
                }
                c->prev[i] = l;
                l = c;
            }
        }
        if (l) {
            l->next[i] = NULL;
        }
        if (r) {
            r->next[i] = NULL;
        }
        nt[0][i] = l;
        nt[1][i] = r;
    }
    return 1;
}

static int
split(BoxNode *node) {
    unsigned char rl, rh, gl, gh, bl, bh;
    int f[3];
    int best, axis;
    int i;
    PixelList *heads[2][3];
    PixelList *tails[2][3];
    uint32_t newCounts[2];
    BoxNode *left, *right;

    rh = node->head[0]->p.c.r;
    rl = node->tail[0]->p.c.r;
    gh = node->head[1]->p.c.g;
    gl = node->tail[1]->p.c.g;
    bh = node->head[2]->p.c.b;
    bl = node->tail[2]->p.c.b;
#ifdef DEBUG
    printf("splitting node [%d %d %d] [%d %d %d] ", rl, gl, bl, rh, gh, bh);
#endif
    f[0] = (rh - rl) * 77;
    f[1] = (gh - gl) * 150;
    f[2] = (bh - bl) * 29;

    best = f[0];
    axis = 0;
    for (i = 1; i < 3; i++) {
        if (best < f[i]) {
            best = f[i];
            axis = i;
        }
    }
#ifdef DEBUG
    printf("along axis %d\n", axis + 1);
    {
        PixelList *_prevTest, *_nextTest;
        int _i, _nextCount[3], _prevCount[3];
        for (_i = 0; _i < 3; _i++) {
            if (node->tail[_i]->next[_i]) {
                printf("tail is not tail\n");
                printf(
                    "node->tail[%d]->next[%d]=%p\n", _i, _i, node->tail[_i]->next[_i]
                );
            }
            if (node->head[_i]->prev[_i]) {
                printf("head is not head\n");
                printf(
                    "node->head[%d]->prev[%d]=%p\n", _i, _i, node->head[_i]->prev[_i]
                );
            }
        }

        for (_i = 0; _i < 3; _i++) {
            for (_nextCount[_i] = 0, _nextTest = node->head[_i];
                 _nextTest && _nextTest->next[_i];
                 _nextTest = _nextTest->next[_i], _nextCount[_i]++);
            for (_prevCount[_i] = 0, _prevTest = node->tail[_i];
                 _prevTest && _prevTest->prev[_i];
                 _prevTest = _prevTest->prev[_i], _prevCount[_i]++);
            if (_nextTest != node->tail[_i]) {
                printf("next-list of axis %d does not end at tail\n", _i);
            }
            if (_prevTest != node->head[_i]) {
                printf("prev-list of axis %d does not end at head\n", _i);
            }
            for (; _nextTest && _nextTest->prev[_i]; _nextTest = _nextTest->prev[_i]);
            for (; _prevTest && _prevTest->next[_i]; _prevTest = _prevTest->next[_i]);
            if (_nextTest != node->head[_i]) {
                printf("next-list of axis %d does not loop back to head\n", _i);
            }
            if (_prevTest != node->tail[_i]) {
                printf("prev-list of axis %d does not loop back to tail\n", _i);
            }
        }
        for (_i = 1; _i < 3; _i++) {
            if (_prevCount[_i] != _prevCount[_i - 1] ||
                _nextCount[_i] != _nextCount[_i - 1] ||
                _prevCount[_i] != _nextCount[_i]) {
                printf(
                    "{%d %d %d} {%d %d %d}\n",
                    _prevCount[0],
                    _prevCount[1],
                    _prevCount[2],
                    _nextCount[0],
                    _nextCount[1],
                    _nextCount[2]
                );
            }
        }
    }
#endif
    node->axis = axis;
    if (!splitlists(
            node->head, node->tail, heads, tails, newCounts, axis, node->pixelCount
        )) {
#ifdef DEBUG
        printf("list split failed.\n");
#endif
        return 0;
    }
#ifdef DEBUG
    if (!test_sorted(heads[0])) {
        printf("bug in split");
        exit(1);
    }
    if (!test_sorted(heads[1])) {
        printf("bug in split");
        exit(1);
    }
#endif
    /* malloc check ok, small constant allocation */
    left = malloc(sizeof(BoxNode));
    right = malloc(sizeof(BoxNode));
    if (!left || !right) {
        free(left);
        free(right);
        return 0;
    }
    for (i = 0; i < 3; i++) {
        left->head[i] = heads[0][i];
        left->tail[i] = tails[0][i];
        right->head[i] = heads[1][i];
        right->tail[i] = tails[1][i];
        node->head[i] = NULL;
        node->tail[i] = NULL;
    }
#ifdef DEBUG
    if (left->head[0]) {
        rh = left->head[0]->p.c.r;
        rl = left->tail[0]->p.c.r;
        gh = left->head[1]->p.c.g;
        gl = left->tail[1]->p.c.g;
        bh = left->head[2]->p.c.b;
        bl = left->tail[2]->p.c.b;
        printf("   left node  [%3d %3d %3d] [%3d %3d %3d]\n", rl, gl, bl, rh, gh, bh);
    }
    if (right->head[0]) {
        rh = right->head[0]->p.c.r;
        rl = right->tail[0]->p.c.r;
        gh = right->head[1]->p.c.g;
        gl = right->tail[1]->p.c.g;
        bh = right->head[2]->p.c.b;
        bl = right->tail[2]->p.c.b;
        printf("   right node [%3d %3d %3d] [%3d %3d %3d]\n", rl, gl, bl, rh, gh, bh);
    }
#endif
    left->l = left->r = NULL;
    right->l = right->r = NULL;
    left->axis = right->axis = -1;
    left->volume = right->volume = -1;
    left->pixelCount = newCounts[0];
    right->pixelCount = newCounts[1];
    node->l = left;
    node->r = right;
    return 1;
}

static BoxNode *
median_cut(PixelList *hl[3], uint32_t imPixelCount, int nPixels) {
    PixelList *tl[3];
    int i;
    BoxNode *root;
    Heap *h;
    BoxNode *thisNode;

    h = ImagingQuantHeapNew(box_heap_cmp);
    /* malloc check ok, small constant allocation */
    root = malloc(sizeof(BoxNode));
    if (!root) {
        ImagingQuantHeapFree(h);
        return NULL;
    }
    for (i = 0; i < 3; i++) {
        for (tl[i] = hl[i]; tl[i] && tl[i]->next[i]; tl[i] = tl[i]->next[i]);
        root->head[i] = hl[i];
        root->tail[i] = tl[i];
    }
    root->l = root->r = NULL;
    root->axis = -1;
    root->volume = -1;
    root->pixelCount = imPixelCount;

    ImagingQuantHeapAdd(h, (void *)root);
    while (--nPixels) {
        do {
            if (!ImagingQuantHeapRemove(h, (void **)&thisNode)) {
                goto done;
            }
        } while (compute_box_volume(thisNode) == 1);
        if (!split(thisNode)) {
#ifdef DEBUG
            printf("Oops, split failed...\n");
#endif
            exit(1);
        }
        ImagingQuantHeapAdd(h, (void *)(thisNode->l));
        ImagingQuantHeapAdd(h, (void *)(thisNode->r));
    }
done:
    ImagingQuantHeapFree(h);
    return root;
}

static void
free_box_tree(BoxNode *n) {
    PixelList *p, *pp;
    if (n->l) {
        free_box_tree(n->l);
    }
    if (n->r) {
        free_box_tree(n->r);
    }
    for (p = n->head[0]; p; p = pp) {
        pp = p->next[0];
        free(p);
    }
    free(n);
}

#ifdef DEBUG
static int
checkContained(BoxNode *n, Pixel *pp) {
    if (n->l && n->r) {
        return checkContained(n->l, pp) + checkContained(n->r, pp);
    }
    if (n->l || n->r) {
        printf("box tree is dead\n");
        return 0;
    }
    if (pp->c.r <= n->head[0]->p.c.r && pp->c.r >= n->tail[0]->p.c.r &&
        pp->c.g <= n->head[1]->p.c.g && pp->c.g >= n->tail[1]->p.c.g &&
        pp->c.b <= n->head[2]->p.c.b && pp->c.b >= n->tail[2]->p.c.b) {
        return 1;
    }
    return 0;
}
#endif

static int
annotate_hash_table(BoxNode *n, HashTable *h, uint32_t *box) {
    PixelList *p;
    PixelHashData *d = (PixelHashData *)hashtable_get_user_data(h);
    Pixel q;
    if (n->l && n->r) {
        return annotate_hash_table(n->l, h, box) && annotate_hash_table(n->r, h, box);
    }
    if (n->l || n->r) {
#ifdef DEBUG
        printf("box tree is dead\n");
#endif
        return 0;
    }
    for (p = n->head[0]; p; p = p->next[0]) {
        PIXEL_UNSCALE(&(p->p), &q, d->scale);
        if (!hashtable_insert(h, q, *box)) {
#ifdef DEBUG
            printf("hashtable insert failed\n");
#endif
            return 0;
        }
    }
    if (n->head[0]) {
        (*box)++;
    }
    return 1;
}

typedef struct {
    uint32_t *distance;
    uint32_t index;
} DistanceWithIndex;


static int
compute_palette_from_median_cut(
    Pixel *pixelData,
    uint32_t nPixels,
    HashTable *medianBoxHash,
    Pixel **palette,
    uint32_t nPaletteEntries,
    BoxNode *root
) {
    uint32_t i;
    uint32_t paletteEntry;
    Pixel *p;
    uint32_t *avg[3];
    uint32_t *count;

    *palette = NULL;
    /* malloc check ok, using calloc */
    if (!(count = calloc(nPaletteEntries, sizeof(uint32_t)))) {
        return 0;
    }
    for (i = 0; i < 3; i++) {
        avg[i] = NULL;
    }
    for (i = 0; i < 3; i++) {
        /* malloc check ok, using calloc */
        if (!(avg[i] = calloc(nPaletteEntries, sizeof(uint32_t)))) {
            for (i = 0; i < 3; i++) {
                if (avg[i]) {
                    free(avg[i]);
                }
            }
            free(count);
            return 0;
        }
    }
    for (i = 0; i < nPixels; i++) {
#ifdef DEBUG
        if (!(i % 100)) {
            printf("%05d\r", i);
            fflush(stdout);
        }
        if (checkContained(root, pixelData + i) > 1) {
            printf("pixel in two boxes\n");
            for (i = 0; i < 3; i++) {
                free(avg[i]);
            }
            free(count);
            return 0;
        }
#endif
        if (!hashtable_lookup(medianBoxHash, pixelData[i], &paletteEntry)) {
#ifdef DEBUG
            printf("pixel lookup failed\n");
#endif
            for (i = 0; i < 3; i++) {
                free(avg[i]);
            }
            free(count);
            return 0;
        }
        if (paletteEntry >= nPaletteEntries) {
#ifdef DEBUG
            printf(
                "panic - paletteEntry>=nPaletteEntries (%d>=%d)\n",
                (int)paletteEntry,
                (int)nPaletteEntries
            );
#endif
            for (i = 0; i < 3; i++) {
                free(avg[i]);
            }
            free(count);
            return 0;
        }
        avg[0][paletteEntry] += pixelData[i].c.r;
        avg[1][paletteEntry] += pixelData[i].c.g;
        avg[2][paletteEntry] += pixelData[i].c.b;
        count[paletteEntry]++;
    }
    /* malloc check ok, using calloc */
    p = calloc(nPaletteEntries, sizeof(Pixel));
    if (!p) {
        for (i = 0; i < 3; i++) {
            free(avg[i]);
        }
        free(count);
        return 0;
    }
    for (i = 0; i < nPaletteEntries; i++) {
        p[i].c.r = (int)(.5 + (double)avg[0][i] / (double)count[i]);
        p[i].c.g = (int)(.5 + (double)avg[1][i] / (double)count[i]);
        p[i].c.b = (int)(.5 + (double)avg[2][i] / (double)count[i]);
    }
    *palette = p;
    for (i = 0; i < 3; i++) {
        free(avg[i]);
    }
    free(count);
    return 1;
}

/* (Ultimate No Mercy) The palette PIL's Image.quantize(colors, method=MEDIANCUT, kmeans=0) gives an RGB image of n pixels
 * (rgb: 3 bytes a pixel; alpha 255 as ImagingQuantize sets it): its colours into palette (3 bytes each), how many in *count.
 * 1 done, 0 failed. (quantize()'s steps up to its palette; the pixels' mapping it does after doesn't change the palette.) */
int firstrun_median_cut(const unsigned char *rgb, uint32_t n, uint32_t colors, unsigned char *palette, uint32_t *count) {
    Pixel *px = calloc(n ? n : 1, sizeof(Pixel));
    PixelList *hl[3];
    HashTable *h;
    BoxNode *root;
    Pixel *p = NULL;
    uint32_t i, nPaletteEntries = 0;
    if (!px) return 0;
    for (i = 0; i < n; i++) { px[i].c.r = rgb[3 * i]; px[i].c.g = rgb[3 * i + 1]; px[i].c.b = rgb[3 * i + 2]; px[i].c.a = 255; }
    h = create_pixel_hash(px, n);
    if (!h) { free(px); return 0; }
    hl[0] = hl[1] = hl[2] = NULL;
    hashtable_foreach(h, hash_to_list, hl);
    if (!hl[0]) { destroy_pixel_hash(h); free(px); return 0; }
    for (i = 0; i < 3; i++) hl[i] = mergesort_pixels(hl[i], i);
    root = median_cut(hl, n, colors);
    if (!root) { destroy_pixel_hash(h); free(px); return 0; }
    if (!annotate_hash_table(root, h, &nPaletteEntries) ||
        !compute_palette_from_median_cut(px, n, h, &p, nPaletteEntries, root)) {
        free_box_tree(root); destroy_pixel_hash(h); free(px); return 0;
    }
    free_box_tree(root);
    for (i = 0; i < nPaletteEntries && i < colors; i++) { palette[3 * i] = p[i].c.r; palette[3 * i + 1] = p[i].c.g; palette[3 * i + 2] = p[i].c.b; }
    *count = nPaletteEntries < colors ? nPaletteEntries : colors;
    free(p); destroy_pixel_hash(h); free(px);
    return 1;
}
