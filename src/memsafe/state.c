#include "memsafe/memsafe.h"

MsState ms_state_join(MsState a, MsState b)
{
    return a == b ? a : MS_UNKNOWN;
}

const char *ms_state_name(MsState state)
{
    static const char *const names[MS_STATE_COUNT] = {
        "unallocated", "allocated", "freed", "escaped", "unknown",
    };

    if ((u32)state >= MS_STATE_COUNT)
        return "unknown";
    return names[state];
}

MsTransition ms_transition(MsState from, MsAction action, bool is_null)
{
    MsTransition result = {from, MS_OUTCOME_OK};

    if (action == MS_ACT_FREE && is_null) {
        result.outcome = MS_OUTCOME_NOOP;
        return result;
    }

    switch (action) {
    case MS_ACT_ALLOC:
        result.state = MS_ALLOCATED;
        break;
    case MS_ACT_FREE:
        if (from == MS_ALLOCATED)
            result.state = MS_FREED;
        else if (from == MS_FREED)
            result.outcome = MS_OUTCOME_DOUBLE_FREE;
        else if (from == MS_ESCAPED || from == MS_UNKNOWN)
            result.state = MS_UNKNOWN;
        break;
    case MS_ACT_ESCAPE:
        if (from == MS_ALLOCATED)
            result.state = MS_ESCAPED;
        else if (from == MS_FREED)
            result.outcome = MS_OUTCOME_UAF_ESCAPE;
        break;
    case MS_ACT_DEREF:
        if (from == MS_FREED)
            result.outcome = MS_OUTCOME_UAF;
        break;
    }
    return result;
}
