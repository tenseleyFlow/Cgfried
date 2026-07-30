#include "report.h"

#include <stdio.h>

int counts_total(const Counts *c)
{
    return c->pass + c->fail + c->xfail + c->xpass + c->skip + c->config;
}

int counts_exit_code(const Counts *c)
{
    return (c->fail || c->xpass || c->config) ? 1 : 0;
}

void report_summary(const Counts *c, const char *profile)
{
    printf("cgf-test: profile=%s total=%d pass=%d fail=%d xfail=%d xpass=%d "
           "skip=%d config=%d\n",
           profile, counts_total(c), c->pass, c->fail, c->xfail, c->xpass,
           c->skip, c->config);
}
