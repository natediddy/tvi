/*
 * tvi - TV series Information
 *
 * Copyright (C) 2014  Nathan Forbes
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "tvi.h"
#include "utils.h"

#define FALLBACK_CONSOLE_WIDTH 40

extern const char *program_name;

#ifdef TVI_DEBUG
void
__tvi_debug (const char *tag, const char *fmt, ...)
{
  va_list args;

  fputs (tag, stderr);
  va_start (args, fmt);
  vfprintf (stderr, fmt, args);
  va_end (args);
  fputc ('\n', stderr);
}
#endif

void
tvi_error (int errno_value, const char *fmt, ...)
{
  va_list args;

  fprintf (stderr, "%s: error: ", program_name);
  va_start (args, fmt);
  vfprintf (stderr, fmt, args);
  va_end (args);
  if (errno_value != 0)
    fprintf (stderr, ": %s (%i)", strerror (errno_value), errno_value);
  fputc ('\n', stderr);
}

void
tvi_die (int exit_status, const char *fmt, ...)
{
  va_list args;

  fprintf (stderr, "%s: error: ", program_name);
  va_start (args, fmt);
  vfprintf (stderr, fmt, args);
  va_end (args);
  fputc ('\n', stderr);
  exit (exit_status);
}

/* the following strncasecmp algorithm was borrowed from eglibc 2.15 {{{ */
int
tvi_strncasecmp (const char *s1, const char *s2, size_t n)
{
  const unsigned char *p1 = (const unsigned char *) s1;
  const unsigned char *p2 = (const unsigned char *) s2;
  int result;

  if (p1 == p2 || n == 0)
    return 0;

  while ((result = tolower (*p1) - tolower (*p2++)) == 0)
    if (*p1++ == '\0' || --n == 0)
      break;
  return result;
}
/* }}} strncasecmp algorithm */

/* the following strcasestr algorithm was borrowed from eglibc 2.15 {{{ */
#define __tolower(__c) (isupper (__c) ? tolower (__c) : (__c))

#define __available(__h, __nh, __j, __nn) \
  (!memchr ((__h) + (__nh), '\0', (__j) + (__nn) - (__nh)) && \
   ((__nh) = (__j) + (__nn)))

#define __max(__a, __b) ((__a < __b) ? (__b) : (__a))

#if CHAR_BIT < 10
# define __LONG_NEEDLE_THRESHOLD 32U
#else
# define __LONG_NEEDLE_THRESHOLD SIZE_MAX
#endif

static size_t
critical_factorization (const unsigned char *needle,
                        size_t n_needle,
                        size_t *period)
{
  unsigned char a;
  unsigned char b;
  size_t max_suffix;
  size_t max_suffix_rev;
  size_t j;
  size_t k;
  size_t p;

  max_suffix = SIZE_MAX;
  j = 0;
  k = 1;
  p = 1;

  while (j + k < n_needle)
  {
    a = __tolower (needle[j + k]);
    b = __tolower (needle[max_suffix + k]);
    if (a < b)
    {
      j += k;
      k = 1;
      p = j - max_suffix;
    }
    else if (a == b)
    {
      if (k != p)
        ++k;
      else
      {
        j += p;
        k = 1;
      }
    }
    else
    {
      max_suffix = j++;
      k = 1;
      p = 1;
    }
  }

  *period = p;
  max_suffix_rev = SIZE_MAX;
  j = 0;
  k = 1;
  p = 1;

  while (j + k < n_needle)
  {
    a = __tolower (needle[j + 1]);
    b = __tolower (needle[max_suffix_rev + k]);
    if (b < a)
    {
      j += k;
      k = 1;
      p = j - max_suffix_rev;
    }
    else if (a == b)
    {
      if (k != p)
        ++k;
      else
      {
        j += p;
        k = 1;
      }
    }
    else
    {
      max_suffix_rev = j++;
      k = 1;
      p = 1;
    }
  }

  if (max_suffix_rev + 1 < max_suffix + 1)
    return max_suffix + 1;
  *period = p;
  return max_suffix_rev + 1;
}

static char *
two_way_short_needle (const unsigned char *haystack,
                      size_t n_haystack,
                      const unsigned char *needle,
                      size_t n_needle)
{
  size_t i;
  size_t j;
  size_t period;
  size_t suffix;

  suffix = critical_factorization (needle, n_needle, &period);
  if (tvi_strncasecmp (needle, needle + period, suffix) == 0)
  {
    size_t memory = 0;
    j = 0;
    while (__available (haystack, n_haystack, j, n_needle))
    {
      i = __max (suffix, memory);
      while (i < n_needle &&
             (__tolower (needle[i]) == __tolower (haystack[i + j])))
        ++i;
      if (n_needle <= i)
      {
        i = suffix - 1;
        while (memory < i + 1 &&
               (__tolower (needle[i]) == __tolower (haystack[i + j])))
          --i;
        if (i + 1 < memory + 1)
          return (char *) (haystack + j);
        j += period;
        memory = n_needle - period;
      }
      else
      {
        j += i - suffix + 1;
        memory = 0;
      }
    }
  }
  else
  {
    period = __max (suffix, n_needle - suffix) + 1;
    j = 0;
    while (__available (haystack, n_haystack, j, n_needle))
    {
      i = suffix;
      while (i < n_needle &&
             (__tolower (needle[i]) == __tolower (haystack[i + j])))
        ++i;
      if (n_needle <= i)
      {
        i = suffix - 1;
        while (i != SIZE_MAX &&
               (__tolower (needle[i]) == __tolower (haystack[i + j])))
          --i;
        if (i == SIZE_MAX)
          return (char *) (haystack + j);
        j += period;
      }
      else
        j += i - suffix + 1;
    }
  }
  return NULL;
}

static char *
two_way_long_needle (const unsigned char *haystack,
                     size_t n_haystack,
                     const unsigned char *needle,
                     size_t n_needle)
{
  size_t i;
  size_t j;
  size_t period;
  size_t suffix;
  size_t shift_table[1U << CHAR_BIT];

  suffix = critical_factorization (needle, n_needle, &period);

  for (i = 0; i < 1U << CHAR_BIT; i++)
    shift_table[i] = n_needle;
  for (i = 0; i < n_needle; i++)
    shift_table[__tolower (needle[i])] = n_needle - i - 1;

  if (tvi_strncasecmp (needle, needle + period, suffix) == 0)
  {
    size_t memory = 0;
    size_t shift;
    j = 0;
    while (__available (haystack, n_haystack, j, n_needle))
    {
      shift = shift_table[__tolower (haystack[j + n_needle - 1])];
      if (0 < shift)
      {
        if (memory && shift < period)
          shift = n_needle - period;
        memory = 0;
        j += shift;
        continue;
      }
      i = __max (suffix, memory);
      while (i < n_needle - 1 &&
             (__tolower (needle[i]) == __tolower (haystack[i + j])))
        ++i;
      if (n_needle - 1 <= i)
      {
        i = suffix - 1;
        while (memory < i + 1 &&
               (__tolower (needle[i]) == __tolower (haystack[i + j])))
          --i;
        if (i + 1 < memory + 1)
          return (char *) haystack + j;
        j += period;
        memory = n_needle - period;
      }
      else
      {
        j += i - suffix + 1;
        memory = 0;
      }
    }
  }
  else
  {
    size_t shift;
    period = __max (suffix, n_needle - suffix) + 1;
    j = 0;
    while (__available (haystack, n_haystack, j, n_needle))
    {
      shift = shift_table[__tolower (haystack[j + n_needle - 1])];
      if (0 < shift)
      {
        j += shift;
        continue;
      }
      i = suffix;
      while (i < n_needle - 1 &&
             (__tolower (needle[i]) == __tolower (haystack[i + j])))
        ++i;
      if (n_needle - 1 <= i)
      {
        i = suffix - 1;
        while (i != SIZE_MAX &&
               (__tolower (needle[i]) == __tolower (haystack[i + j])))
          --i;
        if (i == SIZE_MAX)
          return (char *) (haystack + j);
        j += period;
      }
      else
        j += i - suffix + 1;
    }
  }
  return NULL;
}

char *
tvi_strcasestr (const char *haystack_start, const char *needle_start)
{
  const char *haystack = haystack_start;
  const char *needle = needle_start;
  size_t n_haystack;
  size_t n_needle;
  bool ok = true;

  while (*haystack && *needle)
  {
    ok &= (__tolower ((unsigned char) *haystack) ==
           __tolower ((unsigned char) *needle));
    haystack++;
    needle++;
  }

  if (*needle)
    return NULL;

  if (ok)
    return (char *) haystack_start;

  n_needle = needle - needle_start;
  haystack = haystack_start + 1;
  n_haystack = n_needle - 1;

  if (n_needle < __LONG_NEEDLE_THRESHOLD)
    return two_way_short_needle ((const unsigned char *) haystack,
                                 n_haystack,
                                 (const unsigned char *) needle_start,
                                 n_needle);
  return two_way_long_needle ((const unsigned char *) haystack,
                              n_haystack,
                              (const unsigned char *) needle_start,
                              n_needle);
}

#undef __tolower
#undef __available
#undef __max
#undef __LONG_NEEDLE_THRESHOLD
/* }}} strcasestr algorithm */

void *
tvi_malloc (size_t n)
{
  void *p;

  p = malloc (n);
  if (!p)
  {
    tvi_error (errno, "malloc failed");
    exit (E_SYSTEM);
  }
  return p;
}

void *
tvi_realloc (void *o, size_t n)
{
  void *p;

  p = realloc (o, n);
  if (!p)
  {
    tvi_error (errno, "realloc failed");
    exit (E_SYSTEM);
  }
  return p;
}

char *
tvi_strdup (const char *s, ssize_t n)
{
  size_t len;
  char *p;

  len = (n < 0) ? strlen (s) + 1 : strnlen (s, n) + 1;
  p = tvi_newa (char, len);
  p[len - 1] = '\0';
  return (char *) memcpy (p, s, len - 1);
}

void
tvi_replace_c (char *s, char c1, char c2)
{
  char *p;

  for (p = s; *p; ++p)
    if (*p == c1)
      *p = c2;
}

void
tvi_strip_trailing_space (char *s)
{
  size_t n;

  n = strlen (s) - 1;
  while (s[n] == ' ')
    s[n--] = '\0';
}

void
tvi_gettimeofday (struct timeval *t)
{
  memset (t, 0, sizeof (struct timeval));
  if (gettimeofday (t, NULL) == -1)
  {
    tvi_error (errno, "gettimeofday failed");
    exit (E_SYSTEM);
  }
}

int
tvi_console_width (void)
{
  struct winsize w;

  if (ioctl (STDOUT_FILENO, TIOCGWINSZ, &w) != -1)
    return w.ws_col;
  return FALLBACK_CONSOLE_WIDTH;
}

