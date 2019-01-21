/* conf_post.h --- configure.ac includes this via AH_BOTTOM

Copyright (C) 1988, 1993-1994, 1999-2002, 2004-2018 Free Software
Foundation, Inc.

This file is part of GNU Emacs.

GNU Emacs is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at
your option) any later version.

GNU Emacs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Emacs.  If not, see <https://www.gnu.org/licenses/>.  */

/* Put the code here rather than in configure.ac using AH_BOTTOM.
   This way, the code does not get processed by autoheader.  For
   example, undefs here are not commented out.

   To help make dependencies clearer elsewhere, this file typically
   does not #include other files.  The exceptions are first stdbool.h
   because it is unlikely to interfere with configuration and bool is
   such a core part of the C language, and second ms-w32.h (DOS_NT
   only) because it historically was included here and changing that
   would take some work.  */

#include <stdbool.h>

/* GNUC_PREREQ (V, W, X) is true if this is GNU C version V.W.X or later.
   It can be used in a preprocessor expression.  */
#ifndef __GNUC_MINOR__
# define GNUC_PREREQ(v, w, x) false
#elif ! defined __GNUC_PATCHLEVEL__
# define GNUC_PREREQ(v, w, x) \
    ((v) < __GNUC__ + ((w) < __GNUC_MINOR__ + ((x) == 0))
#else
# define GNUC_PREREQ(v, w, x) \
    ((v) < __GNUC__ + ((w) < __GNUC_MINOR__ + ((x) <= __GNUC_PATCHLEVEL__)))
#endif

/* The type of bool bitfields.  Needed to compile Objective-C with
   standard GCC.  It was also needed to port to pre-C99 compilers,
   although we don't care about that any more.  */
#if NS_IMPL_GNUSTEP
typedef unsigned int bool_bf;
#else
typedef bool bool_bf;
#endif

/* Simulate __has_attribute on compilers that lack it.  It is used only
   on arguments like alloc_size that are handled in this simulation.
   __has_attribute should be used only in #if expressions, as Oracle
   Studio 12.5's __has_attribute does not work in plain code.  */
#ifndef __has_attribute
# define __has_attribute(a) __has_attribute_##a
# define __has_attribute_alloc_size GNUC_PREREQ (4, 3, 0)
# define __has_attribute_cleanup GNUC_PREREQ (3, 4, 0)
# define __has_attribute_externally_visible GNUC_PREREQ (4, 1, 0)
# define __has_attribute_no_address_safety_analysis false
# define __has_attribute_no_sanitize_address GNUC_PREREQ (4, 8, 0)
#endif

/* Simulate __has_builtin on compilers that lack it.  It is used only
   on arguments like __builtin_assume_aligned that are handled in this
   simulation.  */
#ifndef __has_builtin
# define __has_builtin(a) __has_builtin_##a
# define __has_builtin___builtin_assume_aligned GNUC_PREREQ (4, 7, 0)
#endif

/* Simulate __has_feature on compilers that lack it.  It is used only
   to define ADDRESS_SANITIZER below.  */
#ifndef __has_feature
# define __has_feature(a) false
#endif

/* True if addresses are being sanitized.  */
#if defined __SANITIZE_ADDRESS__ || __has_feature (address_sanitizer)
# define ADDRESS_SANITIZER true
#else
# define ADDRESS_SANITIZER false
#endif

/* Yield PTR, which must be aligned to ALIGNMENT.  */
#if ! __has_builtin (__builtin_assume_aligned)
# define __builtin_assume_aligned(ptr, ...) ((void *) (ptr))
#endif

#ifdef DARWIN_OS
#if defined emacs && !defined CANNOT_DUMP
#define malloc unexec_malloc
#define realloc unexec_realloc
#define free unexec_free
#endif
#endif  /* DARWIN_OS */

/* If HYBRID_MALLOC is defined (e.g., on Cygwin), emacs will use
   gmalloc before dumping and the system malloc after dumping.
   hybrid_malloc and friends, defined in gmalloc.c, are wrappers that
   accomplish this.  */
#ifdef HYBRID_MALLOC
#ifdef emacs
#define malloc hybrid_malloc
#define realloc hybrid_realloc
#define aligned_alloc hybrid_aligned_alloc
#define calloc hybrid_calloc
#define free hybrid_free
#endif
#endif	/* HYBRID_MALLOC */

/* We have to go this route, rather than the old hpux9 approach of
   renaming the functions via macros.  The system's stdlib.h has fully
   prototyped declarations, which yields a conflicting definition of
   srand48; it tries to redeclare what was once srandom to be srand48.
   So we go with HAVE_LRAND48 being defined.  */
#ifdef HPUX
#undef srandom
#undef random
#undef HAVE_RANDOM
#undef HAVE_RINT
#endif  /* HPUX */

/* macOS / GNUstep need a bit more pure memory.  Of the existing knobs,
   SYSTEM_PURESIZE_EXTRA seems like the least likely to cause problems.  */
#ifdef HAVE_NS
#if defined NS_IMPL_GNUSTEP
#  define SYSTEM_PURESIZE_EXTRA 30000
#elif defined DARWIN_OS
#  define SYSTEM_PURESIZE_EXTRA 200000
#endif
#endif

#ifdef CYGWIN
#define SYSTEM_PURESIZE_EXTRA 50000
#endif

#if defined HAVE_NTGUI && !defined DebPrint
# ifdef EMACSDEBUG
extern void _DebPrint (const char *fmt, ...);
#  define DebPrint(stuff) _DebPrint stuff
# else
#  define DebPrint(stuff) ((void) 0)
# endif
#endif

#if defined CYGWIN && defined HAVE_NTGUI
# define NTGUI_UNICODE /* Cygwin runs only on UNICODE-supporting systems */
# define _WIN32_WINNT 0x500 /* Win2k */
/* The following was in /usr/include/string.h prior to Cygwin 1.7.33.  */
#ifndef strnicmp
#define strnicmp strncasecmp
#endif
#endif

#ifdef emacs /* Don't do this for lib-src.  */
/* Tell regex.c to use a type compatible with Emacs.  */
#define RE_TRANSLATE_TYPE Lisp_Object
#define RE_TRANSLATE(TBL, C) char_table_translate (TBL, C)
#define RE_TRANSLATE_P(TBL) (!EQ (TBL, make_number (0)))
#endif

/* Tell time_rz.c to use Emacs's getter and setter for TZ.
   Only Emacs uses time_rz so this is OK.  */
#define getenv_TZ emacs_getenv_TZ
#define setenv_TZ emacs_setenv_TZ
extern char *emacs_getenv_TZ (void);
extern int emacs_setenv_TZ (char const *);

#if __GNUC__ >= 3  /* On GCC 3.0 we might get a warning.  */
#define NO_INLINE __attribute__((noinline))
#else
#define NO_INLINE
#endif

#if __has_attribute (externally_visible)
#define EXTERNALLY_VISIBLE __attribute__((externally_visible))
#else
#define EXTERNALLY_VISIBLE
#endif

#if GNUC_PREREQ (2, 7, 0)
# define ATTRIBUTE_FORMAT(spec) __attribute__ ((__format__ spec))
#else
# define ATTRIBUTE_FORMAT(spec) /* empty */
#endif

#if GNUC_PREREQ (7, 0, 0)
# define FALLTHROUGH __attribute__ ((__fallthrough__))
#else
# define FALLTHROUGH ((void) 0)
#endif

#if GNUC_PREREQ (4, 4, 0) && defined __GLIBC_MINOR__
# define PRINTF_ARCHETYPE __gnu_printf__
#else
# define PRINTF_ARCHETYPE __printf__
#endif
#define ATTRIBUTE_FORMAT_PRINTF(string_index, first_to_check) \
  ATTRIBUTE_FORMAT ((PRINTF_ARCHETYPE, string_index, first_to_check))

#define ATTRIBUTE_CONST _GL_ATTRIBUTE_CONST
#define ATTRIBUTE_UNUSED _GL_UNUSED

#if GNUC_PREREQ (3, 3, 0) && !defined __ICC
# define ATTRIBUTE_MAY_ALIAS __attribute__ ((__may_alias__))
#else
# define ATTRIBUTE_MAY_ALIAS
#endif

/* Declare NAME to be a pointer to an object of type TYPE, initialized
   to the address ADDR, which may be of a different type.  Accesses
   via NAME may alias with other accesses with the traditional
   behavior, even if options like gcc -fstrict-aliasing are used.  */

#define DECLARE_POINTER_ALIAS(name, type, addr) \
  type ATTRIBUTE_MAY_ALIAS *name = (type *) (addr)

#if 3 <= __GNUC__
# define ATTRIBUTE_MALLOC __attribute__ ((__malloc__))
#else
# define ATTRIBUTE_MALLOC
#endif

#if __has_attribute (alloc_size)
# define ATTRIBUTE_ALLOC_SIZE(args) __attribute__ ((__alloc_size__ args))
#else
# define ATTRIBUTE_ALLOC_SIZE(args)
#endif

#define ATTRIBUTE_MALLOC_SIZE(args) ATTRIBUTE_MALLOC ATTRIBUTE_ALLOC_SIZE (args)

/* Work around GCC bug 59600: when a function is inlined, the inlined
   code may have its addresses sanitized even if the function has the
   no_sanitize_address attribute.  This bug is fixed in GCC 4.9.0 and
   clang 3.4.  */
#if (! ADDRESS_SANITIZER \
     || (GNUC_PREREQ (4, 9, 0) \
	 || 3 < __clang_major__ + (4 <= __clang_minor__)))
# define ADDRESS_SANITIZER_WORKAROUND /* No workaround needed.  */
#else
# define ADDRESS_SANITIZER_WORKAROUND NO_INLINE
#endif

/* Attribute of functions whose code should not have addresses
   sanitized.  */

#if __has_attribute (no_sanitize_address)
# define ATTRIBUTE_NO_SANITIZE_ADDRESS \
    __attribute__ ((no_sanitize_address)) ADDRESS_SANITIZER_WORKAROUND
#elif __has_attribute (no_address_safety_analysis)
# define ATTRIBUTE_NO_SANITIZE_ADDRESS \
    __attribute__ ((no_address_safety_analysis)) ADDRESS_SANITIZER_WORKAROUND
#else
# define ATTRIBUTE_NO_SANITIZE_ADDRESS
#endif

/* gcc -fsanitize=address does not work with vfork in Fedora 25 x86-64.
   For now, assume that this problem occurs on all platforms.  */
#if ADDRESS_SANITIZER && !defined vfork
# define vfork fork
#endif

/* Some versions of GNU/Linux define noinline in their headers.  */
#ifdef noinline
#undef noinline
#endif

/* Use Gnulib's extern-inline module for extern inline functions.
   An include file foo.h should prepend FOO_INLINE to function
   definitions, with the following overall pattern:

      [#include any other .h files first.]
      ...
      INLINE_HEADER_BEGIN
      ...
      INLINE int
      incr (int i)
      {
        return i + 1;
      }
      ...
      INLINE_HEADER_END

   For every executable, exactly one file that includes the header
   should do this:

      #define INLINE EXTERN_INLINE

   before including config.h or any other .h file.
   Other .c files should not define INLINE.
   For Emacs, this is done by having emacs.c first '#define INLINE
   EXTERN_INLINE' and then include every .h file that uses INLINE.

   The INLINE_HEADER_BEGIN and INLINE_HEADER_END suppress bogus
   warnings in some GCC versions; see ../m4/extern-inline.m4.

   C99 compilers compile functions like 'incr' as C99-style extern
   inline functions.  Buggy GCC implementations do something similar with
   GNU-specific keywords.  Buggy non-GCC compilers use static
   functions, which bloats the code but is good enough.  */

#ifndef INLINE
# define INLINE _GL_INLINE
#endif
#define EXTERN_INLINE _GL_EXTERN_INLINE
#define INLINE_HEADER_BEGIN _GL_INLINE_HEADER_BEGIN
#define INLINE_HEADER_END _GL_INLINE_HEADER_END

/* 'int x UNINIT;' is equivalent to 'int x;', except it cajoles GCC
   into not warning incorrectly about use of an uninitialized variable.  */
#if defined GCC_LINT || defined lint
# define UNINIT = {0,}
#else
# define UNINIT /* empty */
#endif
