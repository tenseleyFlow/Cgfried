// FLAGS: -fsyntax-only -Wunreachable-code
// WARN_COUNT: 0
void flow_unreachable_defensive_break(int selector)
{
    switch (selector) {
    case 0:
        return;
        break;
    default:
        return;
    }
}
