#include "memsafe/memsafe.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "diag.h"

struct MsTraceNode {
    const MsTraceNode *prev;
    MsEvent event;
};

void ms_trace_init(MsTrace *trace, Arena *arena)
{
    trace->arena = arena;
    trace->tail = NULL;
    trace->len = 0;
}

static const char *find_note(const MsTrace *trace, const char *note)
{
    const MsTraceNode *node;

    for (node = trace->tail; node; node = node->prev)
        if (strcmp(node->event.note, note) == 0)
            return node->event.note;
    return NULL;
}

void ms_trace_push(MsTrace *trace, Span loc, MsEventKind kind, const char *fmt,
                   ...)
{
    MsTraceNode *node;
    const char *interned;
    char *note;
    va_list ap, copy;
    int needed;

    if (!trace || !trace->arena)
        CGF_ICE("ms_trace_push: uninitialized trace");
    va_start(ap, fmt);
    va_copy(copy, ap);
    needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) {
        va_end(copy);
        CGF_ICE("ms_trace_push: vsnprintf failed");
    }
    note = arena_alloc(trace->arena, (size_t)needed + 1, 1);
    (void)vsnprintf(note, (size_t)needed + 1, fmt, copy);
    va_end(copy);
    interned = find_note(trace, note);
    if (!interned)
        interned = note;

    node = arena_alloc(trace->arena, sizeof(*node), _Alignof(MsTraceNode));
    node->prev = trace->tail;
    node->event.loc = loc;
    node->event.kind = kind;
    node->event.note = interned;
    trace->tail = node;
    trace->len++;
}

bool ms_trace_event(const MsTrace *trace, u32 ordinal, MsEvent *out)
{
    const MsTraceNode *node;
    u32 reverse;

    if (!trace || !out || ordinal >= trace->len)
        return false;
    reverse = trace->len - ordinal - 1;
    node = trace->tail;
    while (reverse--)
        node = node->prev;
    *out = node->event;
    return true;
}
