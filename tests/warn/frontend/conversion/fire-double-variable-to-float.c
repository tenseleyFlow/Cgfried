// FLAGS: -fsyntax-only -Wconversion
// DIVERGES(gcc-8): GCC uses -Wfloat-conversion (Sprint 39).
// WARN_COUNT: 2

float conversion_double_variable_to_float(double value)
{
    // WARN_CHECK: conversion conversion from 'double' to 'float'
    float converted = value;
    // WARN_CHECK: conversion conversion from 'double' to 'float'
    float braced[1] = {value};
    return converted + braced[0];
}
