// ERROR_EXPECTED: alignment of array elements is greater than element size

typedef int *__attribute__((aligned(16))) aligned_pointer;

aligned_pointer invalid_array[2];
