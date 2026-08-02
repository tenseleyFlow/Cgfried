// FLAGS: -fsyntax-only -Waddress
// WARN_COUNT: 0
int pointer_may_be_null(void (*callback)(void))
{
    return callback != 0;
}
