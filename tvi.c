/*
 * tvi - TV series Information
 *
 * Copyright (C) 2013  Nathan Forbes
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <getopt.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <curl/curl.h>

#ifdef PACKAGE_NAME
# define PROGRAM_NAME PACKAGE_NAME
#else
# define PROGRAM_NAME "tvi"
#endif

#ifdef PACKAGE_VERSION
# define PROGRAM_VERSION PACKAGE_VERSION
#else
# define PROGRAM_VERSION "3.1.1"
#endif

#define TVDOTCOM "http://www.tv.com"

#define HELP_TEXT \
  "Options:\n" \
  "\n" \
  "  Mandatory arguments to long options are mandatory for short options " \
    "too.\n" \
  "\n" \
  "  -e, --episode=N[,N,...]   specify episode(s) N...\n" \
  "                            For more than one episode, use a\n" \
  "                            comma-separated list (e.g. \"1,2,3\").\n" \
  "  -s, --season=N[,N,...]    specify season(s) N...\n" \
  "                            For more than one season, use a\n" \
  "                            comma-separated list (e.g. \"1,2,3\").\n" \
  "  -a, --air                 print the air date for each episode\n" \
  "  -d, --description         print description for each episode\n" \
  "  -H, --highest-rated       print highest rated episode of series\n" \
  "  -l, --last                print the most recently aired episode\n" \
  "  -L, --lowest-rated        print lowest rated episode of series\n" \
  "  -n, --next                print the next upcoming episode scheduled " \
    "to air\n" \
  "  -r, --rating              print rating for each episode\n" \
  "  -h, --help                print this text and exit\n" \
  "  -v, --version             print version information and exit\n" \
  "\n" \
  "Only 1 TITLE can be provided at a time.\n" \
  "\n" \
  "All TV series data is obtained from <" TVDOTCOM "/>.\n"

#define VERSION_TEXT \
  PROGRAM_NAME " " PROGRAM_VERSION "\n" \
  "Written by Nathan Forbes (2014)\n"

#define SEARCH_URL   TVDOTCOM "/search?q=%s/"
#define EPISODES_URL TVDOTCOM "/shows/%s/episodes/"
#define SEASON_URL   TVDOTCOM "/shows/%s/season-%u/"

#define USERAGENT PROGRAM_NAME "(TV series Info)/" PROGRAM_VERSION

#define PROPELLER_SIZE          4
#define FALLBACK_CONSOLE_WIDTH 40
#define MILLIS_PER_SECOND    1000

#define SERIES_TITLE_PATTERN        "<title>"
#define SERIES_DESCRIPTION_PATTERN  "\"og:description\" content=\""
#define SERIES_AIRS_ON_PATTERN      "class=\"tagline\">"
#define SEASON_PATTERN              "<strong>Season %u"
#define EPISODE_PATTERN             "Episode %u\r\n"
#define EPISODE_AIR_PATTERN         "class=\"date\">"
#define EPISODE_DESCRIPTION_PATTERN "class=\"description\">"
#define EPISODE_RATING_PATTERN      "_rating"
#define SEARCH_SHOW_PATTERN         "class=\"result show\">"
#define SEARCH_HREF_PATTERN         " href=\"/shows/"

#define XBUFMAX 1024

#define EMPTY_SERIES_DESCRIPTION  "N/A"
#define EMPTY_EPISODE_DESCRIPTION "N/A"

#define SPEC_DELIM_C ','
#define SPEC_DELIM_S ","
#define SPEC_ERROR_MESSAGE \
  "must be of the form \"N" SPEC_DELIM_S "N" SPEC_DELIM_S "N" SPEC_DELIM_S \
  "...\" (e.g. \"1" SPEC_DELIM_S "23\", \"4\", \"5" SPEC_DELIM_S "6" \
  SPEC_DELIM_S "7\", etc.)"

#define PROGRESS_LOADING_MESSAGE "Loading... "

#define ENCODE_CHARS "!@#$%^&*()=+{}[]|\\;':\",<>/? "

#define TITLE series->title.proper

#define SEASON(n)          series->season[(n)]
#define LAST_SEASON        SEASON (series->total_seasons - 1)
#define EPISODE(s, n)      s.episode[(n)]
#define LAST_EPISODE_OF(s) s.episode[s.total_episodes - 1]

#define PROPELLER_ROTATE_INTERVAL 0.25f
#define propeller_rotate_interval_passed(m) \
  ((m) >= MILLIS_PER_SECOND * PROPELLER_ROTATE_INTERVAL)

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

typedef unsigned char bool;
#define false ((bool) 0)
#define true  ((bool) 1)

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
#else
# define xdebug(...)
#endif

/* these are hacky macros that facilitate creating and formatting static
   buffers for URLs and HTML patterns in a way that looks a lot cleaner {{{ */
#define __url_var(name) url_ ## name
#define url_var__(name) __url_var (name)

#define search_url(name) \
  char url_var__ (name)[XBUFMAX]; \
  char *__e = encode_series_given_title (); \
  snprintf (url_var__ (name), XBUFMAX, SEARCH_URL, __e); \
  xfree (__e)

#define episodes_url(name) \
  char url_var__ (name)[XBUFMAX]; \
  snprintf (url_var__ (name), XBUFMAX, EPISODES_URL, series->title.url)

#define season_url(name, n) \
  char url_var__ (name)[XBUFMAX]; \
  snprintf (url_var__ (name), XBUFMAX, SEASON_URL, series->title.url, (n))

#define __html_pat_var(name) html_pattern_ ## name
#define html_pat_var__(name) __html_pat_var (name)

#define season_pattern(name, n) \
  char html_pat_var__ (name)[XBUFMAX]; \
  snprintf (html_pat_var__ (name), XBUFMAX, SEASON_PATTERN, (n))

#define episode_pattern(name, n) \
  char html_pat_var__ (name)[XBUFMAX]; \
  snprintf (html_pat_var__ (name), XBUFMAX, EPISODE_PATTERN, (n))
/* }}} */

/* exit status codes */
enum
{
  E_OKAY,     /* everything went fine */
  E_OPTION,   /* there was an error with a command line option */
  E_INTERNET, /* there was an error with the internet (libcurl) */
  E_SYSTEM    /* there was a serious system error */
};

struct episode
{
  bool has_aired;
  double rating;
  char air[XBUFMAX];
  char title[XBUFMAX];
  char *description;
};

struct season
{
  int total_episodes;
  double rating;
  struct episode episode[XBUFMAX];
};

struct series_title
{
  char proper[XBUFMAX]; /* proper (e.g. "The Wire") */
  char url[XBUFMAX];    /* for URL (e.g. "the-wire") */
  char *given;          /* from command line (e.g. "the wire") */
};

struct series
{
  int total_episodes;
  int total_seasons;
  double rating;
  char air_start[XBUFMAX];
  char air_end[XBUFMAX];
  char airs_on[XBUFMAX];
  struct season season[XBUFMAX];
  struct series_title title;
  char *description;
};

struct page_content
{
  size_t n;
  char *buffer;
};

struct spec
{
  int n;
  int v[XBUFMAX];
};

#define E_ATTR_0           0
#define E_ATTR_AIR         0x01
#define E_ATTR_DESCRIPTION 0x02
#define E_ATTR_RATING      0x04

struct tvi_options
{
  bool highest_rated;
  bool info;
  bool last;
  bool lowest_rated;
  bool next;
  char episode_attrs;
  struct spec e;
  struct spec s;
};

static const char *        program_name;
static struct page_content page     = {0, NULL};
static struct series *     series   = NULL;

static struct option const options[] =
{
  {"air", no_argument, NULL, 'a'},
  {"desc", no_argument, NULL, 'd'},
  {"episode", required_argument, NULL, 'e'},
  {"help", no_argument, NULL, 'h'},
  {"highest-rated", no_argument, NULL, 'H'},
  {"info", no_argument, NULL, 'i'},
  {"last", no_argument, NULL, 'l'},
  {"lowest-rated", no_argument, NULL, 'L'},
  {"next", no_argument, NULL, 'n'},
  {"rating", no_argument, NULL, 'r'},
  {"season", required_argument, NULL, 's'},
  {"version", no_argument, NULL, 'v'},
  {NULL, 0, NULL, 0}
};

static void
set_program_name (const char *argv0)
{
  const char *p;

  if (argv0 && *argv0)
  {
    p = strrchr (argv0, '/');
    if (p && *p && *(p + 1))
      program_name = p + 1;
    else
      program_name = argv0;
    return;
  }
  program_name = PROGRAM_NAME;
}

#ifdef DEBUG
static void
__xdebug (const char *tag, const char *fmt, ...)
{
  va_list args;

  fputs (tag, stderr);
  va_start (args, fmt);
  vfprintf (stderr, fmt, args);
  va_end (args);
  fputc ('\n', stderr);
}
#endif

static void
xerror (int errnum, const char *fmt, ...)
{
  va_list args;

  fprintf (stderr, "%s: error: ", program_name);
  va_start (args, fmt);
  vfprintf (stderr, fmt, args);
  va_end (args);
  if (errnum != 0)
    fprintf (stderr, ": %s (errno=%i)", strerror (errnum), errnum);
  fputc ('\n', stderr);
}

static void
die (int exit_status, const char *fmt, ...)
{
  va_list args;

  fprintf (stderr, "%s: error: ", program_name);
  va_start (args, fmt);
  vfprintf (stderr, fmt, args);
  va_end (args);
  fputc ('\n', stderr);
  exit (exit_status);
}

static size_t
xstrlen (const char *s, const char *x)
{
  bool c;
  size_t n;
  const char *e;
  const char *p;

  n = 0;
  for (p = s; *p; ++p)
  {
    c = false;
    for (e = x; *e; ++e)
    {
      if (*e == *p)
      {
        c = true;
        break;
      }
    }
    if (!c)
      n++;
  }
  return n;
}

static char *
xstrcpy (char *dst, const char *src, const char *x)
{
  bool c;
  size_t n;
  char *p;
  const char *e;
  const char *s;

  n = xstrlen (src, x);
  char buf[n + 1];

  for (p = buf, s = src; *s; ++s)
  {
    c = false;
    for (e = x; *e; ++e)
    {
      if (*e == *s)
      {
        c = true;
        break;
      }
    }
    if (!c)
      *p++ = *s;
  }

  *p = '\0';
  return (char *) memcpy (dst, buf, n + 1);
}

static int
xstrncasecmp (const char *s1, const char *s2, size_t n)
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

static void *
xmalloc (size_t n)
{
  void *p;

  p = malloc (n);
  if (!p)
  {
    xerror (errno, "malloc failed");
    exit (E_SYSTEM);
  }
  return p;
}

static void *
xrealloc (void *o, size_t n)
{
  void *p;

  p = realloc (o, n);
  if (!p)
  {
    xerror (errno, "realloc failed");
    exit (E_SYSTEM);
  }
  return p;
}

static char *
xstrdup (const char *s, ssize_t n)
{
  size_t len;
  char *p;

  len = (n < 0) ? strlen (s) + 1 : strnlen (s, n) + 1;
  p = xnewa (char, len);
  p[len - 1] = '\0';
  return (char *) memcpy (p, s, len - 1);
}

static void
usage (bool had_error)
{
  fprintf ((!had_error) ? stdout : stderr,
           "Usage: %s [-adHilLnr] [-sN[,N,...]] [-eN[,N,...]] TITLE\n",
           program_name);

  if (!had_error)
    fputs (HELP_TEXT, stdout);

  exit ((!had_error) ? E_OKAY : E_OPTION);
}

static void
version (void)
{
  fputs (VERSION_TEXT, stdout);
  exit (E_OKAY);
}

static void
replace_c (char *s, char c1, char c2)
{
  char *p;

  for (p = s; *p; ++p)
    if (*p == c1)
      *p = c2;
}

static void
strip_trailing_space (char *s)
{
  size_t n;

  n = strlen (s) - 1;
  while (s[n] == ' ')
    s[n--] = '\0';
}

static void
set_series_given_title (char **item)
{
  size_t n;
  size_t p;
  size_t pos;
  char *t;

  n = 0;
  for (p = 0; item[p]; ++p)
  {
    n += strlen (item[p]);
    if (item[p + 1])
      n++;
  }

  pos = 0;
  series->title.given = xnewa (char, n + 1);

  for (p = 0, t = series->title.given; item[p]; ++p)
  {
    n = strlen (item[p]);
    memcpy (t, item[p], n);
    t += n;
    if (item[p + 1])
      *t++ = ' ';
  }
  *t = '\0';
}

static char *
encode_series_given_title (void)
{
  size_t n;
  size_t ng;
  const char *c;
  const char *s;

  ng = strlen (series->title.given);
  n = ng;

  for (s = series->title.given; *s; ++s)
  {
    for (c = ENCODE_CHARS; *c; ++c)
    {
      if (*s == *c)
      {
        n += 2;
        break;
      }
    }
  }

  if (n == ng)
    return xstrdup (series->title.given, n);

  bool encoded_char;
  char *encoded = xnewa (char, n + 1);
  char *e = encoded;

  for (s = series->title.given; *s; ++s, ++e)
  {
    encoded_char = false;
    for (c = ENCODE_CHARS; *c; ++c)
    {
      if (*s == *c)
      {
        snprintf (e, 4, "%%%X", *c);
        e += 2;
        encoded_char = true;
        break;
      }
    }
    if (!encoded_char)
      *e = *s;
  }
  *e = '\0';
  return encoded;
}

static void
xgettimeofday (struct timeval *t)
{
  memset (t, 0, sizeof (struct timeval));
  if (gettimeofday (t, NULL) == -1)
  {
    xerror (errno, "gettimeofday failed");
    exit (E_SYSTEM);
  }
}

static int
console_width (void)
{
  struct winsize w;

  if (ioctl (STDOUT_FILENO, TIOCGWINSZ, &w) != -1)
    return w.ws_col;
  return FALLBACK_CONSOLE_WIDTH;
}

static size_t
page_write_cb (void *buf, size_t size, size_t nmemb, void *data)
{
  size_t n;
  struct page_content *p;

  p = (struct page_content *) data;
  n = size * nmemb;

  p->buffer = xrenewa (char, p->buffer, p->n + n + 1);
  memcpy (p->buffer + p->n, buf, n);
  p->n += n;
  p->buffer[p->n] = '\0';
  return n;
}

static int
progress_cb (void *data, double dt, double dc, double ut, double uc)
{
  static const char propeller[PROPELLER_SIZE] = {'-', '\\', '|', '/'};
  static size_t propeller_pos = 0;
  static struct timeval last = {-1, -1};

  int space_remaining;
  long millis;
  struct timeval now;

  if (last.tv_sec == -1 || last.tv_usec == -1)
    millis = -1;
  else
  {
    xgettimeofday (&now);
    millis = get_millis (last, now);
  }

  if (millis == -1 || propeller_rotate_interval_passed (millis)) {
    space_remaining = console_width ();
    fputs (PROGRESS_LOADING_MESSAGE, stdout);
    space_remaining -= strlen (PROGRESS_LOADING_MESSAGE);
    if (propeller_pos == PROPELLER_SIZE)
      propeller_pos = 0;
    fputc (propeller[propeller_pos++], stdout);
    space_remaining--;
    while (space_remaining > 1)
    {
      fputc (' ', stdout);
      space_remaining--;
    }
    fputc ('\r', stdout);
    fflush (stdout);
    if (millis == -1)
      xgettimeofday (&last);
    else
    {
      last.tv_sec = now.tv_sec;
      last.tv_usec = now.tv_usec;
    }
  }
  return 0;
}

static void
progress_finish (void)
{
  int i;
  int w;

  w = console_width ();
  for (i = 0; i < w; ++i)
    fputc (' ', stdout);
  fputc ('\r', stdout);
}

static void
check_curl_status (CURL *cp, CURLcode status)
{
  if (status != CURLE_OK)
  {
    xerror (0, "libcurl error: %s", curl_easy_strerror (status));
    curl_easy_cleanup (cp);
    exit (E_INTERNET);
  }
}

static bool
try_connect (const char *url)
{
  CURL *cp = curl_easy_init ();

  if (!cp)
  {
    xerror (0, "failed to initialize libcurl: %s",
            curl_easy_strerror (CURLE_FAILED_INIT));
    return false;
  }

  xdebug ("connecting to \"%s\"...", url);
  page.n = 0;
  page.buffer = xnewa (char, 1);

#define __setopt(o, p) check_curl_status (cp, curl_easy_setopt (cp, o, p))
  __setopt (CURLOPT_URL, url);
  __setopt (CURLOPT_USERAGENT, USERAGENT);
  __setopt (CURLOPT_FAILONERROR, 1L);
  __setopt (CURLOPT_FOLLOWLOCATION, 1L);
  __setopt (CURLOPT_WRITEDATA, &page);
  __setopt (CURLOPT_WRITEFUNCTION, &page_write_cb);
  __setopt (CURLOPT_NOPROGRESS, 0L);
  __setopt (CURLOPT_PROGRESSFUNCTION, &progress_cb);
#undef __setopt

  bool result = true;

  CURLcode status = curl_easy_perform (cp);
  progress_finish ();

  if (status != CURLE_OK)
  {
    long res = 0L;
    if (curl_easy_getinfo (cp, CURLINFO_RESPONSE_CODE, &res) == CURLE_OK)
      xerror (0, "%s (http response=%li)", curl_easy_strerror (status), res);
    else
      xerror (0, curl_easy_strerror (status));
    result = false;
  }

  curl_easy_cleanup (cp);
  return result;
}

static struct
{
  char c;
  size_t n1;
  size_t n2;
  char *s1;
  char *s2;
}
entity_ref[] =
{
  {'"', 6, 5, "&quot;", "&#34;"},
  {'&', 5, 5, "&amp;", "&#38;"},
  {'\'', 6, 5, "&apos;", "&#39;"},
  {'<', 4, 5, "&lt;", "&#60;"},
  {'>', 4, 5, "&gt;", "&#62;"},
  {' ', 6, 6, "&nbsp;", "&#160;"},
  {'\0', 0, 0, NULL, NULL}
};

static bool
is_entity_ref (const char *s)
{
  size_t i;

  for (i = 0; entity_ref[i].c; ++i)
    if (xstrncasecmp (s, entity_ref[i].s1, entity_ref[i].n1) == 0 ||
        memcmp (s, entity_ref[i].s2, entity_ref[i].n2) == 0)
      return true;
  return false;
}

static char
entity_ref_char (char **s)
{
  size_t i;
  size_t n = 0;

  for (i = 0; entity_ref[i].c; ++i)
  {
    if (xstrncasecmp (*s, entity_ref[i].s1, entity_ref[i].n1) == 0)
      n = entity_ref[i].n1;
    else if (memcmp (*s, entity_ref[i].s2, entity_ref[i].n2) == 0)
      n = entity_ref[i].n2;
    if (n != 0)
    {
      *s += n;
      return entity_ref[i].c;
    }
  }
  return '&';
}

static void
parse_search_page (void)
{
  char *p;

  p = strstr (page.buffer, SEARCH_SHOW_PATTERN);
  if (p)
  {
    char *h = strstr (p, SEARCH_HREF_PATTERN);
    if (h)
    {
      char *u;
      h += strlen (SEARCH_HREF_PATTERN);
      for (u = series->title.url; *h != '/'; ++h, ++u)
        *u = *h;
      *u = '\0';
    }
  }

  if (!*series->title.url)
  {
    xerror (0, "failed to find title \"%s\"", series->title.given);
    exit (E_INTERNET);
  }
}

static void
parse_series_proper_title (void)
{
  char *p;
  char *t;

  series->title.proper[0] = '\0';
  p = strstr (page.buffer, SERIES_TITLE_PATTERN);

  if (p && *p)
  {
    p += strlen (SERIES_TITLE_PATTERN);
    for (t = series->title.proper; *p && *p != '-'; ++p, ++t)
      *t = *p;
    *t = '\0';
  }

  if (!*series->title.proper)
    xdebug ("failed to parse proper title");
  strip_trailing_space (series->title.proper);
}

static void
parse_series_description (void)
{
  size_t n;
  char *d;
  char *p;

  n = 0;
  p = strstr (page.buffer, SERIES_DESCRIPTION_PATTERN);

  if (p && *p)
  {
    p += strlen (SERIES_DESCRIPTION_PATTERN);
    for (; *p && *p != '"'; ++p, ++n)
      ;
  }

  if (n == 0)
  {
    series->description = xstrdup (EMPTY_SERIES_DESCRIPTION, -1);
    return;
  }

  series->description = xnewa (char, n + 1);
  p = strstr (page.buffer, SERIES_DESCRIPTION_PATTERN);

  if (p && *p)
  {
    p += strlen (SERIES_DESCRIPTION_PATTERN);
    for (d = series->description; *p && *p != '"'; ++d, ++p)
    {
      while (is_entity_ref (p))
        *d++ = entity_ref_char (&p);
      *d = *p;
    }
    *d = '\0';
  }

  if (!series->description || !*series->description)
    xdebug ("failed to parse series description");
}

static void
parse_series_airs_on (void)
{
  char *a;
  char *p;

  p = strstr (page.buffer, SERIES_AIRS_ON_PATTERN);
  if (p && *p)
  {
    p += strlen (SERIES_AIRS_ON_PATTERN);
    for (a = series->airs_on; *p != '<'; ++a, ++p)
      *a = *p;
    *a = '\0';
  }
}

static void
spec_append (struct spec *s, int value)
{
  s->v[s->n++] = value;
}

static bool
spec_parse_from_optarg (struct spec *s, char *arg)
{
  char *p;

  for (p = arg; *p; ++p)
    if (!isdigit (*p) && *p != SPEC_DELIM_C)
      return false;

  for (p = strtok (arg, SPEC_DELIM_S); p; p = strtok (NULL, SPEC_DELIM_S))
    spec_append (s, (int) strtoll (p, (char **) NULL, 10));
  return true;
}

static bool
spec_contains (const struct spec *s, int value)
{
  int i;

  for (i = 0; i < s->n; ++i)
    if (s->v[i] == value)
      return true;
  return false;
}

static void
parse_episodes_page (void)
{
  int i;
  char *p;

  parse_series_proper_title ();
  parse_series_description ();
  parse_series_airs_on ();

  for (i = 1;; ++i)
  {
    season_pattern (s, i);
    p = strstr (page.buffer, html_pattern_s);
    if (!p)
      break;
    series->total_seasons++;
  }
}

static void
init_series (void)
{
  series = xnew (struct series);

  series->total_episodes = 0;
  series->total_seasons = 0;

  series->rating = -1.0f;

  series->air_start[0] = '\0';
  series->air_end[0] = '\0';

  series->title.proper[0] = '\0';
  series->title.url[0] = '\0';
  series->title.given = NULL;

  series->description = NULL;
}

static void
init_season (struct season *season)
{
  season->total_episodes = 0;
  season->rating = -1.0f;
}

static void
init_episode (struct episode *episode)
{
  episode->has_aired = false;
  episode->rating = 0.0f;
  episode->title[0] = '\0';
  episode->air[0] = '\0';
  episode->description = NULL;
}

static void
parse_episode_title (struct episode *episode, char **secp)
{
  ssize_t p;
  char *t;
  char *q;

  for (p = *secp - page.buffer; p >= 0; --p)
  {
    if (page.buffer[p] == '<' &&
        page.buffer[p + 1] == '/' &&
        page.buffer[p + 2] == 'a' &&
        page.buffer[p + 3] == '>')
    {
      for (p--; page.buffer[p] != '>'; --p)
        ;
      for (t = episode->title, q = page.buffer + (p + 1); *q != '<'; ++t, ++q)
        *t = *q;
      *t = '\0';
      break;
    }
  }
}

static void
parse_episode_air (struct episode *episode, char **secp)
{
  char *a;
  char *p;

  p = strstr (*secp, EPISODE_AIR_PATTERN);
  if (p && *p)
  {
    p += strlen (EPISODE_AIR_PATTERN);
    for (a = episode->air; *p != '<'; ++a, ++p)
      *a = *p;
    *a = '\0';
  }
}

static void
parse_episode_rating (struct episode *episode, char **secp)
{
  char buffer[XBUFMAX];
  char *r;
  char *p;

  buffer[0] = '\0';
  p = strstr (*secp, EPISODE_RATING_PATTERN);

  if (p && *p)
  {
    p += strlen (EPISODE_RATING_PATTERN);
    for (; *p != '>'; ++p)
      ;
    for (p++, r = buffer; *p != '<'; ++r, ++p)
      *r = *p;
    *r = '\0';
  }

  if (*buffer)
    episode->rating = strtod (buffer, (char **) NULL);
}

static void
parse_episode_description (struct episode *episode, char **secp)
{
  bool end;
  size_t n;
  char *d;
  char *p;

  n = 0;
  p = strstr (*secp, EPISODE_DESCRIPTION_PATTERN);

  if (p && *p)
  {
    p += strlen (EPISODE_DESCRIPTION_PATTERN);
    if (*p == '<' && *(p + 1) == '/')
    {
      episode->description = xstrdup (EMPTY_EPISODE_DESCRIPTION, -1);
      return;
    }
    for (;;)
    {
      if (*p == '<' || *p == '>' || isspace (*p))
      {
        if (*p == '<')
          while (*p != '>')
            p++;
        else
          p++;
        continue;
      }
      break;
    }
    for (; *p; ++p, ++n)
    {
      end = false;
      while (*p == '<')
      {
        if (*(p + 1) == '/')
        {
          end = true;
          break;
        }
        while (*p != '>')
          p++;
        if (*p == '>')
          p++;
      }
      if (end)
        break;
    }
  }

  episode->description = xnewa (char, n + 1);
  p = strstr (*secp, EPISODE_DESCRIPTION_PATTERN);

  if (p && *p)
  {
    p += strlen (EPISODE_DESCRIPTION_PATTERN);
    for (;;)
    {
      if (*p == '<' || *p == '>' || isspace (*p))
      {
        if (*p == '<')
          while (*p != '>')
            p++;
        else
          p++;
        continue;
      }
      break;
    }
    for (d = episode->description; *p; ++d, ++p)
    {
      end = false;
      while (*p == '<')
      {
        if (*(p + 1) == '/')
        {
          end = true;
          break;
        }
        while (*p != '>')
          p++;
        if (*p == '>')
          p++;
      }
      if (end)
        break;
      while (is_entity_ref (p))
        *d++ = entity_ref_char (&p);
      *d = *p;
    }
    *d = '\0';
  }

  if (!episode->description || !*episode->description)
    xdebug ("failed to parse episode description (\"%s\")", episode->title);
}

static void
get_air_time_from_airs_on (char *buffer)
{
  char *p;
  char *q;

  p = strchr (series->airs_on, ':');
  if (p && *p)
  {
    for (; *p != ' '; --p)
      ;
    if (*p == ' ')
    {
      q = buffer;
      *q = ' ';
      for (p++, q++; *p != ' '; ++p, ++q)
        *q = *p;
      *q = '\0';
    }
  }
}

static void
set_episode_has_aired (struct episode *episode)
{
  size_t n;
  time_t a;
  char buffer[XBUFMAX];
  struct tm t;

  n = strlen (episode->air);
  memcpy (buffer, episode->air, n + 1);
  get_air_time_from_airs_on (buffer + n);

  memset (&t, 0, sizeof (struct tm));
  strptime (buffer, "%m/%d/%y %I:%M %p", &t);

  a = mktime (&t);
  if (a == -1)
  {
    xerror (0, "failed to get time value from air date/time \"%s\"", buffer);
    return;
  }

  if (a < time (NULL))
    episode->has_aired = true;

  if (!episode->has_aired)
    episode->rating = -1.0f;
}

static void
set_season_rating (struct season *season)
{
  int e;
  int total;
  double x;

  /* get average of all episode ratings this season */
  total = 0;
  x = 0.0f;

  for (e = 0; e < season->total_episodes; ++e)
  {
    if (season->episode[e].has_aired)
    {
      total++;
      x += season->episode[e].rating;
    }
  }
  season->rating = x / total;
}

static void
parse_season_page (struct season *season)
{
  int i;
  char *p;
  struct episode *episode;

  init_season (season);
  for (i = 0;; ++i)
  {
    episode_pattern (e, i + 1);
    p = strstr (page.buffer, html_pattern_e);
    if (!p)
      break;
    season->total_episodes++;
    episode = &season->episode[i];
    init_episode (episode);
    parse_episode_title (episode, &p);
    parse_episode_air (episode, &p);
    set_episode_has_aired (episode);
    parse_episode_rating (episode, &p);
    parse_episode_description (episode, &p);
  }
  set_season_rating (season);
}

static void
set_series_start_end_airs (void)
{
  memcpy (series->air_start,
          EPISODE (SEASON (0), 0).air,
          strlen (EPISODE (SEASON (0), 0).air) + 1);

  memcpy (series->air_end,
          LAST_EPISODE_OF (LAST_SEASON).air,
          strlen (LAST_EPISODE_OF (LAST_SEASON).air) + 1);
}

static void
set_series_total_episodes (void)
{
  int s;

  for (s = 0; s < series->total_seasons; ++s)
    series->total_episodes += SEASON (s).total_episodes;
}

static void
set_series_rating (void)
{
  int s;
  int total;
  double x;

  /* get average of all season ratings for a rating of the entire series */
  total = 0;
  x = 0.0f;

  for (s = 0; s < series->total_seasons; ++s)
  {
    if (SEASON (s).rating >= 0.0f)
    {
      total++;
      x += SEASON (s).rating;
    }
  }
  series->rating = x / total;
}

static void
retrieve_series (const struct tvi_options *x)
{
  search_url (search);
  if (try_connect (url_search))
  {
    parse_search_page ();
    episodes_url (e);
    if (try_connect (url_e))
    {
      parse_episodes_page ();
      int i;
      for (i = 0; i < series->total_seasons; ++i)
      {
        season_url (s, i + 1);
        if (try_connect (url_s))
          parse_season_page (&SEASON (i));
        else
          die (E_INTERNET, "failed to connect to \"%s\"", url_s);
      }
      set_series_start_end_airs ();
      set_series_total_episodes ();
      set_series_rating ();
    }
    else
      die (E_INTERNET, "failed to connect to \"%s\"", url_e);
  }
  else
    die (E_INTERNET, "failed to connect to \"%s\"", url_search);
}

static void
display_episode (int s, int e, const struct tvi_options *x)
{
  struct episode *episode = &EPISODE (SEASON (s), e);

  printf ("Season %02i Episode %02i: %s", s + 1, e + 1, episode->title);

  if (x->episode_attrs & E_ATTR_RATING)
  {
    if (episode->has_aired)
      printf (" - %.1f", episode->rating);
    else
      fputs (" - not rated", stdout);
  }

  if (x->episode_attrs & E_ATTR_AIR)
  {
    if (x->episode_attrs & E_ATTR_RATING)
      fputs (" -", stdout);
    fputc (' ', stdout);
    if (episode->has_aired)
      fputs ("AIRED ", stdout);
    else
      fputs ("AIRS ", stdout);
    fputs (episode->air, stdout);
  }

  fputc('\n', stdout);

  if (x->episode_attrs & E_ATTR_DESCRIPTION)
    printf ("    %s\n\n", episode->description);
}

static void
display_series (const struct tvi_options *x)
{
  int e;
  int i;
  int j;
  int s;

  if (x->info)
  {
    printf ("%s (%i seasons, %i episodes) %s - %s\n",
            TITLE, series->total_seasons, series->total_episodes,
            series->air_start, series->air_end);
    printf ("Airs %s\n", series->airs_on);
      for (s = 0; s < series->total_seasons; ++s)
        printf ("Season %i rating: %.1f\n", s + 1, SEASON (s).rating);
    printf ("Series overall rating: %.1f\n", series->rating);
    printf ("    %s\n", series->description);
    return;
  }

  if (x->highest_rated || x->lowest_rated)
  {
    if (x->e.n > 1)
      printf ("There is a tie between %i %s rated episodes of \"%s\".\n\n",
              x->e.n, (x->highest_rated) ? "highest" : "lowest", TITLE);
    for (i = 0; i < x->e.n; ++i)
      display_episode (x->s.v[i] - 1, x->e.v[i] - 1, x);
    return;
  }

  if (x->last || x->next)
  {
    if (x->s.v[0] == -1 && x->e.v[0] == -1)
    {
      if (x->last)
        printf ("\"%s\" has not yet aired any episodes.\n", TITLE);
      else
      {
        printf ("\"%s\" has no new episodes.\n", TITLE);
        printf ("The last episode aired on %s.\n",
                LAST_EPISODE_OF (LAST_SEASON).air);
      }
      return;
    }
  }

  /* seasons specified: no
     episodes specified: no */
  if (x->s.n == 0 && x->e.n == 0)
  {
    for (s = 0; s < series->total_seasons; ++s)
      for (e = 0; e < SEASON (s).total_episodes; ++e)
        display_episode (s, e, x);
    return;
  }

  /* seasons specified: yes
     episodes specified: no */
  if (x->s.n > 0 && x->e.n == 0)
  {
    for (i = 0; i < x->s.n; ++i)
      for (e = 0; e < SEASON (x->s.v[i] - 1).total_episodes; ++e)
        display_episode (x->s.v[i] - 1, e, x);
    return;
  }

  /* seasons specified: no
     episodes specified: yes */
  if (x->s.n == 0 && x->e.n > 0)
  {
    for (i = 0; i < x->e.n; ++i)
      for (s = 0, j = 0; s < series->total_seasons; ++s)
        for (e = 0; e < SEASON (s).total_episodes; ++e, ++j)
          if (x->e.v[i] == j + 1 || x->e.v[i] == e + 1)
            display_episode (s, e, x);
     return;
  }

  /* seasons specified: yes
     episodes specified: yes */
  if (x->s.n > 0 && x->e.n > 0)
  {
    for (i = 0; i < x->s.n; ++i)
      for (j = 0; j < x->e.n; ++j)
        display_episode (x->s.v[i] - 1, x->e.v[j] - 1, x);
    return;
  }
}

static void
verify_options (const struct tvi_options *x)
{
  if (x->highest_rated)
  {
    if (x->info)
      xerror (0, "options --highest-rated and --info are mutually exclusive");
    if (x->last)
      xerror (0, "options --highest-rated and --last are mutually exclusive");
    if (x->lowest_rated)
      xerror (0, "options --highest-rated and --lowest-rated are mutually "
                 "exclusive");
    if (x->next)
      xerror (0, "options --highest-rated and --next are mutually exclusive");
    if (x->s.n > 0)
      xerror (0, "options --highest-rated and --season are mutually "
                 "exclusive");
    if (x->e.n > 0)
      xerror (0, "options --highest-rated and --episode are mutually "
                 "exclusive");
    if (x->info || x->last || x->lowest_rated ||
        x->next || x->s.n > 0 || x->e.n > 0)
      usage (true);
  }

  if (x->lowest_rated)
  {
    if (x->info)
      xerror (0, "options --lowest-rated and --info are mutually exclusive");
    if (x->last)
      xerror (0, "options --lowest-rated and --last are mutually exclusive");
    if (x->next)
      xerror (0, "options --lowest-rated and --next are mutually exclusive");
    if (x->s.n > 0)
      xerror (0, "options --lowest-rated and --season are mutually "
                 "exclusive");
    if (x->e.n > 0)
      xerror (0, "options --lowest-rated and --episode are mutually "
                 "exclusive");
    if (x->info || x->last || x->next || x->s.n > 0 || x->e.n > 0)
      usage (true);
  }

  if (x->info)
  {
    if (x->episode_attrs & E_ATTR_AIR)
      xerror (0, "options --info and --air are mutually exclusive");
    if (x->episode_attrs & E_ATTR_DESCRIPTION)
      xerror (0, "options --info and --desc are mutually exclusive");
    if (x->last)
      xerror (0, "options --info and --last are mutually exclusive");
    if (x->next)
      xerror (0, "options --info and --next are mutually exclusive");
    if (x->episode_attrs & E_ATTR_RATING)
      xerror (0, "options --info and --rating are mutually exclusive");
    if (x->s.n > 0)
      xerror (0, "options --info and --season are mutually exclusive");
    if (x->e.n > 0)
      xerror (0, "options --info and --episode are mutually exclusive");
    if (x->episode_attrs & E_ATTR_AIR ||
        x->episode_attrs & E_ATTR_DESCRIPTION || x->last ||
        x->next || x->episode_attrs & E_ATTR_RATING ||
        x->s.n > 0 || x->e.n > 0)
      usage (true);
  }

  if (x->last)
  {
    if (x->next)
      xerror (0, "options --last and --next are mutually exclusive");
    if (x->s.n > 0)
      xerror (0, "options --last and --season are mutually exclusive");
    if (x->e.n > 0)
      xerror (0, "options --last and --episode are mutually exclusive");
    if (x->next || x->s.n > 0 || x->e.n > 0)
      usage (true);
  }

  if (x->next)
  {
    if (x->s.n > 0)
      xerror (0, "options --next and --season are mutually exclusive");
    if (x->e.n > 0)
      xerror (0, "options --next and --episode are mutually exclusive");
    if (x->s.n > 0 || x->e.n > 0)
      usage (true);
  }
}

static void
find_last_to_air_episode (int *season_no, int *episode_no)
{
  int e;
  int s;

  if (series->total_seasons == 0)
  {
    *season_no = -1;
    *episode_no = -1;
    return;
  }

  if (LAST_EPISODE_OF (LAST_SEASON).has_aired)
  {
    *season_no = series->total_seasons - 1;
    *episode_no = SEASON (series->total_seasons - 1).total_episodes - 1;
    return;
  }

  for (s = 0; s < series->total_seasons; ++s)
  {
    e = SEASON (s).total_episodes - 1;
    if (!EPISODE (SEASON (s), e).has_aired)
    {
      for (e--; e >= 0; --e)
      {
        if (EPISODE (SEASON (s), e).has_aired)
        {
          *season_no = s;
          *episode_no = e;
          return;
        }
      }
    }
  }
}

static void
find_next_to_air_episode (int *season_no, int *episode_no)
{
  int e = 0;
  int s = 0;

  find_last_to_air_episode (&s, &e);

  if ((s == -1 && e == -1) ||
      (s == series->total_seasons - 1 &&
       e == LAST_SEASON.total_episodes - 1))
  {
    *season_no = -1;
    *episode_no = -1;
    return;
  }

  if (e < SEASON (s).total_episodes - 1)
  {
    *season_no = s;
    *episode_no = e + 1;
  }
  else
  {
    if (s < series->total_seasons)
    {
      *season_no = s + 1;
      *episode_no = 0;
    }
    else
    {
      *season_no = -1;
      *episode_no = -1;
    }
  }
}

static void
find_highest_rated_episode (int *sa, int *ea)
{
  bool skip;
  int e;
  int s;
  int p;
  int q;
  double r;

  r = 0.0f;
  for (s = 0; s < series->total_seasons; ++s)
  {
    for (e = 0; e < SEASON (s).total_episodes; ++e)
    {
      if (EPISODE (SEASON (s), e).has_aired)
      {
        if (EPISODE (SEASON (s), e).rating > r)
        {
          ea[0] = e;
          sa[0] = s;
          r = EPISODE (SEASON (s), e).rating;
        }
      }
    }
  }

  p = 1;
  for (s = 0; s < series->total_seasons; ++s)
  {
    for (e = 0; e < SEASON (s).total_episodes; ++e)
    {
      if (EPISODE (SEASON (s), e).rating == r)
      {
        skip = false;
        for (q = p - 1; q >= 0; --q)
        {
          if (e == ea[q] && s == sa[q])
          {
            skip = true;
            break;
          }
        }
        if (!skip)
        {
          ea[p] = e;
          sa[p++] = s;
        }
      }
    }
  }

  ea[p] = -1;
  sa[p] = -1;
}

static void
find_lowest_rated_episode (int *sa, int *ea)
{
  bool skip;
  int e;
  int s;
  int p;
  int q;
  double r;

  r = DBL_MAX;
  for (s = 0; s < series->total_seasons; ++s)
  {
    for (e = 0; e < SEASON (s).total_episodes; ++e)
    {
      if (EPISODE (SEASON (s), e).has_aired)
      {
        if (EPISODE (SEASON (s), e).rating < r)
        {
          ea[0] = e;
          sa[0] = s;
          r = EPISODE (SEASON (s), e).rating;
        }
      }
    }
  }

  for (s = 0; s < series->total_seasons; ++s)
  {
    for (e = 0; e < SEASON (s).total_episodes; ++e)
    {
      if (EPISODE (SEASON (s), e).rating == r)
      {
        skip = false;
        for (q = p - 1; q >= 0; --q)
        {
          if (e == ea[q] && s == sa[q])
          {
            skip = true;
            break;
          }
        }
        if (!skip)
        {
          ea[p] = e;
          sa[p++] = s;
        }
      }
    }
  }

  ea[p] = -1;
  sa[p] = -1;
}

static void
verify_options_with_series (struct tvi_options *x)
{
  bool had_error;
  int e = 0;
  int s = 0;

  if (x->highest_rated || x->lowest_rated)
  {
    int ea[XBUFMAX];
    int sa[XBUFMAX];
    if (x->highest_rated)
      find_highest_rated_episode (sa, ea);
    else
      find_lowest_rated_episode (sa, ea);
    for (s = 0; sa[s] != -1; ++s)
      spec_append (&x->s, sa[s] + 1);
    for (e = 0; ea[e] != -1; ++e)
      spec_append (&x->e, ea[e] + 1);
    x->episode_attrs |= E_ATTR_AIR | E_ATTR_DESCRIPTION | E_ATTR_RATING;
    return;
  }

  if (x->last || x->next)
  {
    if (x->last)
      find_last_to_air_episode (&s, &e);
    else
      find_next_to_air_episode (&s, &e);
    if (s != -1 && e != -1)
    {
      s++;
      e++;
    }
    spec_append (&x->s, s);
    spec_append (&x->e, e);
    x->episode_attrs |= E_ATTR_AIR | E_ATTR_DESCRIPTION;
    if (x->last)
      x->episode_attrs |= E_ATTR_RATING;
    return;
  }

  if (x->s.n == 0 && x->e.n > 0)
  {
    had_error = false;
    for (e = 0; e < x->e.n; ++e)
    {
      if (x->e.v[e] <= 0 || x->e.v[e] > series->total_episodes)
      {
        xerror (0, "invalid episode specified -- %i", x->e.v[e]);
        had_error = true;
      }
    }
    if (had_error)
    {
      xerror (0, "\"%s\" has a total of %i episodes",
              TITLE, series->total_episodes);
      die (E_OPTION, "specify a value between 1-%i", series->total_episodes);
    }
  }

  if (x->s.n > 0)
  {
    had_error = false;
    for (s = 0; s < x->s.n; ++s)
    {
      if (x->s.v[s] <= 0 || x->s.v[s] > series->total_seasons)
      {
        xerror (0, "invalid season specified -- %i", x->s.v[s]);
        had_error = true;
      }
    }
    if (had_error)
    {
      xerror (0, "\"%s\" has a total of %i seasons",
              TITLE, series->total_seasons);
      die (E_OPTION, "specify a value between 1-%i", series->total_seasons);
    }
  }

  if (x->s.n > 0 && x->e.n > 0)
  {
    bool had_season_episode_error;
    had_error = false;
    for (s = 0; s < x->s.n; ++s)
    {
      had_season_episode_error = false;
      for (e = 0; e < x->e.n; ++e)
      {
        if (x->e.v[e] <= 0 ||
            x->e.v[e] > SEASON (x->s.v[s] - 1).total_episodes)
        {
          xerror (0, "invalid episode specified for season %i -- %i",
                  x->s.v[s], x->e.v[e]);
          had_season_episode_error = true;
        }
      }
      if (had_season_episode_error)
      {
        xerror (0, "season %i of \"%s\" has a total of %i episodes",
                x->s.v[s], TITLE, SEASON (x->s.v[s] - 1).total_episodes);
        xerror (0, "specify value(s) between 1-%i",
                SEASON (x->s.v[s] - 1).total_episodes);
        had_error = true;
      }
    }
    if (had_error)
      exit (E_OPTION);
  }
}

static void
init_tvi_options (struct tvi_options *x)
{
  x->highest_rated = false;
  x->info = false;
  x->last = false;
  x->lowest_rated = false;
  x->next = false;
  x->episode_attrs = E_ATTR_0;
  x->e.n = 0;
  x->s.n = 0;
}

static void
cleanup (void)
{
  int e;
  int s;

  xfree (page.buffer);
  if (series)
  {
    xfree (series->title.given);
    xfree (series->description);
    for (s = 0; s < series->total_seasons; ++s)
      for (e = 0; e < SEASON (s).total_episodes; ++e)
        xfree (EPISODE (SEASON (s), e).description);
  }
  xfree (series);
}

int
main (int argc, char **argv)
{
  int c;
  struct tvi_options x;

  set_program_name (argv[0]);
  atexit (cleanup);
  init_tvi_options (&x);

  for (;;)
  {
    c = getopt_long (argc, argv, "ade:hHlLnirs:v", options, NULL);
    if (c == -1)
      break;
    switch (c)
    {
      case 'a':
        x.episode_attrs |= E_ATTR_AIR;
        break;
      case 'd':
        x.episode_attrs |= E_ATTR_DESCRIPTION;
        break;
      case 'e':
        if (!spec_parse_from_optarg (&x.e, optarg))
        {
          xerror (0, "invalid episode argument -- `%s'", optarg);
          die (E_OPTION, SPEC_ERROR_MESSAGE);
        }
        break;
      case 'h':
        usage(false);
        break;
      case 'H':
        x.highest_rated = true;
        break;
      case 'i':
        x.info = true;
        break;
      case 'l':
        x.last = true;
        break;
      case 'L':
        x.lowest_rated = true;
        break;
      case 'n':
        x.next = true;
        break;
      case 'r':
        x.episode_attrs |= E_ATTR_RATING;
        break;
      case 's':
        if (!spec_parse_from_optarg (&x.s, optarg))
        {
          xerror (0, "invalid season argument -- `%s'", optarg);
          die (E_OPTION, SPEC_ERROR_MESSAGE);
        }
        break;
      case 'v':
        version ();
        break;
      default:
        usage (true);
        break;
    }
  }

  if (argc <= optind)
  {
    xerror (0, "missing TV series title");
    usage (true);
  }

  verify_options (&x);
  init_series ();
  set_series_given_title (argv + optind);
  retrieve_series (&x);
  verify_options_with_series (&x);
  display_series (&x);
  exit (E_OKAY);
}

