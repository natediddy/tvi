/*
 * ep.c
 *
 * Nathan Forbes (2014)
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdarg.h>
#ifdef HAVE_STDBOOL_H
# include <stdbool.h>
#endif
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
# define EP_VERSION "1.0.0"
#endif

#define EPISODES_URL "http://www.tv.com/shows/%s/episodes/"
#define SEASON_URL   "http://www.tv.com/shows/%s/season-%u/"
#define USERAGENT    EP_PROGRAM_NAME " (EPisode list)/" EP_VERSION

#define U_TITLE_IGNORE_CHARS ".'"

#define DEFAULT_TERM_WIDTH 40
#define MILLIS_PER_SEC     1000L
#define PROPELLER_SIZE     4

#define URL_BUFMAX 1024

#define SEASON_PATTERN        "<strong>Season %u"
#define SEASON_PATTERN_BUFMAX 256

#define EPISODE_PATTERN        "Episode %u\r\n"
#define EPISODE_PATTERN_BUFMAX 256

#define DESCRIPTION_BUFMAX 2048
#define RATING_BUFMAX      24

#define SERIES_P_TITLE_MAX 1024
#define EPISODE_TITLE_MAX  1024
#define EPISODES_MAX       1024
#define SEASONS_MAX        1024

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

#ifndef HAVE_STDBOOL_H
typedef unsigned char bool;
# define false ((bool) 0)
# define true  ((bool) 1)
#endif

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

struct page_content
{
  size_t n;
  char *buf;
};

struct episode
{
  char title[EPISODE_TITLE_MAX];
  char description[DESCRIPTION_BUFMAX];
  char rating[RATING_BUFMAX];
};

struct season
{
  unsigned int total_episodes;
  struct episode episode[EPISODES_MAX];
};

struct series
{
  unsigned int total_seasons;
  char p_title[SERIES_P_TITLE_MAX];
  char *g_title;
  char *u_title;
  struct season season[SEASONS_MAX];
};

const char *               program_name;
static bool                show_descriptions = false;
static bool                show_ratings      = false;
static unsigned int        n_episode         = 0U;
static unsigned int        n_season          = 0U;
static struct page_content pc                = {0, NULL};
static struct series *     series            = NULL;

static struct option const opts[] =
{
  {"description", no_argument, NULL, 'd'},
  {"episode", required_argument, NULL, 'e'},
  {"rating", no_argument, NULL, 'r'},
  {"season", required_argument, NULL, 's'},
  {"help", no_argument, NULL, 'h'},
  {"version", no_argument, NULL, 'v'},
  {NULL, 0, NULL, 0}
};

static const char propeller[PROPELLER_SIZE] = {'|', '/', '-', '\\'};

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

/* Return the length of S while skipping characters in EXCEPT.
   For example:
     size_t n = ep_strlen ("'foo'.'bar'", ".'");
   In this example n would be 6, not 11 like strlen() would return. */
static size_t
ep_strlen (const char *s, const char *except)
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

/* Copy SRC into DEST while skipping characters in EXCEPT.
   For example:
     char buffer[1024];
     ep_strcpy (buffer, "'foo'.'bar'", ".'");
     printf ("%s\n", buffer);
   This example would print the string "foobar".
   NOTE: this automatically adds a terminating '\0' to DEST */
static void *
ep_strcpy (char *dest, const char *src, const char *except)
{
  bool ec;
  size_t n;
  char *p;
  const char *e;
  const char *s;

  n = ep_strlen (src, except);

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

  return memcpy (dest, buf, n + 1);
}

static void *
ep_malloc (size_t n)
{
  void *p;

  p = malloc (n);
  if (!p)
  {
    debug ("malloc() failed");
    die ("internal error: %s", strerror (errno));
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
    die ("internal error: %s", strerror (errno));
  }
  return p;
}

static void
usage (bool had_error)
{
  fprintf ((had_error) ? stderr : stdout,
           "Usage: %s [-s<N>] [-e<N>] [-dr] <TITLE>\n",
           program_name);

  if (!had_error)
    fputs ("Options:\n"
           "  -e<N>, --episode=<N>  specify episode <N>\n"
           "  -s<N>, --season=<N>   specify season <N>\n"
           "  -d, --description     print description for each episode\n"
           "  -r, --rating          print rating for each episode\n"
           "  -h, --help            print this text and exit\n"
           "  -v, --version         print version information and exit\n"
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
    nu = ep_strlen (item[p], U_TITLE_IGNORE_CHARS);
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
    memcpy (series->g_title + g_pos, item[p], ng);
    ep_strcpy (series->u_title + u_pos, item[p], U_TITLE_IGNORE_CHARS);
    g_pos += ng;
    u_pos += ep_strlen (item[p], U_TITLE_IGNORE_CHARS);
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
try_setup (CURL *cp, CURLcode status, const char *url)
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
  if (!try_setup (cp, status, url))
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
parse_episodes_page (void)
{
  unsigned int x;
  char *p;

  *series->p_title = '\0';
  p = strstr (pc.buf, "<title>");
  if (p && *p)
  {
    char *s = series->p_title;
    for (p += strlen ("<title>"); (*p && (*p != '-')); ++s, ++p)
      *s = *p;
    *s = '\0';
  }

  if (!*series->p_title)
    die ("failed to find a series by the title \"%s\"", series->g_title);

  strip_trailing_space (series->p_title);
  x = 1;
  series->total_seasons = 0;

  for (;;)
  {
    char pbuf[SEASON_PATTERN_BUFMAX];
    snprintf (pbuf, SEASON_PATTERN_BUFMAX, SEASON_PATTERN, x);
    p = strstr (pc.buf, pbuf);
    if (p && *p)
    {
      series->total_seasons = x++;
      continue;
    }
    break;
  }
}

static void
parse_season_page (struct season *season)
{
  unsigned int x;
  ssize_t pos;
  char *p;
  char *q;

  x = 1;
  for (;;)
  {
    char ebuf[EPISODE_PATTERN_BUFMAX];
    snprintf (ebuf, EPISODE_PATTERN_BUFMAX, EPISODE_PATTERN, x);
    p = strstr (pc.buf, ebuf);
    if (p && *p)
    {
      season->total_episodes++;
      for (pos = p - pc.buf; (pos >= 0); --pos)
      {
        if ((pc.buf[pos] == '<') && (pc.buf[pos + 1] == '/') &&
            (pc.buf[pos + 2] == 'a') && (pc.buf[pos + 3] == '>'))
        {
          for (pos--; (pc.buf[pos] != '>'); --pos)
            ;
          char *t = season->episode[x - 1].title;
          char *s = pc.buf + (pos + 1);
          for (; (*s != '<'); ++t, ++s)
            *t = *s;
          *t = '\0';
          season->total_episodes = x;
          break;
        }
      }
      if (show_ratings)
      {
        q = strstr (p, "_rating");
        if (q && *q)
        {
          q += strlen ("_rating");
          for (; (*q != '>'); ++q)
            ;
          char *r = season->episode[x - 1].rating;
          char *s = ++q;
          for (; (*s != '<'); ++r, ++s)
            *r = *s;
          *r = '\0';
        }
        else
          *season->episode[x - 1].rating = '\0';
      }
      if (show_descriptions)
      {
        q = strstr (p, "class=\"description\">");
        if (q && *q)
        {
          q += strlen ("class=\"description\">");
          for (;;)
          {
            while (isspace (*q))
              q++;
            if (*q == '<')
            {
              for (; (*q != '>'); ++q)
                ;
              if (*q == '>')
                q++;
              continue;
            }
            break;
          }
          char *d = season->episode[x - 1].description;
          for (; (*q != '<'); ++d, ++q)
            *d = *q;
          *d = '\0';
        }
        else
          *season->episode[x - 1].description = '\0';
      }
      x++;
      continue;
    }
    break;
  }
}

static void
retrieve_series_data (void)
{
  unsigned int s;
  char e_url[URL_BUFMAX];

  snprintf (e_url, URL_BUFMAX, EPISODES_URL, series->u_title);
  if (try_connect (e_url))
  {
    parse_episodes_page ();
    for (s = 0; (s < series->total_seasons); ++s)
    {
      char s_url[URL_BUFMAX];
      snprintf (s_url, URL_BUFMAX, SEASON_URL, series->u_title, s + 1);
      series->season[s].total_episodes = 0;
      if (try_connect (s_url))
        parse_season_page (&series->season[s]);
    }
  }
}

static void
display_series_data (void)
{
  unsigned int s;
  unsigned int e;

  for (s = 0; (s < series->total_seasons); ++s)
  {
    if (!n_season || (n_season == (s + 1)))
    {
      for (e = 0; (e < series->season[s].total_episodes); ++e)
      {
        if (!n_episode || (n_episode == (e + 1)))
        {
          printf ("s%02ue%02u: %s",
                  s + 1,
                  e + 1,
                  series->season[s].episode[e].title);
          if (show_ratings)
            printf (" (%s)", series->season[s].episode[e].rating);
          fputc ('\n', stdout);
          if (show_descriptions)
            printf ("  %s\n\n", series->season[s].episode[e].description);
        }
      }
    }
  }
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
verify_options_with_series_data (void)
{
  if (n_season && (n_season > series->total_seasons))
  {
    error ("invalid season specified");
    die ("\"%s\" has %u seasons -- you wanted season %u",
         series->p_title,
         series->total_seasons,
         n_season);
  }

  if (n_season && n_episode &&
      (n_episode > series->season[n_season - 1].total_episodes))
  {
    error ("invalid episode specified");
    die ("season %u of \"%s\" has %u episodes -- you wanted episode %u",
         n_season,
         series->p_title,
         series->season[n_season - 1].total_episodes,
         n_episode);
  }
}

int
main (int argc, char **argv)
{
  int c;

  set_program_name (argv[0]);
  atexit (cleanup);

  while ((c = getopt_long (argc, argv, "de:rs:hv", opts, NULL)) != -1)
  {
    switch (c)
    {
      case 'e':
        n_episode = (unsigned int) strtoul (optarg, (char **) NULL, 10);
        break;
      case 's':
        n_season = (unsigned int) strtoul (optarg, (char **) NULL, 10);
        break;
      case 'd':
        show_descriptions = true;
        break;
      case 'r':
        show_ratings = true;
        break;
      case 'h':
        usage (false);
      case 'v':
        version ();
      default:
        usage (true);
    }
  }

  if (argc <= optind)
  {
    error ("missing title");
    usage (true);
  }

  if (!n_season && n_episode)
  {
    error ("episode option must be accompanied with season option");
    usage (true);
  }

  series = ep_new (struct series);
  set_series_title (argv + optind);
  retrieve_series_data ();
  verify_options_with_series_data ();
  display_series_data ();

  exit (EXIT_SUCCESS);
  return 0; /* for compiler */
}

