/* Provide gettimeofday for systems that don't have it or for which it's broken.

   Copyright (C) 2001-2003, 2005-2007, 2009-2018 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <https://www.gnu.org/licenses/>.  */

/* written by Jim Meyering */

#include <config.h>

/* Specification.  */
#include <sys/time.h>

#include <time.h>

#include "localtime-buffer.h"

/* This is a wrapper for gettimeofday.  It is used only on systems
   that lack this function, or whose implementation of this function
   causes problems.
   Work around the bug in some systems whereby gettimeofday clobbers
   the static buffer that localtime uses for its return value.  The
   gettimeofday function from Mac OS X 10.0.4 (i.e., Darwin 1.3.7) has
   this problem.  */

int
gettimeofday (struct timeval *restrict tv, void *restrict tz)
{
#undef gettimeofday
# if HAVE_GETTIMEOFDAY
#  if GETTIMEOFDAY_CLOBBERS_LOCALTIME
  /* Save and restore the contents of the buffer used for localtime's
     result around the call to gettimeofday.  */
  struct tm save = *localtime_buffer_addr;
#  endif

#  if defined timeval /* 'struct timeval' overridden by gnulib?  */
#   undef timeval
  struct timeval otv;
  int result = gettimeofday (&otv, (struct timezone *) tz);
  if (result == 0)
    {
      tv->tv_sec = otv.tv_sec;
      tv->tv_usec = otv.tv_usec;
    }
#  else
  int result = gettimeofday (tv, (struct timezone *) tz);
#  endif

#  if GETTIMEOFDAY_CLOBBERS_LOCALTIME
  *localtime_buffer_addr = save;
#  endif

  return result;

# else

#  if !defined OK_TO_USE_1S_CLOCK
#   error "Only 1-second nominal clock resolution found.  Is that intended?" \
          "If so, compile with the -DOK_TO_USE_1S_CLOCK option."
#  endif
  tv->tv_sec = time (NULL);
  tv->tv_usec = 0;

  return 0;

# endif
}
