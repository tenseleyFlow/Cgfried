#define STR_(x) #x
#define STR(x)  STR_(x)
#define N 42
STR_(N)
STR(N)
#define B(x) #x x
B(N)
