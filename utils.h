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

#ifndef __TVI_UTILS_H__
#define __TVI_UTILS_H__

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "tvi.h"

#define TVI_BUFMAX             256
#define TVI_MILLIS_PER_SECOND 1000

#define tvi_new(t)          ((t *) tvi_malloc (sizeof (t)))
#define tvi_newa(t, n)      ((t *) tvi_malloc ((n) * sizeof (t)))
#define tvi_renewa(t, p, n) ((t *) tvi_realloc (p, (n) * sizeof (t)))
#define tvi_free(p) \
  do \
  { \
    if (p) \
    { \
      free (p); \
      p = NULL; \
    } \
  } while (0)

#define tvi_get_millis(start_timeval, end_timeval) \
    (((end_timeval.tv_sec - start_timeval.tv_sec) * TVI_MILLIS_PER_SECOND) + \
     ((end_timeval.tv_usec - start_timeval.tv_usec) / TVI_MILLIS_PER_SECOND))

#ifdef TVI_DEBUG
# undef __TVI_FUNCTION__
# if defined (__GNUC__)
#  define __TVI_FUNCTION__ ((const char *) (__PRETTY_FUNCTION__))
# elif (defined (__STDC_VERSION__) && (__STDC_VERSION__ >= 19901L))
#  define __TVI_FUNCTION__ ((const char *) (__func__))
# elif (defined (_MSC_VER) && (_MSC_VER > 1300))
#  define __TVI_FUNCTION__ ((const char *) (__FUNCTION__))
# endif
# ifdef __TVI_FUNCTION__
static const bool has___TVI_FUNCTION__ = true;
# else
static const bool has___TVI_FUNCTION__ = false;
# endif
# define tvi_debug(...) \
  do \
  { \
    char __tag[TVI_BUFMAX]; \
    if (has___TVI_FUNCTION__) \
      snprintf (__tag, TVI_BUFMAX, "%s:DEBUG:%s():%i: ", \
                program_name, __TVI_FUNCTION__, __LINE__); \
    else \
      snprintf (__tag, TVI_BUFMAX, "%s:DEBUG:%i: ", \
                program_name, __LINE__); \
    __tvi_debug (__tag, __VA_ARGS__); \
  } while (0)
void __tvi_debug (const char *tag, const char *fmt, ...);
#else
# define tvi_debug(...)
#endif

void tvi_error (int errno_value, const char *fmt, ...);
void tvi_die (int exit_status, const char *fmt, ...);
int tvi_strncasecmp (const char *s1, const char *s2, size_t n);
char *tvi_strcasestr (const char *haystack, const char *needle);
void *tvi_malloc (size_t n);
void *tvi_realloc (void *o, size_t n);
char *tvi_strdup (const char *s, ssize_t n);
void tvi_replace_c (char *s, char c1, char c2);
void tvi_strip_trailing_space (char *s);
void tvi_gettimeofday (struct timeval *t);
int tvi_console_width (void);

#endif /* __TVI_UTILS_H__ */

