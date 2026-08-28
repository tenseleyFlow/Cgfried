// FLAGS: -std=gnu17
// EXIT_CODE: 0

/* The directive keywords are raw and must not expand as macros. */
#define push_macro shadow_push
#define pop_macro shadow_pop

#define VALUE 11
#pragma push_macro("VALUE")
#undef VALUE
#define VALUE 22
#pragma push_macro("VALUE")
#undef VALUE
#define VALUE 33
enum { deepest = VALUE };
#pragma pop_macro("VALUE")
enum { middle = VALUE };
#pragma pop_macro("VALUE")
enum { original = VALUE };

/* Stacks are independent by name, and an undefined snapshot restores the
 * undefined state. */
#define OTHER 44
#pragma push_macro("OTHER")
#pragma push_macro("VALUE")
#undef OTHER
#undef VALUE
#define VALUE 55
#pragma pop_macro("OTHER")
enum { other = OTHER };
#pragma pop_macro("VALUE")
enum { value_again = VALUE };

#pragma push_macro("MISSING")
#define MISSING 66
#pragma pop_macro("MISSING")
#ifdef MISSING
#error pop_macro failed to restore an undefined name
#endif

/* _Pragma uses the same stack handler. */
#define VIA_PRAGMA 77
_Pragma("push_macro(\"VIA_PRAGMA\")")
#undef VIA_PRAGMA
#define VIA_PRAGMA 88
enum { changed = VIA_PRAGMA };
_Pragma("pop_macro(\"VIA_PRAGMA\")")
enum { restored = VIA_PRAGMA };

int main(void)
{
    return !(deepest == 33 && middle == 22 && original == 11 && other == 44 &&
             value_again == 11 && changed == 88 && restored == 77);
}
