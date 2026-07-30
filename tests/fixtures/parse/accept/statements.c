int g(int);
int f(int a, int b) {
    int i, s = 0;
    if (a) s = 1; else if (b) s = 2; else s = 3;
    if (a) if (b) s = 4; else s = 5;
    while (a) { a--; if (a == 1) break; else continue; }
    do { b--; } while (b > 0);
    for (i = 0; i < 10; i++) s += i;
    for (int j = 0; j < 3; j++) s += j;
    for (;;) break;
    switch (a) {
    case 0: s = 0; break;
    case 1:
    case 2: s = 1; break;
    default: s = 2; break;
    }
    goto done;
    ;
    {
        int shadow = s;
        s = shadow;
    }
done:
    return s;
}
