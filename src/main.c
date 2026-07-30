#include "driver/driver.h"
#include "driver/toolchain.h"

/* The single return-to-OS site (exit-code contract, driver.h). */
int main(int argc, char **argv)
{
    cgf_toolchain_set_argv0(argv[0]);
    return driver_main(argc, argv);
}
