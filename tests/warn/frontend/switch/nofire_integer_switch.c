// FLAGS: -fsyntax-only -Wswitch
// WARN_COUNT: 0
int integer_switch(int value)
{
    switch (value) {
    case 0: return 1;
    }
    return 0;
}
