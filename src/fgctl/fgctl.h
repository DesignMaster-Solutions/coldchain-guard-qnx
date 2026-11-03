#ifndef CCG_FGCTL_H
#define CCG_FGCTL_H
#include <stdio.h>
/* CLI entry point with caller-owned output stream, also used by host tests. */
int fgctl_run(int argc, char **argv, FILE *out);
#endif
