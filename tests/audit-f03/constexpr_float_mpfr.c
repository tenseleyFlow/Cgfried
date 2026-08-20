// Generated offline by tests/tools/f03_mpfr_constexpr_oracle.c
// MPFR_RNDN, binary24/binary53; MPFR is not a test dependency.
// FLAGS: -fdump-init
// CHECK: f_point1: size=4 bytes=CDCCCC3D
// CHECK: f_half_even_down: size=4 bytes=0000803F
// CHECK: f_half_even_up: size=4 bytes=0200803F
// CHECK: f_above_half: size=4 bytes=0100803F
// CHECK: f_below_half: size=4 bytes=0000803F
// CHECK: f_min_normal: size=4 bytes=00008000
// CHECK: f_max_subnormal: size=4 bytes=FFFF7F00
// CHECK: f_min_subnormal: size=4 bytes=01000000
// CHECK: f_half_min_subnormal: size=4 bytes=00000000
// CHECK: f_max_finite: size=4 bytes=FFFF7F7F
// CHECK: d_point1: size=8 bytes=9A9999999999B93F
// CHECK: d_two53_plus_one: size=8 bytes=0000000000004043
// CHECK: d_half_even_down: size=8 bytes=000000000000F03F
// CHECK: d_half_even_up: size=8 bytes=020000000000F03F
// CHECK: d_above_half: size=8 bytes=010000000000F03F
// CHECK: d_below_half: size=8 bytes=000000000000F03F
// CHECK: d_min_normal: size=8 bytes=0000000000001000
// CHECK: d_max_subnormal: size=8 bytes=FFFFFFFFFFFF0F00
// CHECK: d_min_subnormal: size=8 bytes=0100000000000000
// CHECK: d_half_min_subnormal: size=8 bytes=0000000000000000
// CHECK: d_max_finite: size=8 bytes=FFFFFFFFFFFFEF7F
// CHECK: f_add_point1_point2: size=4 bytes=9A99993E
// CHECK: f_add_half_to_odd: size=4 bytes=0200803F
// CHECK: f_sub_adjacent: size=4 bytes=00008033
// CHECK: f_mul_adjacent: size=4 bytes=0200803F
// CHECK: f_div_three: size=4 bytes=ABAAAA3E
// CHECK: f_div_max: size=4 bytes=FFFFFF7E
// CHECK: f_div_min_normal: size=4 bytes=00004000
// CHECK: f_add_min_subnormals: size=4 bytes=02000000
// CHECK: d_add_point1_point2: size=8 bytes=343333333333D33F
// CHECK: d_add_half_to_odd: size=8 bytes=020000000000F03F
// CHECK: d_sub_adjacent: size=8 bytes=000000000000A03C
// CHECK: d_mul_adjacent: size=8 bytes=020000000000F03F
// CHECK: d_div_three: size=8 bytes=555555555555D53F
// CHECK: d_div_max: size=8 bytes=FFFFFFFFFFFFDF7F
// CHECK: d_div_min_normal: size=8 bytes=0000000000000800
// CHECK: d_add_min_subnormals: size=8 bytes=0200000000000000
// CHECK: cast_double_half_down: size=4 bytes=0000803F
// CHECK: cast_double_half_up: size=4 bytes=0200803F
// CHECK: cast_double_min_normal_boundary: size=4 bytes=00008000
// CHECK: cast_double_half_min_subnormal: size=4 bytes=00000000

// clang-format off
float f_point1 = 0.1f;
float f_half_even_down = 1.000000059604644775390625f;
float f_half_even_up = 1.000000178813934326171875f;
float f_above_half = 1.0000000596046447753906250000000000001f;
float f_below_half = 1.0000000596046447753906249999999999999f;
float f_min_normal = 1.17549435082228750796873653722224567782e-38f;
float f_max_subnormal = 1.17549421069244107548702944484928734883e-38f;
float f_min_subnormal = 1.40129846432481707092372958328991613128e-45f;
float f_half_min_subnormal = 7.00649232162408535461864791644958065640e-46f;
float f_max_finite = 3.40282346638528859811704183484516925440e38f;
double d_point1 = 0.1;
double d_two53_plus_one = 9007199254740993.0;
double d_half_even_down = 1.00000000000000011102230246251565404236316680908203125;
double d_half_even_up = 1.00000000000000033306690738754696212708950042724609375;
double d_above_half = 1.0000000000000001110223024625156540423631668090820312500000001;
double d_below_half = 1.0000000000000001110223024625156540423631668090820312499999999;
double d_min_normal = 2.225073858507201383090232717332404064219215980462331830553327417e-308;
double d_max_subnormal = 2.225073858507200889024586876085859887650423112240959465493524802e-308;
double d_min_subnormal = 4.940656458412465441765687928682213723650598026143247644255856825e-324;
double d_half_min_subnormal = 2.470328229206232720882843964341106861825299013071623822127928412e-324;
double d_max_finite = 1.797693134862315708145274237317043567981e308;
float f_add_point1_point2 = (0.1f + 0.2f);
float f_add_half_to_odd = (1.00000011920928955078125f + 5.9604644775390625e-8f);
float f_sub_adjacent = (1.0f - 0.999999940395355224609375f);
float f_mul_adjacent = (1.00000011920928955078125f * 1.00000011920928955078125f);
float f_div_three = (1.0f / 3.0f);
float f_div_max = (3.40282346638528859811704183484516925440e38f / 2.0f);
float f_div_min_normal = (1.17549435082228750796873653722224567782e-38f / 2.0f);
float f_add_min_subnormals = (1.40129846432481707092372958328991613128e-45f + 1.40129846432481707092372958328991613128e-45f);
double d_add_point1_point2 = (0.1 + 0.2);
double d_add_half_to_odd = (1.0000000000000002220446049250313080847263336181640625 + 1.1102230246251565404236316680908203125e-16);
double d_sub_adjacent = (1.0 - 0.99999999999999988897769753748434595763683319091796875);
double d_mul_adjacent = (1.0000000000000002220446049250313080847263336181640625 * 1.0000000000000002220446049250313080847263336181640625);
double d_div_three = (1.0 / 3.0);
double d_div_max = (1.797693134862315708145274237317043567981e308 / 2.0);
double d_div_min_normal = (2.225073858507201383090232717332404064219215980462331830553327417e-308 / 2.0);
double d_add_min_subnormals = (4.940656458412465441765687928682213723650598026143247644255856825e-324 + 4.940656458412465441765687928682213723650598026143247644255856825e-324);
float cast_double_half_down = (float)(1.0000000596046447753906250000000000000000000000000000000000000001);
float cast_double_half_up = (float)(1.0000001788139343261718749999999999999999999999999999999999999999);
float cast_double_min_normal_boundary = (float)(1.17549428075736429172788299103576651332e-38);
float cast_double_half_min_subnormal = (float)(7.00649232162408535461864791644958065640e-46);
// clang-format on
