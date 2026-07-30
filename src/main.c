#include "driver/driver.h"

/* The single return-to-OS site (exit-code contract, driver.h). */
int main(int argc, char **argv)
{
    return driver_main(argc, argv);
}
