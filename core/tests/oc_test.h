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

/* Each suite uses some of these helpers and not others -- test_morse.c never
 * needs check_near(), test_gaze.c never needs check_str(). Saying so is what
 * keeps the warning flags worth having: without it every suite compiles with a
 * handful of "defined but not used" warnings it can do nothing about, and a
 * real warning in a test becomes one line among six that are noise.
 *
 * C11 has no portable spelling for it, so this is per compiler. GCC and Clang
 * have the attribute -- and clang-cl is caught by the first branch, because it
 * defines __clang__ as well as _MSC_VER and accepts the attribute. MSVC has no
 * equivalent that can sit on a declaration; the nearest thing is turning off
 * C4505, which is the same warning under another number.
 *
 * Getting this wrong does not produce a warning on MSVC, it produces a parse
 * error: __attribute__ is not a keyword there, so the compiler reads it as a
 * malformed declaration and every function below it fails in cascade. */
#if defined(__GNUC__) || defined(__clang__)
#  define OC_TEST_UNUSED __attribute__((unused))
#else
#  define OC_TEST_UNUSED
#  ifdef _MSC_VER
#    pragma warning(disable : 4505) /* unreferenced local function removed */
#  endif
#endif

static int oc_pass = 0, oc_fail = 0;
static const char *oc_group = "";

OC_TEST_UNUSED static void group(const char *name) { oc_group = name; printf("\n  %s\n", name); }

OC_TEST_UNUSED static void check(int cond, const char *label)
{
    if (cond) { oc_pass++; printf("    ok   %s\n", label); }
    else      { oc_fail++; printf("    FAIL %s\n", label); }
}

OC_TEST_UNUSED static void check_int(long long got, long long want, const char *label)
{
    if (got == want) { oc_pass++; printf("    ok   %s\n", label); }
    else { oc_fail++; printf("    FAIL %s (got %lld, want %lld)\n", label, got, want); }
}

OC_TEST_UNUSED static void check_str(const char *got, const char *want, const char *label)
{
    if (got && want && strcmp(got, want) == 0) { oc_pass++; printf("    ok   %s\n", label); }
    else { oc_fail++; printf("    FAIL %s (got \"%s\", want \"%s\")\n", label,
                            got ? got : "(null)", want ? want : "(null)"); }
}

OC_TEST_UNUSED static void check_near(double got, double want, double eps, const char *label)
{
    if (fabs(got - want) <= eps) { oc_pass++; printf("    ok   %s\n", label); }
    else { oc_fail++; printf("    FAIL %s (got %f, want %f)\n", label, got, want); }
}

OC_TEST_UNUSED static int oc_report(const char *suite)
{
    printf("\n%s: %d passed, %d failed\n", suite, oc_pass, oc_fail);
    return oc_fail == 0 ? 0 : 1;
}

#endif
