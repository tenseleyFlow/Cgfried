/* A prefixed character constant in #if keeps its code-unit value. The
 * ordinary lexer already evaluates each spelling below as 256; the
 * preprocessor must make the same choice before selecting a source arm. */
#if L'\400' != 256
#error "wide octal character constant narrowed during preprocessing"
#endif

#if L'\x100' != 256
#error "wide hexadecimal character constant narrowed during preprocessing"
#endif

#if u'\400' != 256
#error "UTF-16 character constant narrowed during preprocessing"
#endif

#if U'\400' != 256
#error "UTF-32 character constant narrowed during preprocessing"
#endif

#define C L'\400'
#if C
#define ZERO (!C)
#else
#define ZERO C
#endif

_Static_assert(ZERO == 0, "#if and ordinary character semantics diverged");

int main(void)
{
    return ZERO;
}
