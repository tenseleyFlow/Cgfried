// FLAGS: -fsyntax-only -Wswitch-enum
// WARN_COUNT: 0
enum result { FAILURE, SUCCESS };
int complete_enum(enum result result)
{
    switch (result) {
    case FAILURE: return 0;
    case SUCCESS: return 1;
    }
    return 0;
}
