#include <errno.h>
#include <math.h>

/**
 * Compute the smallest integral value not less than the argument.
 *
 * @param x A double value.
 * @return The smallest integral value not less than the argument.
 */
double
ceil(double x)
{
  // ----------------------------------------------------------
  // Code adapted from "The Standard C Library" by P.J. Plauger
  // ----------------------------------------------------------

  switch (__dtrunc(&x, 0)) {
  case FP_NAN:
    errno = EDOM;
    return x;
  case FP_NORMAL:
  case FP_SUBNORMAL:
    return (x > 0.0) ? x + 1.0 : x;
  }

  return x;
}
