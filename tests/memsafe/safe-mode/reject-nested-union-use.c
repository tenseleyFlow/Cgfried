// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: use a tagged struct with explicit accessor functions
#include <signal.h>

struct Envelope {
    int kind;
    union sigval payload;
};

int inspect(struct Envelope envelope)
{
    return envelope.kind;
}
