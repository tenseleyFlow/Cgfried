// RESOLVED(audit): X64-M-03 multi-digit inline-assembly operand references consume one digit
int tenth_operand(int a0, int a1, int a2, int a3, int a4,
                  int a5, int a6, int a7, int a8, int a9) {
    int output;
    __asm__("movl %10, %0"
            : "=r"(output)
            : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4),
              "r"(a5), "r"(a6), "r"(a7), "r"(a8), "r"(a9));
    return output;
}
