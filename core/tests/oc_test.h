/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
/* A test harness small enough to read in one sitting.
 *
 * Every check() label names the rule it guards, so a failure reads as the rule
 * that broke rather than as a line number. */
#ifndef OC_TEST_H
#define OC_TEST_H

#include <math.h>
#include <stdio.h>
#include <string.h>

static int oc_pass = 0, oc_fail = 0;
static const char *oc_group = "";

__attribute__((unused)) static void group(const char *name) { oc_group = name; printf("\n  %s\n", name); }

__attribute__((unused)) static void check(int cond, const char *label)
{
    if (cond) { oc_pass++; printf("    ok   %s\n", label); }
    else      { oc_fail++; printf("    FAIL %s\n", label); }
}

__attribute__((unused)) static void check_int(long long got, long long want, const char *label)
{
    if (got == want) { oc_pass++; printf("    ok   %s\n", label); }
    else { oc_fail++; printf("    FAIL %s (got %lld, want %lld)\n", label, got, want); }
}

__attribute__((unused)) static void check_str(const char *got, const char *want, const char *label)
{
    if (got && want && strcmp(got, want) == 0) { oc_pass++; printf("    ok   %s\n", label); }
    else { oc_fail++; printf("    FAIL %s (got \"%s\", want \"%s\")\n", label,
                            got ? got : "(null)", want ? want : "(null)"); }
}

__attribute__((unused)) static void check_near(double got, double want, double eps, const char *label)
{
    if (fabs(got - want) <= eps) { oc_pass++; printf("    ok   %s\n", label); }
    else { oc_fail++; printf("    FAIL %s (got %f, want %f)\n", label, got, want); }
}

__attribute__((unused)) static int oc_report(const char *suite)
{
    printf("\n%s: %d passed, %d failed\n", suite, oc_pass, oc_fail);
    return oc_fail == 0 ? 0 : 1;
}

#endif
