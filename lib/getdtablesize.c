/* getdtablesize() function: Return maximum possible file descriptor value + 1.
   Copyright (C) 2008-2018 Free Software Foundation, Inc.
   Written by Bruno Haible <bruno@clisp.org>, 2008.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

#include <config.h>

/* Specification.  */
#include <unistd.h>

# include <limits.h>
# include <sys/resource.h>

# ifndef RLIM_SAVED_CUR
#  define RLIM_SAVED_CUR RLIM_INFINITY
# endif
# ifndef RLIM_SAVED_MAX
#  define RLIM_SAVED_MAX RLIM_INFINITY
# endif

int
getdtablesize (void)
{
  struct rlimit lim;

  if (getrlimit (RLIMIT_NOFILE, &lim) == 0
      && 0 <= lim.rlim_cur && lim.rlim_cur <= INT_MAX
      && lim.rlim_cur != RLIM_INFINITY
      && lim.rlim_cur != RLIM_SAVED_CUR
      && lim.rlim_cur != RLIM_SAVED_MAX)
    return lim.rlim_cur;

  return INT_MAX;
}
