// FLAGS: -fsafe -fsyntax-only
// ERROR_EXPECTED: use a tagged struct with explicit accessor functions
struct Repeated0 {
    void *pointer;
    unsigned long bits;
};
struct Repeated1 {
    struct Repeated0 left, right;
};
struct Repeated2 {
    struct Repeated1 left, right;
};
struct Repeated3 {
    struct Repeated2 left, right;
};
struct Repeated4 {
    struct Repeated3 left, right;
};
struct Repeated5 {
    struct Repeated4 left, right;
};
struct Repeated6 {
    struct Repeated5 left, right;
};
struct Repeated7 {
    struct Repeated6 left, right;
};
struct Repeated8 {
    struct Repeated7 left, right;
};
struct Repeated9 {
    struct Repeated8 left, right;
};
struct Repeated10 {
    struct Repeated9 left, right;
};
struct Repeated11 {
    struct Repeated10 left, right;
};
struct Repeated12 {
    struct Repeated11 left, right;
};
struct Repeated13 {
    struct Repeated12 left, right;
};
struct Repeated14 {
    struct Repeated13 left, right;
};
struct Repeated15 {
    struct Repeated14 left, right;
};
struct Repeated16 {
    struct Repeated15 left, right;
};
struct Repeated17 {
    struct Repeated16 left, right;
};
struct Repeated18 {
    struct Repeated17 left, right;
};
struct Repeated19 {
    struct Repeated18 left, right;
};
struct Repeated20 {
    struct Repeated19 left, right;
};
struct Repeated21 {
    struct Repeated20 left, right;
};
struct Repeated22 {
    struct Repeated21 left, right;
};
struct Repeated23 {
    struct Repeated22 left, right;
};
struct Repeated24 {
    struct Repeated23 left, right;
};
struct Repeated25 {
    struct Repeated24 left, right;
};
struct Repeated26 {
    struct Repeated25 left, right;
};
struct Repeated27 {
    struct Repeated26 left, right;
};
struct Repeated28 {
    struct Repeated27 left, right;
};

union RepeatedTypeGraph {
    struct Repeated28 tree;
    unsigned long bits;
};
