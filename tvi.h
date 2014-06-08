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

#ifndef __TVI_H__
#define __TVI_H__

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef PACKAGE_NAME
# define PROGRAM_NAME PACKAGE_NAME
#else
# define PROGRAM_NAME "tvi"
#endif

#ifdef PACKAGE_VERSION
# define PROGRAM_VERSION PACKAGE_VERSION
#else
# define PROGRAM_VERSION "3.4.2"
#endif

#define USERAGENT PROGRAM_NAME "(TV Info)/" PROGRAM_VERSION

#define TVDOTCOM "http://www.tv.com"

typedef unsigned char bool;
#define false ((bool) 0)
#define true  ((bool) 1)

/* exit status codes */
enum
{
  E_OKAY,     /* everything went fine */
  E_OPTION,   /* there was an error with a command line option */
  E_INTERNET, /* there was an error with the internet (libcurl) */
  E_SYSTEM    /* there was a serious system error */
};

#endif /* __TVI_H__ */

