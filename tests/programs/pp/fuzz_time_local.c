// FLAGS: -E
// ENV: SOURCE_DATE_EPOCH=1700000000
// CHECK: "Nov 14 2023"
// CHECK: "22:13:20"
// ppfuzz seeds 1786+: SOURCE_DATE_EPOCH is interpreted as UTC
// (reproducible-builds.org); WITHOUT it we use local time like gcc.
__DATE__
__TIME__
