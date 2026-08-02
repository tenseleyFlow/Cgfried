// FLAGS: -fsyntax-only -Wshadow
// WARN_COUNT: 0
int member_name;
struct shadow_member { int member_name; };
