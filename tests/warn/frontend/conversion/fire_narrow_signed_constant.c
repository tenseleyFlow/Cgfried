// FLAGS: -fsyntax-only -Wconversion
// WARN_COUNT: 1

// WARN_CHECK: conversion conversion from 'long' to 'int' changes value
int conversion_narrow_signed_constant = 2147483648L;
