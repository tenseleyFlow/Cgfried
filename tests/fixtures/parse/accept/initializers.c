int a = 1;
int b[] = { 1, 2, 3 };
int c[3] = { [0] = 1, [2] = 3 };
struct P { int x, y; } p = { .x = 1, .y = 2 };
struct Q { struct P inner; } q = { .inner.x = 5 };
int d[2][2] = { { 1, 2 }, { 3, 4 } };
int e[] = { 1, 2, 3, };
char s[] = "hello";
struct P arr[2] = { [1].y = 7 };
