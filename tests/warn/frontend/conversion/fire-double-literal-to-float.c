// FLAGS: -fsyntax-only -Wconversion
// DIVERGES(gcc-8): GCC spells this -Wfloat-conversion; that flag lands in Sprint 39.
// WARN_COUNT: 1

float conversion_double_literal_to_float(void)
{
    // WARN_CHECK: conversion conversion from 'double' to 'float' may change value
    return 0.1;
}
