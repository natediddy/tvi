/*
 * ep - show television series episode information
 *
 * Copyright (C) 2014 Nathan Forbes
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
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

#include <curl/curl.h>

#ifdef PACKAGE_NAME
# define EP_PROGRAM_NAME PACKAGE_NAME
#else
# define EP_PROGRAM_NAME "ep"
#endif

#ifdef PACKAGE_VERSION
# define EP_VERSION PACKAGE_VERSION
#else
# define EP_VERSION "2.0.0"
#endif

#define EPISODES_URL "http://www.tv.com/shows/%s/episodes/"
#define SEASON_URL   "http://www.tv.com/shows/%s/season-%u/"
#define USERAGENT    EP_PROGRAM_NAME " (tv series info)/" EP_VERSION

#define U_TITLE_IGNORE_CHARS ".'"

#define DEFAULT_TERM_WIDTH 40
#define MILLIS_PER_SEC     1000L
#define PROPELLER_SIZE     4

/* html patterns to look for when parsing web pages */
#define TAG_A_END                   "</a>"
#define TAG_A_END_SIZE              4 /* strlen("</a>") */
#define SERIES_TITLE_PATTERN        "<title>"
#define SERIES_DESCRIPTION_PATTERN  "\"og:description\" content=\""
#define SEASON_PATTERN              "<strong>Season %u"
#define EPISODE_PATTERN             "Episode %u\r\n"
#define EPISODE_AIR_DATE_PATTERN    "class=\"date\">"
#define EPISODE_DESCRIPTION_PATTERN "class=\"description\">"
#define EPISODE_RATING_PATTERN      "_rating"

/* various buffer sizes */
#define URL_BUFMAX            1024
#define SEASON_PATTERN_BUFMAX  256
#define EPISODE_PATTERN_BUFMAX 256
#define AIR_DATE_BUFMAX         24
#define DESCRIPTION_BUFMAX    6144 /* 6KB just in case (they can be large) */
#define RATING_BUFMAX           24
#define SERIES_TITLE_BUFMAX 1024
#define EPISODE_TITLE_BUFMAX  1024

#define EPISODES_MAX 1024
#define SEASONS_MAX  1024

#define propeller_wait_time(m) ((m) >= (MILLIS_PER_SEC / 25L))

#define ep_new(type)          ((type *) ep_malloc (sizeof (type)))
#define ep_newa(type, n)      ((type *) ep_malloc ((n) * sizeof (type)))
#define ep_renewa(type, p, n) ((type *) ep_realloc (p, (n) * sizeof (type)))
#define ep_free(p) \
  do \
  { \
    if (p) \
    { \
      free (p); \
      p = NULL; \
    } \
  } while (0)

typedef unsigned char bool;
#define false ((bool) 0)
#define true  ((bool) 1)

#ifdef DEBUG
# undef __FUNC
# if defined (__GNUC__)
#  define __FUNC ((const char *) (__PRETTY_FUNCTION__))
# elif defined (__STDC_VERSION__) && (__STDC_VERSION__ >= 19901L)
#  define __FUNC ((const char *) (__func__))
# elif defined (_MSC_VER) && (_MSC_VER > 1300)
#  define __FUNC ((const char *) (__FUNCTION__))
# endif
# ifdef __FUNC
static const bool has___FUNC = true;
# else
static const bool has___FUNC = false;
# endif
# define debug(...) \
  do \
  { \
    char __tag[512]; \
    if (has___FUNC) \
      snprintf (__tag, 512, "%s:DEBUG:%s():%i: ", \
                program_name, __FUNC, __LINE__); \
    else \
      snprintf (__tag, 512, "%s:DEBUG:%i: ", program_name, __LINE__); \
    debug_p (__tag, __VA_ARGS__); \
  } while (0)
#else
# define debug(...)
#endif

/* macros that help hide creating and formatting static buffers */
#define __urlvar(v) url_ ## v
#define urlvar__(v) __urlvar (v)

#define episodes_url(v) \
  char urlvar__ (v)[URL_BUFMAX]; \
  snprintf (urlvar__ (v), URL_BUFMAX, EPISODES_URL, series->u_title)

#define season_url(v, n) \
  char urlvar__ (v)[URL_BUFMAX]; \
  snprintf (urlvar__ (v), URL_BUFMAX, SEASON_URL, series->u_title, (n))

#define __patvar(v) pat_ ## v
#define patvar__(v) __patvar (v)

#define season_pattern(v, n) \
  char patvar__ (v)[SEASON_PATTERN_BUFMAX]; \
  snprintf (patvar__ (v), SEASON_PATTERN_BUFMAX, SEASON_PATTERN, (n))

#define episode_pattern(v, n) \
  char patvar__ (v)[EPISODE_PATTERN_BUFMAX]; \
  snprintf (patvar__ (v), EPISODE_PATTERN_BUFMAX, EPISODE_PATTERN, (n))

struct page_content
{
  size_t n;
  char *buf;
};

struct episode
{
  char title[EPISODE_TITLE_BUFMAX];
  char air_date[AIR_DATE_BUFMAX];
  char rating[RATING_BUFMAX];
  char *description;
};

struct season
{
  unsigned int total_episodes;
  struct episode episode[EPISODES_MAX];
};

struct series
{
  unsigned int total_episodes;
  unsigned int total_seasons;
  char title[SERIES_TITLE_BUFMAX]; /* proper title, e.g.: "The Wire" */
  char *description;
  char *g_title; /* given title (from command line, e.g.: "the wire") */
  char *u_title; /* URL title (to be inserted into URL, e.g.: "the_wire") */
  struct season season[SEASONS_MAX];
};

struct ep_options
{
  bool show_air_dates;
  bool show_descriptions;
  bool show_ratings;
  bool show_title;
  bool series_info;
  unsigned int season_spec;
  unsigned int episode_spec;
};

static const char *        program_name;
static struct page_content pc     = {0, NULL};
static struct series *     series = NULL;

static struct option const options[] =
{
  {"air", no_argument, NULL, 'a'},
  {"description", no_argument, NULL, 'd'},
  {"episode", required_argument, NULL, 'e'},
  {"info", no_argument, NULL, 'i'},
  {"rating", no_argument, NULL, 'r'},
  {"season", required_argument, NULL, 's'},
  {"no-title", no_argument, NULL, 't'},
  {"help", no_argument, NULL, 'h'},
  {"version", no_argument, NULL, 'v'},
  {NULL, 0, NULL, 0}
};

static const char propeller[PROPELLER_SIZE] = "|/-\\";

#ifdef DEBUG
static void
debug_p (const char *tag, const char *fmt, ...)
{
  va_list ap;

  fputs (tag, stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
}
#endif

static void
set_program_name (const char *n)
{
  const char *p;

  if (n && *n)
  {
    p = strrchr (n, '/');
    if (p && *p && *(p + 1))
      program_name = ++p;
    else
      program_name = n;
    return;
  }
  debug ("argv[0] == NULL");
  program_name = EP_PROGRAM_NAME;
}

static void
error (const char *fmt, ...)
{
  va_list ap;

  fprintf (stderr, "%s: error: ", program_name);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
}

static void
die (const char *fmt, ...)
{
  va_list ap;

  fprintf (stderr, "%s: error: ", program_name);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  exit (EXIT_FAILURE);
}

/* Return the length of S while skipping characters within EXCEPT.
   Example:
     size_t n = ep_strlenex ("the s.h.i.e.l.d", ".");
   In this example n would be 10, not 15. */
static size_t
ep_strlenex (const char *s, const char *except)
{
  bool ec;
  size_t n;
  const char *e;
  const char *p;

  n = 0;
  for (p = s; *p; ++p)
  {
    ec = false;
    for (e = except; *e; ++e)
    {
      if (*e == *p)
      {
        ec = true;
        break;
      }
    }
    if (!ec)
      n++;
  }
  return n;
}

/* Copy SRC into DEST while skipping characters within EXCEPT.
   Example:
     char ga[64];
     char ts[64]
     ep_strcpyex (ga, "grey's anatomy", "'");
     ep_strcpyex (ts, "the s.h.i.e.l.d", ".")
     printf ("%s\n", ga);
     printf ("%s\n", ts);
   This example would print the following:
       greys anatomy
       the shield
   NOTE: this automatically adds a terminating '\0' to DEST */
static void *
ep_strcpyex (char *dest, const char *src, const char *except)
{
  bool ec;
  size_t n;
  char *p;
  const char *e;
  const char *s;

  n = ep_strlenex (src, except);

  char buf[n + 1];
  for (p = buf, s = src; *s; ++s)
  {
    ec = false;
    for (e = except; *e; ++e)
    {
      if (*e == *s)
      {
        ec = true;
        break;
      }
    }
    if (!ec)
      *p++ = *s;
  }
  *p = '\0';

  return strncpy (dest, buf, n + 1);
}

static int
ep_strncasecmp (const char *s1, const char *s2, size_t n)
{
  const unsigned char *p1 = (const unsigned char *) s1;
  const unsigned char *p2 = (const unsigned char *) s2;
  int result;

  if ((p1 == p2) || (n == 0))
    return 0;

  while ((result = (tolower (*p1) - tolower (*p2++))) == 0)
    if ((*p1++ == '\0') || (--n == 0))
      break;
  return result;
}

static void *
ep_malloc (size_t n)
{
  void *p;

  p = malloc (n);
  if (!p)
  {
    debug ("malloc() failed");
    die ("internal: %s", strerror (errno));
  }
  return p;
}

static void *
ep_realloc (void *o, size_t n)
{
  void *p;

  p = realloc (o, n);
  if (!p)
  {
    debug ("realloc() failed");
    die ("internal: %s", strerror (errno));
  }
  return p;
}

static char *
ep_strdup (const char *s)
{
  size_t n;
  void *p;

  n = strlen (s) + 1;
  p = ep_malloc (n);
  return (char *) memcpy (p, s, n);
}

static void
usage (bool had_error)
{
  fprintf ((had_error) ? stderr : stdout,
           "Usage: %s [-i] [-adrt] [-s<N>] [-e<N>] <TITLE>\n",
           program_name);

  if (!had_error)
    fputs ("Options:\n"
           "  -e<N>, --episode=<N>  specify episode <N>\n"
           "  -s<N>, --season=<N>   specify season <N>\n"
           "  -a, --air             print air date for each episode\n"
           "  -d, --description     print description for each episode\n"
           "  -r, --rating          print rating for each episode\n"
           "  -t, --no-title        do not print episode titles\n"
           "  -i, --info            print general info about <TITLE>\n"
           "  -h, --help            print this text and exit\n"
           "  -v, --version         print version info and exit\n"
           "\n"
           "All TV series data is obtained from www.tv.com\n",
           stdout);

  exit ((had_error) ? EXIT_FAILURE : EXIT_SUCCESS);
}

static void
version (void)
{
  fputs (EP_PROGRAM_NAME " " EP_VERSION "\n"
         "Written by Nathan Forbes (2014)\n",
         stdout);
  exit (EXIT_SUCCESS);
}

static void
replace_c (char *s, const char c1, const char c2)
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
set_series_title (char **item)
{
  size_t ng;
  size_t nu;
  size_t p;
  size_t g_pos;
  size_t u_pos;

  ng = 0;
  nu = 0;

  for (p = 0; item[p]; ++p)
  {
    ng += strlen (item[p]);
    nu = ep_strlenex (item[p], U_TITLE_IGNORE_CHARS);
    if (item[p + 1])
    {
      ng++;
      nu++;
    }
  }

  g_pos = 0;
  u_pos = 0;
  series->g_title = ep_newa (char, ng + 1);
  series->u_title = ep_newa (char, nu + 1);

  for (p = 0; item[p]; ++p)
  {
    ng = strlen (item[p]);
    strncpy (series->g_title + g_pos, item[p], ng);
    ep_strcpyex (series->u_title + u_pos, item[p], U_TITLE_IGNORE_CHARS);
    g_pos += ng;
    u_pos += ep_strlenex (item[p], U_TITLE_IGNORE_CHARS);
    if (item[p + 1])
    {
      series->g_title[g_pos++] = ' ';
      series->u_title[u_pos++] = '-';
    }
  }

  series->g_title[g_pos] = '\0';
  series->u_title[u_pos] = '\0';
  replace_c (series->u_title, ' ', '-');
}

static void
ep_gettimeofday (struct timeval *tv)
{
  memset (tv, 0, sizeof (struct timeval));
  if (gettimeofday (tv, NULL) == -1)
  {
    debug ("gettimeofday() failed");
    die ("internal error: %s", strerror (errno));
  }
}

static long
milliseconds (const struct timeval *start, const struct timeval *end)
{
  long sec;
  long usec;

  sec = ((end->tv_sec - start->tv_sec) * MILLIS_PER_SEC);
  usec = ((end->tv_usec - start->tv_usec) / MILLIS_PER_SEC);
  return (sec + usec);
}

static int
term_width (void)
{
  struct winsize ws;

  if (ioctl (STDOUT_FILENO, TIOCGWINSZ, &ws) != -1)
    return ws.ws_col;
  debug ("failed to get terminal width from ioctl()");
  return DEFAULT_TERM_WIDTH;
}

static size_t
write_cb (void *buf, size_t size, size_t nmemb, void *data)
{
  size_t n;
  struct page_content *p;

  p = (struct page_content *) data;
  n = (size * nmemb);

  p->buf = ep_renewa (char, p->buf, p->n + n + 1);
  memcpy (p->buf + p->n, buf, n);
  p->n += n;
  p->buf[p->n] = '\0';
  return n;
}

#define progress_output_s(s, r) \
  do \
  { \
    fputs (s, stdout); \
    r -= strlen (s); \
  } while (0)

#define progress_output_c(c, r) \
  do \
  { \
    fputc (c, stdout); \
    r--; \
  } while (0)

static int
progress_cb (void *data, double dt, double dc, double ut, double uc)
{
  static size_t p_pos = 0;
  static struct timeval l = {-1, -1};

  int r;
  long m;
  struct timeval n;

  if ((l.tv_sec == -1) || (l.tv_usec == -1))
    m = -1;
  else
  {
    ep_gettimeofday (&n);
    m = milliseconds (&l, &n);
  }

  if ((m == -1) || propeller_wait_time (m))
  {
    r = term_width ();
    progress_output_s ("Loading... ", r);
    if (p_pos == PROPELLER_SIZE)
      p_pos = 0;
    progress_output_c (propeller[p_pos++], r);
    while (r > 2)
      progress_output_c (' ', r);
    fputc ('\r', stdout);
    fflush (stdout);
    if (m == -1)
      ep_gettimeofday (&l);
    else
      memcpy (&l, &n, sizeof (struct timeval));
  }
  return 0;
}

static void
progress_finish (void)
{
  int i;
  int w;

  w = term_width ();
  for (i = 0; (i < w); ++i)
    fputc (' ', stdout);
  fputc ('\r', stdout);
}

static bool
check_status (const CURLcode status)
{
  if (status == CURLE_OK)
    return true;
  error (curl_easy_strerror (status));
  return false;
}

static bool
try_net_setup (CURL *cp, CURLcode status, const char *url)
{
  pc.n = 0;
  pc.buf = ep_newa (char, 1);

  status = curl_easy_setopt (cp, CURLOPT_URL, url);
  if (!check_status (status))
    return false;

  status = curl_easy_setopt (cp, CURLOPT_USERAGENT, USERAGENT);
  if (!check_status (status))
    return false;

  status = curl_easy_setopt (cp, CURLOPT_FOLLOWLOCATION, 1L);
  if (!check_status (status))
    return false;

  status = curl_easy_setopt (cp, CURLOPT_WRITEDATA, (void *) &pc);
  if (!check_status (status))
    return false;

  status = curl_easy_setopt (cp, CURLOPT_WRITEFUNCTION, (void *) &write_cb);
  if (!check_status (status))
    return false;

  status = curl_easy_setopt (cp, CURLOPT_NOPROGRESS, 0L);
  if (!check_status (status))
    return false;

  status = curl_easy_setopt (cp, CURLOPT_PROGRESSFUNCTION,
                             (void *) &progress_cb);
  if (!check_status (status))
    return false;
  return true;
}

static bool
try_connect (const char *url)
{
  CURLcode status;
  CURL *cp;

  cp = curl_easy_init ();
  if (!cp)
  {
    error (curl_easy_strerror (CURLE_FAILED_INIT));
    return false;
  }

  debug ("connecting to: \"%s\"", url);

  status = CURLE_OK;
  if (!try_net_setup (cp, status, url))
  {
    curl_easy_cleanup (cp);
    return false;
  }

  status = curl_easy_perform (cp);
  progress_finish ();
  if (!check_status (status))
  {
    curl_easy_cleanup (cp);
    return false;
  }

  curl_easy_cleanup (cp);
  return true;
}

static void
decode_c (char *s, char **p)
{
  if (ep_strncasecmp (*p, "&quot;", 6) == 0)
  {
    *s = '"';
    *p += 6;
  }
  else if (strncmp (*p, "&#34;", 5) == 0)
  {
    *s = '"';
    *p += 5;
  }
  else if (ep_strncasecmp (*p, "&amp;", 5) == 0)
  {
    *s = '&';
    *p += 5;
  }
  else if (strncmp (*p, "&#38;", 5) == 0)
  {
    *s = '&';
    *p += 5;
  }
  else if (ep_strncasecmp (*p, "&apos;", 6) == 0)
  {
    *s = '\'';
    *p += 6;
  }
  else if (strncmp (*p, "&#39;", 5) == 0)
  {
    *s = '\'';
    *p += 5;
  }
  else if (ep_strncasecmp (*p, "&lt;", 4) == 0)
  {
    *s = '<';
    *p += 4;
  }
  else if (strncmp (*p, "&#60;", 5) == 0)
  {
    *s = '<';
    *p += 5;
  }
  else if (ep_strncasecmp (*p, "&gt;", 4) == 0)
  {
    *s = '>';
    *p += 4;
  }
  else if (strncmp (*p, "&#62;", 5) == 0)
  {
    *s = '>';
    *p += 5;
  }
  else if (ep_strncasecmp (*p, "&nbsp;", 6) == 0)
  {
    *s = ' ';
    *p += 6;
  }
  else if (strncmp (*p, "&#160;", 6) == 0)
  {
    *s = ' ';
    *p += 6;
  }
  else
    *s = '&';
}

static void
parse_series_title (void)
{
  char *t;
  char *p;

  *series->title = '\0';
  p = strstr (pc.buf, SERIES_TITLE_PATTERN);

  if (p && *p)
  {
    p += strlen (SERIES_TITLE_PATTERN);
    for (t = series->title; (*p && (*p != '-')); ++t, ++p)
      *t = *p;
    *t = '\0';
  }

  if (!*series->title)
    die ("failed to find any TV series by the name \"%s\"", series->g_title);

  strip_trailing_space (series->title);
}

static void
parse_series_description (void)
{
  char buffer[DESCRIPTION_BUFMAX];
  char *d;
  char *p;

  *buffer = '\0';
  p = strstr (pc.buf, SERIES_DESCRIPTION_PATTERN);

  if (p && *p)
  {
    p += strlen (SERIES_DESCRIPTION_PATTERN);
    for (d = buffer; (*p && (*p != '"')); ++d, ++p)
    {
      while (*p == '&')
        decode_c (d++, &p);
      *d = *p;
    }
    *d = '\0';
  }

  if (!*buffer)
  {
    debug ("failed to parse series description");
    return;
  }
  series->description = ep_strdup (buffer);
}

static void
parse_episodes_page (void)
{
  unsigned int x;
  char *p;
  char *s;

  parse_series_title ();
  parse_series_description ();

  for (x = 1;; ++x)
  {
    season_pattern (s, x);
    p = strstr (pc.buf, pat_s);
    if (!p)
      break;
    series->total_seasons++;
  }
}

static void
parse_episode_air_date (struct episode *episode, char **secp)
{
  char *a;
  char *p;

  *episode->air_date = '\0';
  p = strstr (*secp, EPISODE_AIR_DATE_PATTERN);

  if (p && *p)
  {
    p += strlen (EPISODE_AIR_DATE_PATTERN);
    for (a = episode->air_date; (*p != '<'); ++a, ++p)
      *a = *p;
    *a = '\0';
  }
}

static void
parse_episode_description (struct episode *episode, char **secp)
{
  bool end;
  char buffer[DESCRIPTION_BUFMAX];
  char *d;
  char *p;

  *buffer = '\0';
  p = strstr (*secp, EPISODE_DESCRIPTION_PATTERN);

  /* Traverse forward from SECP skipping any html tags. Once past any tags,
     fill the buffer BUFFER with the characters read. Decode any html
     character entity references (e.g. "&nbsp;") and skip over any html tags
     that are actually within the content along the way. Stop filling BUFFER
     whenever a closing html tag is detected. */

  if (p && *p)
  {
    p += strlen (EPISODE_DESCRIPTION_PATTERN);
    for (;;)
    {
      if ((*p == '<') || (*p == '>') || isspace (*p))
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
    for (d = buffer; *p; ++d, ++p)
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
      while (*p == '&')
        decode_c (d++, &p);
      *d = *p;
    }
    *d = '\0';
  }

  if (!*buffer)
  {
    debug ("failed to parse episode description (\"%s\")", episode->title);
    return;
  }

  episode->description = ep_strdup (buffer);
}

static void
parse_episode_rating (struct episode *episode, char **secp)
{
  char *r;
  char *p;

  *episode->rating = '\0';
  p = strstr (*secp, EPISODE_RATING_PATTERN);

  if (p && *p)
  {
    p += strlen (EPISODE_RATING_PATTERN);
    for (; (*p != '>'); ++p)
      ;
    for (p++, r = episode->rating; (*p != '<'); ++r, ++p)
      *r = *p;
    *r = '\0';
  }
}

static void
parse_episode_title (struct episode *episode, char **secp)
{
  ssize_t p;
  char *t;
  char *s;

  *episode->title = '\0';

  /* SECP will be a pointer to a pointer of the section in the web page that
     contains the pattern "Episode <N>\r\n". However, the title for that
     particular episode will be a ways back in reverse from the pointer. So
     traverse backward until you hit the html tag "</a>", then traverse
     backward from there until you hit the corresponding html tag "<a>".
     Finally, traverse forward from there while filling the buffer
     EPISODE->TITLE with the string between the html tags "<a>" and "</a>". */

  for (p = (*secp - pc.buf); (p >= 0); --p)
  {
    if (strncmp (pc.buf + p, "</a>", 4) == 0)
    {
      for (p--; (p >= 0); --p)
      {
        if (strncmp (pc.buf + p, "<a>", 3) == 0)
        {
          p += 3;
          break;
        }
      }
      for (t = episode->title, s = pc.buf + p; *s; ++t, ++s)
      {
        if (strncmp (s, "</a>", 4) == 0)
          break;
        *t = *s;
      }
      *t = '\0';
      break;
    }
  }
}

static void
init_episode (struct episode *episode)
{
  *episode->title = '\0';
  *episode->air_date = '\0';
  *episode->rating = '\0';
  episode->description = NULL;
}

static void
init_season (struct season *season)
{
  season->total_episodes = 0;
}

static void
parse_season_page (struct season *season)
{
  unsigned int x;
  char *p;

  init_season (season);
  for (x = 0;; ++x)
  {
    episode_pattern (e, x + 1);
    p = strstr (pc.buf, pat_e);
    if (!p)
      break;
    season->total_episodes++;
    init_episode (&season->episode[x]);
    parse_episode_title (&season->episode[x], &p);
    parse_episode_air_date (&season->episode[x], &p);
    parse_episode_rating (&season->episode[x], &p);
    parse_episode_description (&season->episode[x], &p);
  }
}

static void
set_series_total_episodes (void)
{
  unsigned int s;

  for (s = 0; (s < series->total_seasons); ++s)
    series->total_episodes += series->season[s].total_episodes;
}

static void
retrieve_series (void)
{
  unsigned int i;

  episodes_url (e);
  if (try_connect (url_e))
  {
    parse_episodes_page ();
    for (i = 0; (i < series->total_seasons); ++i)
    {
      season_url (s, i + 1);
      if (try_connect (url_s))
        parse_season_page (&series->season[i]);
    }
  }
  set_series_total_episodes ();
}

static void
display_description (const char *desc)
{
  printf ("    %s\n", desc);
}

static void
display_episode (unsigned int s, unsigned int e, const struct ep_options *x)
{
  struct episode *episode = &series->season[s].episode[e];

  printf ("s%02ue%02u:", s + 1, e + 1);

  if (x->show_title)
    printf (" %s", episode->title);

  if (x->show_ratings)
    printf (" (%s)", episode->rating);

  if (x->show_air_dates)
    printf (" %s", episode->air_date);

  fputc ('\n', stdout);

  if (x->show_descriptions)
  {
    display_description (episode->description);
    fputc ('\n', stdout);
  }
}

static void
display_series (const struct ep_options *x)
{
  unsigned int s;
  unsigned int e;

  if (x->series_info)
  {
    printf ("%s (%u seasons - %u episodes)\n",
            series->title, series->total_seasons, series->total_episodes);
    display_description (series->description);
    return;
  }

  if (!x->season_spec && x->episode_spec)
  {
    unsigned int t;
    for (s = 0, t = 0; (s < series->total_seasons); ++s)
      for (e = 0; (e < series->season[s].total_episodes); ++e, ++t)
        if (x->episode_spec == (t + 1))
          display_episode (s, e, x);
    return;
  }

  for (s = 0; (s < series->total_seasons); ++s)
    if (!x->season_spec || (x->season_spec == (s + 1)))
      for (e = 0; (e < series->season[s].total_episodes); ++e)
        if (!x->episode_spec || (x->episode_spec == (e + 1)))
          display_episode (s, e, x);
}

static void
init_series (void)
{
  unsigned int s;
  unsigned int e;

  series = ep_new (struct series);
  *series->title = '\0';
  series->total_seasons = 0;
  series->total_episodes = 0;
  series->g_title = NULL;
  series->u_title = NULL;
  series->description = NULL;
}

static void
cleanup (void)
{
  ep_free (pc.buf);
  if (series)
  {
    ep_free (series->g_title);
    ep_free (series->u_title);
  }
  ep_free (series);
}

static void
verify_options_with_series (const struct ep_options *x)
{
  if (!x->season_spec && (x->episode_spec > series->total_episodes))
  {
    error ("invalid episode specified");
    die ("\"%s\" has a total of %u episodes -- you wanted episode %u",
         series->title,
         series->total_episodes,
         x->episode_spec);
  }

  if (x->season_spec && (x->season_spec > series->total_seasons))
  {
    error ("invalid season specified");
    die ("\"%s\" has %u seasons -- you wanted season %u",
         series->title,
         series->total_seasons,
         x->season_spec);
  }

  if (x->season_spec && x->episode_spec &&
      (x->episode_spec > series->season[x->season_spec - 1].total_episodes))
  {
    error ("invalid episode specified");
    die ("season %u of \"%s\" has %u episodes -- you wanted episode %u",
         x->season_spec,
         series->title,
         series->season[x->season_spec - 1].total_episodes,
         x->episode_spec);
  }
}

static void
init_ep_options (struct ep_options *x)
{
  x->show_air_dates = false;
  x->show_descriptions = false;
  x->show_ratings = false;
  x->show_title = true;
  x->series_info = false;
  x->season_spec = 0;
  x->episode_spec = 0;
}

int
main (int argc, char **argv)
{
  int c;
  struct ep_options x;

  set_program_name (argv[0]);
  atexit (cleanup);
  init_ep_options (&x);

  for (;;)
  {
    c = getopt_long (argc, argv, "ade:irs:thv", options, NULL);
    if (c == -1)
      break;
    switch (c)
    {
      case 'e':
        x.episode_spec = (unsigned int) strtoul (optarg, (char **) NULL, 10);
        break;
      case 's':
        x.season_spec = (unsigned int) strtoul (optarg, (char **) NULL, 10);
        break;
      case 'a':
        x.show_air_dates = true;
        break;
      case 'd':
        x.show_descriptions = true;
        break;
      case 'r':
        x.show_ratings = true;
        break;
      case 't':
        x.show_title = false;
        break;
      case 'i':
        x.series_info = true;
        break;
      case 'h':
        usage (false);
      case 'v':
        version ();
      default:
        usage (true);
    }
  }

  if (x.series_info)
  {
    if (x.episode_spec)
      error ("'info' and 'episode' options are mutually exclusive");
    if (x.season_spec)
      error ("'info' and 'season' options are mutually exclusive");
    if (x.show_air_dates)
      error ("'info' and 'air' options are mutually exclusive");
    if (x.show_descriptions)
      error ("'info' and `description' options are mutually exclusive");
    if (x.show_ratings)
      error ("'info' and `rating' options are mutually exclusive");
    if (!x.show_title)
      error ("'info' and 'no-title' options are mutually exclusive");
    if (x.episode_spec || x.season_spec || x.show_air_dates ||
        x.show_descriptions || x.show_ratings || !x.show_title)
      usage (true);
  }

  if (argc <= optind)
  {
    error ("missing title");
    usage (true);
  }

  init_series ();
  set_series_title (argv + optind);
  retrieve_series ();
  verify_options_with_series (&x);
  display_series (&x);

  exit (EXIT_SUCCESS);
  return 0; /* for compiler */
}

