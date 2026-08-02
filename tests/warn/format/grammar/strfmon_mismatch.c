// FLAGS: -fsyntax-only -Wformat
// WARN_COUNT: 9
long strfmon(char *, unsigned long, const char *, ...);
void test(char *out)
{
    // WARN_CHECK: format expects argument of type 'double'
    strfmon(out, 32, "%n", "wrong");
    // WARN_CHECK: format conversion lacks type at end of format
    strfmon(out, 32, "%!%");
    // WARN_CHECK: format use of '+' flag and '(' flag together
    strfmon(out, 32, "%+(n", 1.0);
    // WARN_CHECK: format repeated fill character in format
    strfmon(out, 32, "%=x=y10n", 1.0);
    // WARN_CHECK: format empty left precision in strfmon format
    strfmon(out, 32, "%#n", 1.0);
    // WARN_CHECK: format empty precision in strfmon format
    strfmon(out, 32, "%.n", 1.0);
    // WARN_CHECK: format repeated '+' flag in format
    strfmon(out, 32, "%++n", 1.0);
    // WARN_CHECK: format missing fill character at end of strfmon format
    strfmon(out, 32, "%=");
}
