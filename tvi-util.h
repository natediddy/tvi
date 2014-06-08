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

#ifndef __TVI_UTIL_H__
#define __TVI_UTIL_H__

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "tvi.h"

#define XBUFMAX 256

#define MILLIS_PER_SECOND 1000

#define xnew(type)          ((type *) xmalloc (sizeof (type)))
#define xnewa(type, n)      ((type *) xmalloc ((n) * sizeof (type)))
#define xrenewa(type, p, n) ((type *) xrealloc (p, (n) * sizeof (type)))
#define xfree(p) \
  do \
  { \
    if (p) \
    { \
      free (p); \
      p = NULL; \
    } \
  } while (0)

#define get_millis(start_timeval, end_timeval) \
    (((end_timeval.tv_sec - start_timeval.tv_sec) * MILLIS_PER_SECOND) + \
     ((end_timeval.tv_usec - start_timeval.tv_usec) / MILLIS_PER_SECOND))

#ifdef DEBUG
# undef __XFUNCTION__
# if defined(__GNUC__)
#  define __XFUNCTION__ ((const char *) (__PRETTY_FUNCTION__))
# elif (defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 19901L))
#  define __XFUNCTION__ ((const char *) (__func__))
# elif (defined(_MSC_VER) && (_MSC_VER > 1300))
#  define __XFUNCTION__ ((const char *) (__FUNCTION__))
# endif
# ifdef __XFUNCTION__
static const bool has___XFUNCTION__ = true;
# else
static const bool has___XFUNCTION__ = false;
# endif
# define xdebug(...) \
  do \
  { \
    char __tag[XBUFMAX]; \
    if (has___XFUNCTION__) \
      snprintf (__tag, XBUFMAX, "%s:DEBUG:%s():%i: ", \
                program_name, __XFUNCTION__, __LINE__); \
    else \
      snprintf (__tag, XBUFMAX, "%s:DEBUG:%i: ", \
                program_name, __LINE__); \
    __xdebug (__tag, __VA_ARGS__); \
  } while (0)
void __xdebug (const char *, const char *, ...);
#else
# define xdebug(...)
#endif

void xerror (int, const char *, ...);
void die (int, const char *, ...);
size_t strlen_except (const char *, const char *);
char *strcpy_except (char *, const char *, const char *);
int xstrncasecmp (const char *, const char *, size_t);
char *xstrcasestr (const char *, const char *);
void *xmalloc (size_t);
void *xrealloc (void *, size_t);
char *xstrdup (const char *, ssize_t);
void replace_c (char *, char, char);
void strip_trailing_space (char *);
void xgettimeofday (struct timeval *);
int console_width (void);

#endif /* __TVI_UTIL_H__ */

