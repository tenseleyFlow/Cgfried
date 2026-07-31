// The escape-dialect sidestep: initializer bytes 0, 10, 34, 92, 255
// emit NUMERICALLY (.byte 92, never "\\") — both assemblers agree on
// numbers where string dialects can differ.
// FLAGS: -S
// ASM_CHECK: .byte	0
// ASM_CHECK: .byte	10
// ASM_CHECK: .byte	34
// ASM_CHECK: .byte	92
// ASM_CHECK: .byte	255
char oddities[5] = {0, 10, 34, 92, '\377'};
