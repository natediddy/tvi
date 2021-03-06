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

#include <float.h>
#include <getopt.h>
#include <string.h>
#include <wchar.h>

#include <curl/curl.h>

#include "tvi.h"
#include "utils.h"

#define HELP_TEXT \
  "Options:\n" \
  "  -eN, --episode=N          specify episode(s) N\n" \
  "                            For more than one episode, use a\n" \
  "                            comma-separated list (e.g. \"1,2,3\").\n" \
  "  -sN, --season=N           specify season(s) N\n" \
  "                            For more than one season, use a\n" \
  "                            comma-separated list (e.g. \"1,2,3\").\n" \
  "  -a, --air                 print the air date for each episode\n" \
  "  -cNAME, --cast=NAME       print cast and crew members\n" \
  "                            If NAME is given, and it matches a cast\n" \
  "                            member's name, their respective role is\n" \
  "                            printed. On the other hand if NAME matches\n" \
  "                            a cast member's role, their respective\n" \
  "                            name is printed. If NAME is not given, all\n" \
  "                            cast and crew members are printed.\n" \
  "  -d, --description         print description for each episode\n" \
  "  -H, --highest-rated       print highest rated episode of series\n" \
  "  -l, --last                print most recently aired episode\n" \
  "  -L, --lowest-rated        print lowest rated episode of series\n" \
  "  -n, --next                print next episode scheduled to air\n" \
  "  -N, --no-progress         do not display any progress while\n" \
  "                            downloading data (useful for writing\n" \
  "                            output to a file)\n" \
  "  -r, --rating              print rating for each episode\n" \
  "  -h, --help                print this text and exit\n" \
  "  -v, --version             print version information and exit\n" \
  "Only 1 TITLE can be provided at a time.\n" \
  "All TV series data is obtained from <" TVDOTCOM "/>.\n"

#define VERSION_TEXT \
  PROGRAM_NAME " " PROGRAM_VERSION "\n" \
  "Copyright (C) 2014 Nathan Forbes <" PACKAGE_BUGREPORT ">\n" \
  "This is free software; see the source for copying conditions.\n" \
  "There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A\n" \
  "PARTICULAR PURPOSE.\n"

#define SEARCH_URL   TVDOTCOM "/search?q=%s/"
#define EPISODES_URL TVDOTCOM "/shows/%s/episodes/"
#define CAST_URL     TVDOTCOM "/shows/%s/cast/"
#define SEASON_URL   TVDOTCOM "/shows/%s/season-%u/"

#define PROPELLER_SIZE 4

#define ATTR_0           0
#define ATTR_AIR         0x01
#define ATTR_DESCRIPTION 0x02
#define ATTR_RATING      0x04

#define SERIES_TITLE_PATTERN        "<title>"
#define SERIES_DESCRIPTION_PATTERN  "\"og:description\" content=\""
#define SERIES_TAGLINE_PATTERN      "class=\"tagline\">"
#define TAGLINE_ENDED               "ended"
#define SEASON_PATTERN              "<strong>Season %u"
#define EPISODE_PATTERN             "Episode %u\r\n"
#define EPISODE_AIR_PATTERN         "class=\"date\">"
#define EPISODE_DESCRIPTION_PATTERN "class=\"description\">"
#define EPISODE_RATING_PATTERN      "_rating"
#define SEARCH_SHOW_PATTERN         "class=\"result show\">"
#define SEARCH_HREF_PATTERN         " href=\"/shows/"
#define CAST_NAME_PATTERN           "<a itemprop=\"name\""
#define CAST_ROLE_PATTERN           "<div class=\"role\">"

#define EMPTY_DESCRIPTION "(no description)"

#define SPEC_DELIM_C ','
#define SPEC_DELIM_S ","
#define SPEC_ERROR_MESSAGE \
  "must be of the form \"N,N,N...\" (e.g. \"1,23\", \"4\", \"5,6,7\", etc.)"

#define PROGRESS_LOADING_MESSAGE "Loading... "

#define ENCODE_CHARS "!@#$%^&*()=+{}[]|\\;':\",<>/? "

#define TITLE               series.title.proper
#define SEASON(n)           series.season[(n)]
#define LAST_SEASON         SEASON (series.total_seasons - 1)
#define EPISODE(s, n)       s.episode[(n)]
#define FIRST_EPISODE_OF(s) s.episode[0]
#define LAST_EPISODE_OF(s)  s.episode[s.total_episodes - 1]
#define PERSON(n)           series.cast.person[(n)]

#define PROPELLER_ROTATE_INTERVAL 0.25f
#define propeller_rotate_interval_passed(m) \
  ((m) >= TVI_MILLIS_PER_SECOND * PROPELLER_ROTATE_INTERVAL)

#define description_indent_size(width) ((width) * 0.05)

/* these are hacky macros that facilitate creating and formatting static
   buffers for URLs and HTML patterns in a way that looks a lot cleaner {{{ */
#define __url_var(name) url_ ## name
#define url_var__(name) __url_var (name)

#define search_url(name) \
  char url_var__ (name)[TVI_BUFMAX]; \
  char *__e = encode_series_given_title (); \
  snprintf (url_var__ (name), TVI_BUFMAX, SEARCH_URL, __e); \
  tvi_free (__e)

#define episodes_url(name) \
  char url_var__ (name)[TVI_BUFMAX]; \
  snprintf (url_var__ (name), TVI_BUFMAX, EPISODES_URL, series.title.url)

#define cast_url(name) \
  char url_var__ (name)[TVI_BUFMAX]; \
  snprintf (url_var__ (name), TVI_BUFMAX, CAST_URL, series.title.url)

#define season_url(name, n) \
  char url_var__ (name)[TVI_BUFMAX]; \
  snprintf (url_var__ (name), TVI_BUFMAX, SEASON_URL, series.title.url, (n))

#define __html_pat_var(name) html_pattern_ ## name
#define html_pat_var__(name) __html_pat_var (name)

#define season_pattern(name, n) \
  char html_pat_var__ (name)[TVI_BUFMAX]; \
  snprintf (html_pat_var__ (name), TVI_BUFMAX, SEASON_PATTERN, (n))

#define episode_pattern(name, n) \
  char html_pat_var__ (name)[TVI_BUFMAX]; \
  snprintf (html_pat_var__ (name), TVI_BUFMAX, EPISODE_PATTERN, (n))
/* }}} */

struct episode
{
  bool has_aired;
  double rating;
  char air[TVI_BUFMAX];
  char title[TVI_BUFMAX];
  char *description;
};

struct season
{
  int total_episodes;
  double rating;
  struct episode episode[TVI_BUFMAX];
};

struct person
{
  size_t n_name;
  size_t n_role;
  char name[TVI_BUFMAX];
  char role[TVI_BUFMAX];
};

struct cast
{
  int total_people;
  struct person person[TVI_BUFMAX];
};

struct schedule
{
  bool ended;
  char day[TVI_BUFMAX];
  char time[TVI_BUFMAX];
  char network[TVI_BUFMAX];
};

struct title
{
  char proper[TVI_BUFMAX]; /* proper (e.g. "The Wire") */
  char url[TVI_BUFMAX];    /* for URL (e.g. "the-wire") */
  char *given;          /* from command line (e.g. "the wire") */
};

struct series
{
  int total_episodes;
  int total_seasons;
  double rating;
  struct cast cast;
  struct schedule schedule;
  struct title title;
  char air_start[TVI_BUFMAX];
  char air_end[TVI_BUFMAX];
  struct season season[TVI_BUFMAX];
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
  int v[TVI_BUFMAX];
};

struct token
{
  size_t n;
  char str[TVI_BUFMAX];
};

struct query
{
  int total_tokens;
  struct token token[TVI_BUFMAX];
};

struct tvi_options
{
  bool cast;
  bool highest_rated;
  bool info;
  bool last;
  bool lowest_rated;
  bool next;
  bool show_progress;
  char attrs;
  char cast_pattern[TVI_BUFMAX];
  struct spec e;
  struct spec s;
};

const char *program_name;

static struct series series;
static struct page_content page = {0, NULL};

static size_t n_series_title_pattern = 0;
static size_t n_series_description_pattern  = 0;
static size_t n_series_tagline_pattern = 0;
static size_t n_tagline_ended = 0;
static size_t n_episode_air_pattern = 0;
static size_t n_episode_description_pattern = 0;
static size_t n_episode_rating_pattern = 0;
static size_t n_search_show_pattern = 0;
static size_t n_search_href_pattern = 0;
static size_t n_cast_name_pattern = 0;
static size_t n_cast_role_pattern = 0;

static struct option const options[] =
{
  {"air", no_argument, NULL, 'a'},
  {"cast", optional_argument, NULL, 'c'},
  {"desc", no_argument, NULL, 'd'},
  {"episode", required_argument, NULL, 'e'},
  {"help", no_argument, NULL, 'h'},
  {"highest-rated", no_argument, NULL, 'H'},
  {"info", no_argument, NULL, 'i'},
  {"last", no_argument, NULL, 'l'},
  {"lowest-rated", no_argument, NULL, 'L'},
  {"next", no_argument, NULL, 'n'},
  {"no-progress", no_argument, NULL, 'N'},
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

static void
usage (bool had_error)
{
  fprintf ((!had_error) ? stdout : stderr,
           "Usage: %s [-adHilLnNr] [-c[NAME]] [-sN[,N,...]] [-eN[,N,...]] "
             "TITLE\n",
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
set_pattern_sizes (void)
{
  n_series_title_pattern = strlen (SERIES_TITLE_PATTERN);
  n_series_description_pattern = strlen (SERIES_DESCRIPTION_PATTERN);
  n_series_tagline_pattern = strlen (SERIES_TAGLINE_PATTERN);
  n_tagline_ended = strlen (TAGLINE_ENDED);
  n_episode_air_pattern = strlen (EPISODE_AIR_PATTERN);
  n_episode_description_pattern = strlen (EPISODE_DESCRIPTION_PATTERN);
  n_episode_rating_pattern = strlen (EPISODE_RATING_PATTERN);
  n_search_show_pattern = strlen (SEARCH_SHOW_PATTERN);
  n_search_href_pattern = strlen (SEARCH_HREF_PATTERN);
  n_cast_name_pattern = strlen (CAST_NAME_PATTERN);
  n_cast_role_pattern = strlen (CAST_ROLE_PATTERN);
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
  series.title.given = tvi_newa (char, n + 1);

  for (p = 0, t = series.title.given; item[p]; ++p)
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

  ng = strlen (series.title.given);
  n = ng;

  for (s = series.title.given; *s; ++s)
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
    return tvi_strdup (series.title.given, n);

  bool encoded_char;
  char *encoded = tvi_newa (char, n + 1);
  char *e = encoded;

  for (s = series.title.given; *s; ++s, ++e)
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

static size_t
page_write_cb (void *buf, size_t size, size_t nmemb, void *data)
{
  size_t n;
  struct page_content *p;

  p = (struct page_content *) data;
  n = size * nmemb;

  p->buffer = tvi_renewa (char, p->buffer, p->n + n + 1);
  memcpy (p->buffer + p->n, buf, n);
  p->n += n;
  p->buffer[p->n] = '\0';
  return n;
}

static int
progress_cb (void *data, double dt, double dc, double ut, double uc)
{
  const struct tvi_options *x = (const struct tvi_options *) data;

  if (!x->show_progress)
    return 0;

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
    tvi_gettimeofday (&now);
    millis = tvi_get_millis (last, now);
  }

  if (millis == -1 || propeller_rotate_interval_passed (millis)) {
    space_remaining = tvi_console_width ();
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
      tvi_gettimeofday (&last);
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

  w = tvi_console_width ();
  for (i = 0; i < w; ++i)
    fputc (' ', stdout);
  fputc ('\r', stdout);
}

static void
check_curl_status (CURL *cp, CURLcode status)
{
  if (status != CURLE_OK)
  {
    tvi_error (0, "libcurl error: %s", curl_easy_strerror (status));
    curl_easy_cleanup (cp);
    exit (E_INTERNET);
  }
}

static bool
try_connect (const char *url, const struct tvi_options *x)
{
  CURL *cp = curl_easy_init ();

  if (!cp)
  {
    tvi_error (0, "failed to initialize libcurl: %s",
               curl_easy_strerror (CURLE_FAILED_INIT));
    return false;
  }

  tvi_debug ("connecting to \"%s\"...", url);
  page.n = 0;
  page.buffer = tvi_newa (char, 1);

#define __setopt(o, p) check_curl_status (cp, curl_easy_setopt (cp, o, p))
  __setopt (CURLOPT_URL, url);
  __setopt (CURLOPT_USERAGENT, USERAGENT);
  __setopt (CURLOPT_FAILONERROR, 1L);
  __setopt (CURLOPT_FOLLOWLOCATION, 1L);
  __setopt (CURLOPT_WRITEDATA, &page);
  __setopt (CURLOPT_WRITEFUNCTION, &page_write_cb);
  __setopt (CURLOPT_NOPROGRESS, 0L);
  __setopt (CURLOPT_PROGRESSDATA, x);
  __setopt (CURLOPT_PROGRESSFUNCTION, &progress_cb);
#undef __setopt

  bool result = true;

  CURLcode status = curl_easy_perform (cp);
  progress_finish ();

  if (status != CURLE_OK)
  {
    long res = 0L;
    if (curl_easy_getinfo (cp, CURLINFO_RESPONSE_CODE, &res) == CURLE_OK)
      tvi_error (0, "%s (http response=%li)",
                 curl_easy_strerror (status), res);
    else
      tvi_error (0, curl_easy_strerror (status));
    result = false;
  }

  curl_easy_cleanup (cp);
  return result;
}

static const struct
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
    if (tvi_strncasecmp (s, entity_ref[i].s1, entity_ref[i].n1) == 0 ||
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
    if (tvi_strncasecmp (*s, entity_ref[i].s1, entity_ref[i].n1) == 0)
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
set_url_title_best_guess (void)
{
  char *g;
  char *u;

  for (g = series.title.given, u = series.title.url; *g; ++g, ++u)
  {
    if (*g == '\'' || *g == ':' || *g == '.' || *g == ' ')
    {
      if (*g == ' ')
        *u = '-';
      continue;
    }
    *u = *g;
  }
  *u = '\0';

  tvi_debug ("guessed URL title for \"%s\": \"%s\"",
             series.title.given, series.title.url);
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
      h += n_search_href_pattern;
      for (u = series.title.url; *h != '/'; ++h, ++u)
        *u = *h;
      *u = '\0';
    }
  }

  if (!*series.title.url)
  {
    tvi_debug ("failed to parse title for URL; guessing...");
    set_url_title_best_guess ();
  }
}

static void
parse_series_proper_title (void)
{
  char *p;
  char *t;

  series.title.proper[0] = '\0';
  p = strstr (page.buffer, SERIES_TITLE_PATTERN);

  if (p && *p)
  {
    p += n_series_title_pattern;
    for (t = series.title.proper; *p && *p != '-'; ++p, ++t)
      *t = *p;
    *t = '\0';
  }

  if (!*series.title.proper)
    tvi_debug ("failed to parse proper title");
  tvi_strip_trailing_space (series.title.proper);
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
    p += n_series_description_pattern;
    for (; *p && *p != '"'; ++p, ++n)
      ;
  }

  if (n == 0)
  {
    series.description = tvi_strdup (EMPTY_DESCRIPTION, -1);
    return;
  }

  series.description = tvi_newa (char, n + 1);
  p = strstr (page.buffer, SERIES_DESCRIPTION_PATTERN);

  if (p && *p)
  {
    p += n_series_description_pattern;
    for (d = series.description; *p && *p != '"'; ++d, ++p)
    {
      while (is_entity_ref (p))
        *d++ = entity_ref_char (&p);
      *d = *p;
    }
    *d = '\0';
  }

  if (!series.description || !*series.description)
    tvi_debug ("failed to parse series description");
}

static void
parse_series_schedule (void)
{
  char *e;
  char *p;
  char *q;
  char tagline[TVI_BUFMAX];
  struct schedule *s = &series.schedule;

  p = strstr (page.buffer, SERIES_TAGLINE_PATTERN);
  if (p && *p)
  {
    p += n_series_tagline_pattern;
    for (q = tagline; *p && *p != '<'; ++q, ++p)
      *q = *p;
    *q = '\0';
    e = strstr (tagline, TAGLINE_ENDED);
    if (e && *e)
    {
      /* tagline will be of the form:
           "NETWORK (ended YEAR)"
         example: "AMC (ended 2013)" */
      s->ended = true;
      for (p = tagline, q = s->network; *p != ' '; ++q, ++p)
        *q = *p;
      *q = '\0';
      e += n_tagline_ended;
      while (*e == ' ')
        e++;
      for (q = s->time; *e != ')'; ++q, ++e)
        *q = *e;
      *q = '\0';
    }
    else
    {
      /* tagline will be of the form:
           "DAY TIME on NETWORK "
         example: "Sunday 9:00 PM on HBO " */
      for (p = tagline, q = s->day; *p != ' '; ++q, ++p)
        *q = *p;
      *q = '\0';
      while (*p == ' ')
        p++;
      for (q = s->time; *p != ' '; ++q, ++p)
        *q = *p;
      *q = ' ';
      while (*p == ' ')
        p++;
      for (q++; *p != ' '; ++q, ++p)
        *q = *p;
      *q = '\0';
      while (*p == ' ')
        p++;
      if (*p == 'o' && *(p + 1) == 'n')
        p += 2;
      while (*p == ' ')
        p++;
      for (q = s->network; *p && *p != ' '; ++q, ++p)
        *q = *p;
      *q = '\0';
    }
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
  parse_series_schedule ();

  for (i = 1;; ++i)
  {
    season_pattern (s, i);
    p = strstr (page.buffer, html_pattern_s);
    if (!p)
      break;
    series.total_seasons++;
  }
}

static void
init_series (void)
{
  series.total_episodes = 0;
  series.total_seasons = 0;

  series.rating = -1.0f;

  series.schedule.ended = false;
  series.schedule.day[0] = '\0';
  series.schedule.time[0] = '\0';
  series.schedule.network[0] = '\0';

  series.air_start[0] = '\0';
  series.air_end[0] = '\0';

  series.title.proper[0] = '\0';
  series.title.url[0] = '\0';
  series.title.given = NULL;

  series.cast.total_people = 0;

  series.description = NULL;
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
    p += n_episode_air_pattern;
    for (a = episode->air; *p != '<'; ++a, ++p)
      *a = *p;
    *a = '\0';
  }
}

static void
parse_episode_rating (struct episode *episode, char **secp)
{
  char buffer[TVI_BUFMAX];
  char *r;
  char *p;

  buffer[0] = '\0';
  p = strstr (*secp, EPISODE_RATING_PATTERN);

  if (p && *p)
  {
    p += n_episode_rating_pattern;
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
    p += n_episode_description_pattern;
    if (*p == '<' && *(p + 1) == '/')
    {
      episode->description = tvi_strdup (EMPTY_DESCRIPTION, -1);
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

  episode->description = tvi_newa (char, n + 1);
  p = strstr (*secp, EPISODE_DESCRIPTION_PATTERN);

  if (p && *p)
  {
    p += n_episode_description_pattern;
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
    tvi_debug ("failed to parse episode description (\"%s\")",
               episode->title);
}

static void
set_episode_has_aired (struct episode *episode)
{
  size_t n;
  time_t a;
  char buffer[TVI_BUFMAX];
  struct tm tm;
  char *t;

  n = strlen (episode->air);
  memcpy (buffer, episode->air, n + 1);
  t = series.schedule.time;
  if (*t && strchr (t, ':'))
    memcpy (buffer + n, t, strlen (t) + 1);

  memset (&tm, 0, sizeof (struct tm));
  strptime (buffer, "%m/%d/%y %I:%M %p", &tm);

  a = mktime (&tm);
  if (a == -1)
  {
    tvi_error (0, "failed to get time value from air date/time \"%s\"",
               buffer);
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
  for (i = 0; i < TVI_BUFMAX; ++i)
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
init_person (struct person *person)
{
  person->n_name = 0;
  person->n_role = 0;
  person->name[0] = '\0';
  person->role[0] = '\0';
}

static void
parse_cast_page (void)
{
  int i;
  char *n;
  char *p;
  char *r;

  r = NULL;
  p = page.buffer;
  for (i = 0, n = strstr (p, CAST_NAME_PATTERN);
       i < TVI_BUFMAX && n && *n;
       ++i, n = strstr (p, CAST_NAME_PATTERN))
  {
    series.cast.total_people++;
    init_person (&PERSON (i));
    for (n += n_cast_name_pattern; *n != '>'; ++n)
      ;
    if (*n == '>')
      n++;
    for (p = PERSON (i).name; *n != '<'; ++p, ++n)
    {
      while (is_entity_ref (n))
        *p++ = entity_ref_char (&n);
      *p = *n;
    }
    *p = '\0';
    PERSON (i).n_name = strlen (PERSON (i).name);
    r = strstr (n, CAST_ROLE_PATTERN);
    if (r && *r)
    {
      r += n_cast_role_pattern;
      for (p = PERSON (i).role; *r != '<'; ++r, ++p)
      {
        while (is_entity_ref (r))
          *p++ = entity_ref_char (&r);
        *p = *r;
      }
      *p = '\0';
      PERSON (i).n_role = strlen (PERSON (i).role);
      p = r;
    }
  }
}

static void
set_series_start_end_airs (void)
{
  memcpy (series.air_start,
          EPISODE (SEASON (0), 0).air,
          strlen (EPISODE (SEASON (0), 0).air) + 1);

  memcpy (series.air_end,
          LAST_EPISODE_OF (LAST_SEASON).air,
          strlen (LAST_EPISODE_OF (LAST_SEASON).air) + 1);
}

static void
set_series_total_episodes (void)
{
  int s;

  for (s = 0; s < series.total_seasons; ++s)
    series.total_episodes += SEASON (s).total_episodes;
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

  for (s = 0; s < series.total_seasons; ++s)
  {
    if (SEASON (s).rating >= 0.0f)
    {
      total++;
      x += SEASON (s).rating;
    }
  }
  series.rating = x / total;
}

static void
retrieve_series (const struct tvi_options *x)
{
  search_url (search);

  if (!try_connect (url_search, x))
    tvi_die (E_INTERNET, "failed to connect to \"%s\"", url_search);

  parse_search_page ();

  episodes_url (episodes);
  if (!try_connect (url_episodes, x))
    tvi_die (E_INTERNET, "failed to connect to \"%s\"", url_episodes);

  parse_episodes_page ();

  if (x->cast)
  {
    cast_url (cast);
    if (!try_connect (url_cast, x))
      tvi_die (E_INTERNET, "failed to connect to \"%s\"", url_cast);
    parse_cast_page ();
    return;
  }

  int i;

  for (i = 0; i < series.total_seasons; ++i)
  {
    season_url (season, i + 1);
    if (!try_connect (url_season, x))
      tvi_die (E_INTERNET, "failed to connect to \"%s\"", url_season);
    parse_season_page (&SEASON (i));
  }

  set_series_start_end_airs ();
  set_series_total_episodes ();
  set_series_rating ();
}

static void
display_description (const char *desc)
{
  int i;
  int n;
  int s;
  int indent_size;
  int stop;
  int width;
  size_t n_word;
  char *w;
  const char *p;

  width = tvi_console_width ();
  indent_size = description_indent_size (width);
  stop = width - indent_size;

#define __put_c(__c) \
  do \
  { \
    fputc (__c, stdout); \
    n++; \
  } while (0)

#define __put_w(__w) \
  do \
  { \
    for (w = __w; *w; ++w) \
      __put_c (*w); \
  } while (0)

#define __indent(__n) \
  do \
  { \
    n = 0; \
    s = indent_size * (__n); \
    for (i = 0; i < s; ++i) \
      __put_c (' '); \
  } while (0)

  __indent (2);
  for (p = desc; *p;)
  {
    while (isspace (*p))
      p++;
    char word[TVI_BUFMAX];
    for (w = word; *p && !isspace (*p); ++p, ++w)
      *w = *p;
    *w = '\0';
    n_word = strlen (word);
    if (n + n_word >= stop)
    {
      fputc ('\n', stdout);
      __indent (1);
    }
    __put_w (word);
    __put_c (*p);
  }
  fputc ('\n', stdout);

#undef __put_c
#undef __put_w
#undef __indent
}

static void
display_episode (int s, int e, const struct tvi_options *x)
{
  struct episode *episode = &EPISODE (SEASON (s), e);

  printf ("Season %i Episode %i: %s\n", s + 1, e + 1, episode->title);

  if (x->attrs & ATTR_RATING)
  {
    fputs ("  Rating:      ", stdout);
    if (episode->has_aired)
      printf ("%.1f", episode->rating);
    else
      fputs ("not rated", stdout);
    fputc ('\n', stdout);
  }

  if (x->attrs & ATTR_AIR)
  {
    printf ("  Air Date:    %s", episode->air);
    if (!episode->has_aired)
      fputs (" (not yet aired)", stdout);
    fputc ('\n', stdout);
  }

  if (x->attrs & ATTR_DESCRIPTION)
  {
    fputs ("  Description:", stdout);
    if (strcmp (episode->description, EMPTY_DESCRIPTION) == 0)
      printf (" %s", episode->description);
    else
    {
      fputc ('\n', stdout);
      display_description (episode->description);
    }
    fputc ('\n', stdout);
  }
}

static void
init_query (struct query *query, const char *s)
{
  int pos;
  char *t;

  query->total_tokens = 0;
  for (pos = 0; *s; ++pos, ++query->total_tokens)
  {
    while (*s == ' ')
      s++;
    for (t = query->token[pos].str; *s && *s != ' '; ++t, ++s)
      *t = tolower (*s);
    *t = '\0';
    query->token[pos].n = strlen (query->token[pos].str);
  }
}

static bool
person_compare (const struct person *person, const struct query *query)
{
  int i;

  for (i = 0; i < query->total_tokens; ++i)
    if (tvi_strcasestr (person->name, query->token[i].str) ||
        tvi_strcasestr (person->role, query->token[i].str))
      return true;
  return false;
}

static int
attributes_set (char attrs)
{
  int n;

  n = 0;
  if (attrs & ATTR_AIR)
    n++;
  if (attrs & ATTR_RATING)
    n++;
  if (attrs & ATTR_DESCRIPTION)
    n++;
  return n;
}

static void
display_cast_and_crew (const char *pattern)
{
  size_t i;
  size_t n;
  size_t longest;
  ssize_t offset;
  struct query query;

  if (pattern)
    init_query (&query, pattern);

  longest = 0;
  for (i = 0; i < series.cast.total_people; ++i)
  {
    if (pattern && !person_compare (&PERSON (i), &query))
      continue;
    if (PERSON (i).n_name > longest)
      longest = PERSON (i).n_name;
  }

  printf ("%s cast and crew (", TITLE);
  if (pattern)
    printf ("matching \"%s\"", pattern);
  else
    fputs ("all", stdout);
  fputs ("):\n", stdout);

#define __print_line(__s1, __n, __s2) \
  do \
  { \
    fputs ("  ", stdout); \
    fputs (__s1, stdout); \
    fputs ("   ", stdout); \
    for (offset = longest - __n; offset >= 0; --offset) \
      fputc (' ', stdout); \
    fputs (__s2, stdout); \
    fputc ('\n', stdout); \
  } while (0)

  __print_line ("Name", 4, "Role");
  __print_line ("----", 4, "----");
  for (i = 0; i < series.cast.total_people; ++i)
  {
    if (pattern && !person_compare (&PERSON (i), &query))
      continue;
    __print_line (PERSON (i).name, PERSON (i).n_name, PERSON (i).role);
  }

#undef __print_line
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
            TITLE, series.total_seasons, series.total_episodes,
            series.air_start, series.air_end);
    if (series.schedule.ended)
      printf ("Ended in %s on %s\n",
              series.schedule.time, series.schedule.network);
    else
      printf ("Airs %ss at %s on %s\n",
              series.schedule.day,
              series.schedule.time,
              series.schedule.network);
    for (s = 0; s < series.total_seasons; ++s)
      printf ("Season %i rating: %.1f\n", s + 1, SEASON (s).rating);
    printf ("Series overall rating: %.1f\n", series.rating);
    display_description (series.description);
    return;
  }

  if (x->cast)
  {
    display_cast_and_crew ((!*x->cast_pattern) ? NULL : x->cast_pattern);
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

  if (x->attrs != ATTR_0 && x->e.n == 0)
  {
    int n_attrs = attributes_set (x->attrs);
    if (x->s.n == 0)
    {
      if (n_attrs > 1)
        printf ("%s:\n", TITLE);
      if (x->attrs & ATTR_AIR)
        printf ("%s%s - %s\n",
                (n_attrs > 1) ? "  Air dates:   " : "",
                series.air_start,
                series.air_end);
      if (x->attrs & ATTR_RATING)
        printf ("%s%.1f\n",
                (n_attrs > 1) ? "  Rating:      " : "", series.rating);
      if (x->attrs & ATTR_DESCRIPTION)
      {
        printf ("%s", (n_attrs > 1) ? "  Description:" : "");
        if (strcmp (series.description, EMPTY_DESCRIPTION) == 0)
          printf ("%s%s\n", (n_attrs > 1) ? " " : "", series.description);
        else
        {
          if (n_attrs > 1)
            fputc ('\n', stdout);
          display_description (series.description);
        }
      }
    }
    else
    {
      for (i = 0; i < x->s.n; ++i)
      {
        if (x->s.n > 1)
          printf ("Season %i:\n", x->s.v[i]);
        if (x->attrs & ATTR_AIR)
          printf ("%s%s%s - %s\n",
                  (x->s.n > 1) ? "  " : "",
                  (n_attrs > 1) ? "Air dates:   " : "",
                  FIRST_EPISODE_OF (SEASON (x->s.v[i] - 1)).air,
                  LAST_EPISODE_OF (SEASON (x->s.v[i] - 1)).air);
        if (x->attrs & ATTR_RATING)
          printf ("%s%s%.1f\n",
                  (x->s.n > 1) ? "  " : "",
                  (n_attrs > 1) ? "Rating:      " : "",
                  SEASON (x->s.v[i] - 1).rating);
        if (x->attrs & ATTR_DESCRIPTION)
          printf ("%s%s(no description for seasons)\n",
                  (x->s.n > 1) ? "  " : "",
                  (n_attrs > 1) ? "Description: " : "");
      }
    }
    return;
  }

  /* seasons specified: no
     episodes specified: no */
  if (x->s.n == 0 && x->e.n == 0)
  {
    for (s = 0; s < series.total_seasons; ++s)
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
      for (s = 0, j = 0; s < series.total_seasons; ++s)
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
  if (x->cast)
  {
    if (x->attrs & ATTR_AIR)
      tvi_error (0, "options --cast and --air are mutually exclusive");
    if (x->attrs & ATTR_DESCRIPTION)
      tvi_error (0, "options --cast and --desc are mutually exclusive");
    if (x->attrs & ATTR_RATING)
      tvi_error (0, "options --cast and --rating are mutually exclusive");
    if (x->info)
      tvi_error (0, "options --cast and --info are mutually exclusive");
    if (x->last)
      tvi_error (0, "options --cast and --last are mutually exclusive");
    if (x->highest_rated)
      tvi_error (0, "options --cast and --highest-rated are mutually "
                    "exclusive");
    if (x->lowest_rated)
      tvi_error (0, "options --cast and --lowest-rated are mutually "
                    "exclusive");
    if (x->next)
      tvi_error (0, "options --cast and --next are mutually exclusive");
    if (x->s.n > 0)
      tvi_error (0, "options --cast and --season are mutually exclusive");
    if (x->e.n > 0)
      tvi_error (0, "options --cast and --episode are mutually exclusive");
    if (x->attrs & ATTR_AIR ||
        x->attrs & ATTR_DESCRIPTION ||
        x->attrs & ATTR_RATING ||
        x->info ||
        x->last ||
        x->highest_rated ||
        x->lowest_rated ||
        x->next ||
        x->s.n > 0 ||
        x->e.n > 0)
      usage (true);
  }

  if (x->highest_rated)
  {
    if (x->info)
      tvi_error (0, "options --highest-rated and --info are mutually "
                    "exclusive");
    if (x->last)
      tvi_error (0, "options --highest-rated and --last are mutually "
                    "exclusive");
    if (x->lowest_rated)
      tvi_error (0, "options --highest-rated and --lowest-rated are mutually "
                    "exclusive");
    if (x->next)
      tvi_error (0, "options --highest-rated and --next are mutually "
                    "exclusive");
    if (x->s.n > 0)
      tvi_error (0, "options --highest-rated and --season are mutually "
                    "exclusive");
    if (x->e.n > 0)
      tvi_error (0, "options --highest-rated and --episode are mutually "
                    "exclusive");
    if (x->info ||
        x->last ||
        x->lowest_rated ||
        x->next ||
        x->s.n > 0 ||
        x->e.n > 0)
      usage (true);
  }

  if (x->lowest_rated)
  {
    if (x->info)
      tvi_error (0, "options --lowest-rated and --info are mutually "
                    "exclusive");
    if (x->last)
      tvi_error (0, "options --lowest-rated and --last are mutually "
                    "exclusive");
    if (x->next)
      tvi_error (0, "options --lowest-rated and --next are mutually "
                    "exclusive");
    if (x->s.n > 0)
      tvi_error (0, "options --lowest-rated and --season are mutually "
                    "exclusive");
    if (x->e.n > 0)
      tvi_error (0, "options --lowest-rated and --episode are mutually "
                    "exclusive");
    if (x->info || x->last || x->next || x->s.n > 0 || x->e.n > 0)
      usage (true);
  }

  if (x->info)
  {
    if (x->last)
      tvi_error (0, "options --info and --last are mutually exclusive");
    if (x->next)
      tvi_error (0, "options --info and --next are mutually exclusive");
    if (x->s.n > 0)
      tvi_error (0, "options --info and --season are mutually exclusive");
    if (x->e.n > 0)
      tvi_error (0, "options --info and --episode are mutually exclusive");
    if (x->last || x->next || x->s.n > 0 || x->e.n > 0)
      usage (true);
  }

  if (x->last)
  {
    if (x->next)
      tvi_error (0, "options --last and --next are mutually exclusive");
    if (x->s.n > 0)
      tvi_error (0, "options --last and --season are mutually exclusive");
    if (x->e.n > 0)
      tvi_error (0, "options --last and --episode are mutually exclusive");
    if (x->next || x->s.n > 0 || x->e.n > 0)
      usage (true);
  }

  if (x->next)
  {
    if (x->s.n > 0)
      tvi_error (0, "options --next and --season are mutually exclusive");
    if (x->e.n > 0)
      tvi_error (0, "options --next and --episode are mutually exclusive");
    if (x->s.n > 0 || x->e.n > 0)
      usage (true);
  }
}

static void
find_last_to_air_episode (int *season_no, int *episode_no)
{
  int e;
  int s;

  if (series.total_seasons == 0)
  {
    *season_no = -1;
    *episode_no = -1;
    return;
  }

  if (LAST_EPISODE_OF (LAST_SEASON).has_aired)
  {
    *season_no = series.total_seasons - 1;
    *episode_no = SEASON (series.total_seasons - 1).total_episodes - 1;
    return;
  }

  for (s = 0; s < series.total_seasons; ++s)
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
      (s == series.total_seasons - 1 &&
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
    if (s < series.total_seasons)
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
  for (s = 0; s < series.total_seasons; ++s)
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
  for (s = 0; s < series.total_seasons; ++s)
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
  for (s = 0; s < series.total_seasons; ++s)
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

  for (s = 0; s < series.total_seasons; ++s)
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
    int ea[TVI_BUFMAX];
    int sa[TVI_BUFMAX];
    if (x->highest_rated)
      find_highest_rated_episode (sa, ea);
    else
      find_lowest_rated_episode (sa, ea);
    for (s = 0; sa[s] != -1; ++s)
      spec_append (&x->s, sa[s] + 1);
    for (e = 0; ea[e] != -1; ++e)
      spec_append (&x->e, ea[e] + 1);
    x->attrs |= ATTR_AIR | ATTR_DESCRIPTION | ATTR_RATING;
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
    x->attrs |= ATTR_AIR | ATTR_DESCRIPTION;
    if (x->last)
      x->attrs |= ATTR_RATING;
    return;
  }

  if (x->s.n == 0 && x->e.n > 0)
  {
    had_error = false;
    for (e = 0; e < x->e.n; ++e)
    {
      if (x->e.v[e] <= 0 || x->e.v[e] > series.total_episodes)
      {
        tvi_error (0, "invalid episode specified -- %i", x->e.v[e]);
        had_error = true;
      }
    }
    if (had_error)
    {
      tvi_error (0, "\"%s\" has a total of %i episodes",
                 TITLE, series.total_episodes);
      tvi_die (E_OPTION, "specify a value between 1-%i", series.total_episodes);
    }
  }

  if (x->s.n > 0)
  {
    had_error = false;
    for (s = 0; s < x->s.n; ++s)
    {
      if (x->s.v[s] <= 0 || x->s.v[s] > series.total_seasons)
      {
        tvi_error (0, "invalid season specified -- %i", x->s.v[s]);
        had_error = true;
      }
    }
    if (had_error)
    {
      tvi_error (0, "\"%s\" has a total of %i seasons",
                 TITLE, series.total_seasons);
      tvi_die (E_OPTION, "specify a value between 1-%i", series.total_seasons);
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
          tvi_error (0, "invalid episode specified for season %i -- %i",
                     x->s.v[s], x->e.v[e]);
          had_season_episode_error = true;
        }
      }
      if (had_season_episode_error)
      {
        tvi_error (0, "season %i of \"%s\" has a total of %i episodes",
                   x->s.v[s], TITLE, SEASON (x->s.v[s] - 1).total_episodes);
        tvi_error (0, "specify value(s) between 1-%i",
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
  x->cast = false;
  x->cast_pattern[0] = '\0';
  x->highest_rated = false;
  x->info = false;
  x->last = false;
  x->lowest_rated = false;
  x->next = false;
  x->show_progress = true;
  x->attrs = ATTR_0;
  x->e.n = 0;
  x->s.n = 0;
}

static void
cleanup (void)
{
  int e;
  int s;

  tvi_free (page.buffer);
  tvi_free (series.title.given);
  tvi_free (series.description);

  for (s = 0; s < series.total_seasons; ++s)
    for (e = 0; e < SEASON (s).total_episodes; ++e)
      tvi_free (EPISODE (SEASON (s), e).description);
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
    c = getopt_long (argc, argv, "ac::de:hHlLnNirs:v", options, NULL);
    if (c == -1)
      break;
    switch (c)
    {
      case 'a':
        x.attrs |= ATTR_AIR;
        break;
      case 'c':
        x.cast = true;
        if (optarg)
          memcpy (x.cast_pattern, optarg, strlen (optarg) + 1);
        break;
      case 'd':
        x.attrs |= ATTR_DESCRIPTION;
        break;
      case 'e':
        if (!spec_parse_from_optarg (&x.e, optarg))
        {
          tvi_error (0, "invalid episode argument -- `%s'", optarg);
          tvi_die (E_OPTION, SPEC_ERROR_MESSAGE);
        }
        break;
      case 'h':
        usage (false);
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
      case 'N':
        x.show_progress = false;
        break;
      case 'r':
        x.attrs |= ATTR_RATING;
        break;
      case 's':
        if (!spec_parse_from_optarg (&x.s, optarg))
        {
          tvi_error (0, "invalid season argument -- `%s'", optarg);
          tvi_die (E_OPTION, SPEC_ERROR_MESSAGE);
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
    tvi_error (0, "missing TV series title");
    usage (true);
  }

  verify_options (&x);
  init_series ();
  set_series_given_title (argv + optind);
  set_pattern_sizes ();
  retrieve_series (&x);
  verify_options_with_series (&x);
  display_series (&x);
  exit (E_OKAY);
}

