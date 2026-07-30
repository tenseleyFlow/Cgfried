// FLAGS: -fsyntax-only
// ERROR_EXPECTED: defined as wrong kind of tag
struct T { int a; };
enum T { X };
