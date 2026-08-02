// FLAGS: -fsyntax-only -Wconversion
// WARN_COUNT: 1
unsigned char conversion_add(unsigned char a, unsigned char b)
{
    // WARN_CHECK: conversion conversion from 'int' to 'unsigned char' may change value
    return a + b;
}
