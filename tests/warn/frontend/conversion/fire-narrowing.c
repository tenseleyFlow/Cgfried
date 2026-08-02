// FLAGS: -fsyntax-only -Wconversion
// WARN_COUNT: 2
struct ConversionInner {
    char c;
    long wide;
};
struct ConversionOuter {
    struct ConversionInner inner;
    int narrowed;
};

int conversion_narrow(long value)
{
    // WARN_CHECK: conversion conversion from 'long' to 'int' may change value
    int narrowed = value;
    // Brace elision must keep walking inside `inner`: the first `value`
    // initializes its long member, while the second reaches the outer int.
    // WARN_CHECK: conversion conversion from 'long' to 'int' may change value
    struct ConversionOuter aggregate = {1, value, value};
    return narrowed + aggregate.narrowed;
}
