/* A POSIX-like <errno.h>.

   Copyright (C) 2008-2018 Free Software Foundation, Inc.

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

#ifndef _@GUARD_PREFIX@_ERRNO_H

#if __GNUC__ >= 3
@PRAGMA_SYSTEM_HEADER@
#endif
@PRAGMA_COLUMNS@

/* The include_next requires a split double-inclusion guard.  */
#@INCLUDE_NEXT@ @NEXT_ERRNO_H@

#ifndef _@GUARD_PREFIX@_ERRNO_H
#define _@GUARD_PREFIX@_ERRNO_H


/* On OSF/1 5.1, when _XOPEN_SOURCE_EXTENDED is not defined, the macros
   EMULTIHOP, ENOLINK, EOVERFLOW are not defined.  */
# if @EMULTIHOP_HIDDEN@
#  define EMULTIHOP @EMULTIHOP_VALUE@
#  define GNULIB_defined_EMULTIHOP 1
# endif
# if @ENOLINK_HIDDEN@
#  define ENOLINK   @ENOLINK_VALUE@
#  define GNULIB_defined_ENOLINK 1
# endif
# if @EOVERFLOW_HIDDEN@
#  define EOVERFLOW @EOVERFLOW_VALUE@
#  define GNULIB_defined_EOVERFLOW 1
# endif


/* On OpenBSD 4.0 and on native Windows, the macros ENOMSG, EIDRM, ENOLINK,
   EPROTO, EMULTIHOP, EBADMSG, EOVERFLOW, ENOTSUP, ECANCELED are not defined.
   Likewise, on NonStop Kernel, EDQUOT is not defined.
   Define them here.  Values >= 2000 seem safe to use: Solaris ESTALE = 151,
   HP-UX EWOULDBLOCK = 246, IRIX EDQUOT = 1133.

   Note: When one of these systems defines some of these macros some day,
   binaries will have to be recompiled so that they recognizes the new
   errno values from the system.  */

# ifndef ENOMSG
#  define ENOMSG    2000
#  define GNULIB_defined_ENOMSG 1
# endif

# ifndef EIDRM
#  define EIDRM     2001
#  define GNULIB_defined_EIDRM 1
# endif

# ifndef ENOLINK
#  define ENOLINK   2002
#  define GNULIB_defined_ENOLINK 1
# endif

# ifndef EPROTO
#  define EPROTO    2003
#  define GNULIB_defined_EPROTO 1
# endif

# ifndef EMULTIHOP
#  define EMULTIHOP 2004
#  define GNULIB_defined_EMULTIHOP 1
# endif

# ifndef EBADMSG
#  define EBADMSG   2005
#  define GNULIB_defined_EBADMSG 1
# endif

# ifndef EOVERFLOW
#  define EOVERFLOW 2006
#  define GNULIB_defined_EOVERFLOW 1
# endif

# ifndef ENOTSUP
#  define ENOTSUP   2007
#  define GNULIB_defined_ENOTSUP 1
# endif

# ifndef ENETRESET
#  define ENETRESET 2011
#  define GNULIB_defined_ENETRESET 1
# endif

# ifndef ECONNABORTED
#  define ECONNABORTED 2012
#  define GNULIB_defined_ECONNABORTED 1
# endif

# ifndef ESTALE
#  define ESTALE    2009
#  define GNULIB_defined_ESTALE 1
# endif

# ifndef EDQUOT
#  define EDQUOT 2010
#  define GNULIB_defined_EDQUOT 1
# endif

# ifndef ECANCELED
#  define ECANCELED 2008
#  define GNULIB_defined_ECANCELED 1
# endif

/* On many platforms, the macros EOWNERDEAD and ENOTRECOVERABLE are not
   defined.  */

# ifndef EOWNERDEAD
#  if defined __sun
    /* Use the same values as defined for Solaris >= 8, for
       interoperability.  */
#   define EOWNERDEAD      58
#   define ENOTRECOVERABLE 59
#  else
#   define EOWNERDEAD      2013
#   define ENOTRECOVERABLE 2014
#  endif
#  define GNULIB_defined_EOWNERDEAD 1
#  define GNULIB_defined_ENOTRECOVERABLE 1
# endif

# ifndef EILSEQ
#  define EILSEQ 2015
#  define GNULIB_defined_EILSEQ 1
# endif

#endif /* _@GUARD_PREFIX@_ERRNO_H */
#endif /* _@GUARD_PREFIX@_ERRNO_H */
