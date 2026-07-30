#include "ir/ir.h"

#include <string.h>

/* Cooper-Harvey-Kennedy "A Simple, Fast Dominance Algorithm" over a
 * reverse-postorder numbering. Chosen deliberately over Lengauer-Tarjan:
 * at our function sizes the simple algorithm's constant factor wins, and
 * its 40 lines are auditable against the paper by eye.
 *
 * Unreachable blocks get NO entry (idom stays invalid): the verifier
 * rejects them anyway (check 6), so spending code making dominance
 * meaningful for them would just hide bugs. */

struct IrDomTree {
    u32 nblocks;
    u32 entry; /* block index (id-1) of the entry, always 0 */
    u32 *idom; /* by block index; IDOM_NONE = unreachable / entry */
    u32 *rpo;  /* by block index; RPO_NONE = unreachable */
};

#define IDOM_NONE 0xFFFFFFFFu
#define RPO_NONE 0xFFFFFFFFu

/* Successor iteration: walk every edge of every instruction. On verified
 * IR only the terminator has edges; on unverified IR this over-approximates
 * harmlessly (dominance is only trusted after the verifier passes). */
#define FOR_EACH_SUCC(blk, e)                                                  \
    for (const IrInst *in_ = (blk)->first; in_; in_ = in_->next)               \
        for (const IrEdge *e = in_->edges; e && e < in_->edges + in_->nedges;  \
             e++)

IrDomTree *ir_domtree_build(Arena *arena, const IrFunc *f)
{
    IrDomTree *t = arena_alloc(arena, sizeof(IrDomTree), _Alignof(IrDomTree));
    u32 n = f->nblocks;
    u32 *post; /* postorder listing of block indices */
    u32 npost = 0;
    u32 *stack;    /* DFS: block index */
    u32 *edge_pos; /* DFS: how many successors already visited */
    bool *seen;
    u32 sp = 0;
    u32 i;
    bool changed;

    t->nblocks = n;
    t->entry = 0;
    t->idom = arena_alloc(arena, n * sizeof(u32), _Alignof(u32));
    t->rpo = arena_alloc(arena, n * sizeof(u32), _Alignof(u32));
    memset(t->idom, 0xFF, n * sizeof(u32));
    memset(t->rpo, 0xFF, n * sizeof(u32));
    if (n == 0)
        return t;
    post = arena_alloc(arena, n * sizeof(u32), _Alignof(u32));
    stack = arena_alloc(arena, n * sizeof(u32), _Alignof(u32));
    edge_pos = arena_alloc(arena, n * sizeof(u32), _Alignof(u32));
    seen = arena_alloc(arena, n * sizeof(bool), _Alignof(bool));
    memset(seen, 0, n * sizeof(bool));

    /* Iterative DFS from the entry, emitting postorder. edge_pos[sp] counts
     * successors already expanded for the block at stack[sp], so each stack
     * frame resumes where it left off — no recursion, no successor lists
     * materialized. */
    stack[0] = 0;
    edge_pos[0] = 0;
    seen[0] = true;
    sp = 1;
    while (sp) {
        u32 b = stack[sp - 1];
        u32 want = edge_pos[sp - 1];
        u32 k = 0;
        u32 found = IDOM_NONE;

        FOR_EACH_SUCC(&f->blocks[b], e)
        {
            if (e->target.v == 0 || e->target.v > n)
                continue; /* malformed edge: verifier's problem */
            if (k++ == want) {
                found = e->target.v - 1;
                break;
            }
        }
        if (found == IDOM_NONE) {
            post[npost++] = b;
            sp--;
            continue;
        }
        edge_pos[sp - 1]++;
        if (!seen[found]) {
            seen[found] = true;
            stack[sp] = found;
            edge_pos[sp] = 0;
            sp++;
        }
    }
    for (i = 0; i < npost; i++)
        t->rpo[post[i]] = npost - 1 - i;

    /* CHK: iterate to fixpoint in RPO. intersect() climbs the two chains
     * to their common ancestor by comparing RPO numbers. */
    t->idom[0] = 0; /* entry's idom is itself during iteration */
    do {
        changed = false;
        for (i = npost; i-- > 0;) { /* post[] reversed = RPO */
            u32 b = post[i];
            u32 new_idom = IDOM_NONE;
            u32 j;

            if (b == 0)
                continue;
            /* preds of b = blocks with an edge to b; scan reachable blocks
             * (an unreachable pred must not pollute the answer). */
            for (j = 0; j < n; j++) {
                bool is_pred = false;

                if (t->rpo[j] == RPO_NONE || t->idom[j] == IDOM_NONE)
                    continue; /* unreachable or not yet processed */
                FOR_EACH_SUCC(&f->blocks[j], e)
                {
                    if (e->target.v == b + 1) {
                        is_pred = true;
                        break;
                    }
                }
                if (!is_pred)
                    continue;
                if (new_idom == IDOM_NONE) {
                    new_idom = j;
                } else {
                    /* intersect(new_idom, j) */
                    u32 f1 = new_idom;
                    u32 f2 = j;

                    while (f1 != f2) {
                        while (t->rpo[f1] > t->rpo[f2])
                            f1 = t->idom[f1];
                        while (t->rpo[f2] > t->rpo[f1])
                            f2 = t->idom[f2];
                    }
                    new_idom = f1;
                }
            }
            if (new_idom != IDOM_NONE && t->idom[b] != new_idom) {
                t->idom[b] = new_idom;
                changed = true;
            }
        }
    } while (changed);
    t->idom[0] = IDOM_NONE; /* entry has no idom in the public answer */
    return t;
}

BlockId ir_idom(const IrDomTree *t, BlockId b)
{
    BlockId r = BLOCK_INVALID;

    if (b.v == 0 || b.v > t->nblocks)
        return r;
    if (t->idom[b.v - 1] == IDOM_NONE)
        return r; /* entry, or unreachable */
    r.v = t->idom[b.v - 1] + 1;
    return r;
}

bool ir_dominates(const IrDomTree *t, BlockId a, BlockId b)
{
    u32 cur;

    if (a.v == 0 || b.v == 0 || a.v > t->nblocks || b.v > t->nblocks)
        return false;
    if (a.v == b.v)
        return true; /* dominance is reflexive */
    if (t->rpo[b.v - 1] == RPO_NONE)
        return false; /* unreachable: only itself */
    cur = b.v - 1;
    while (t->idom[cur] != IDOM_NONE) {
        cur = t->idom[cur];
        if (cur == a.v - 1)
            return true;
        if (cur == t->entry)
            break;
    }
    return false;
}
