/* System thread definitions
Copyright (C) 2012-2018 Free Software Foundation, Inc.

This file is part of GNU Emacs.

GNU Emacs is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

GNU Emacs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Emacs.  If not, see <https://www.gnu.org/licenses/>.  */

#include <config.h>
#include <setjmp.h>
#include "lisp.h"

#ifdef HAVE_NS
#include "nsterm.h"
#endif

#ifndef THREADS_ENABLED

void
sys_mutex_init (sys_mutex_t *m)
{
  *m = 0;
}

void
sys_mutex_lock (sys_mutex_t *m)
{
}

void
sys_mutex_unlock (sys_mutex_t *m)
{
}

void
sys_cond_init (sys_cond_t *c)
{
  *c = 0;
}

void
sys_cond_wait (sys_cond_t *c, sys_mutex_t *m)
{
}

void
sys_cond_signal (sys_cond_t *c)
{
}

void
sys_cond_broadcast (sys_cond_t *c)
{
}

void
sys_cond_destroy (sys_cond_t *c)
{
}

sys_thread_t
sys_thread_self (void)
{
  return 0;
}

bool
sys_thread_equal (sys_thread_t t, sys_thread_t u)
{
  return t == u;
}

int
sys_thread_create (sys_thread_t *t, const char *name,
		   thread_creation_function *func, void *datum)
{
  return 0;
}

void
sys_thread_yield (void)
{
}

#elif defined (HAVE_PTHREAD)

#include <sched.h>

#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

void
sys_mutex_init (sys_mutex_t *mutex)
{
  pthread_mutex_init (mutex, NULL);
}

void
sys_mutex_lock (sys_mutex_t *mutex)
{
  pthread_mutex_lock (mutex);
}

void
sys_mutex_unlock (sys_mutex_t *mutex)
{
  pthread_mutex_unlock (mutex);
}

void
sys_cond_init (sys_cond_t *cond)
{
  pthread_cond_init (cond, NULL);
}

void
sys_cond_wait (sys_cond_t *cond, sys_mutex_t *mutex)
{
  pthread_cond_wait (cond, mutex);
}

void
sys_cond_signal (sys_cond_t *cond)
{
  pthread_cond_signal (cond);
}

void
sys_cond_broadcast (sys_cond_t *cond)
{
  pthread_cond_broadcast (cond);
#ifdef HAVE_NS
  /* Send an app defined event to break out of the NS run loop.
     It seems that if ns_select is running the NS run loop, this
     broadcast has no effect until the loop is done, breaking a couple
     of tests in thread-tests.el. */
  ns_run_loop_break ();
#endif
}

void
sys_cond_destroy (sys_cond_t *cond)
{
  pthread_cond_destroy (cond);
}

sys_thread_t
sys_thread_self (void)
{
  return pthread_self ();
}

bool
sys_thread_equal (sys_thread_t t, sys_thread_t u)
{
  return pthread_equal (t, u);
}

int
sys_thread_create (sys_thread_t *thread_ptr, const char *name,
		   thread_creation_function *func, void *arg)
{
  pthread_attr_t attr;
  int result = 0;

  if (pthread_attr_init (&attr))
    return 0;

  /* Avoid crash on macOS with deeply nested GC (Bug#30364).  */
  size_t stack_size;
  size_t required_stack_size = sizeof (void *) * 1024 * 1024;
  if (pthread_attr_getstacksize (&attr, &stack_size) == 0
      && stack_size < required_stack_size)
    pthread_attr_setstacksize (&attr, required_stack_size);

  if (!pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED))
    {
      result = pthread_create (thread_ptr, &attr, func, arg) == 0;
#if defined (HAVE_SYS_PRCTL_H) && defined (HAVE_PRCTL) && defined (PR_SET_NAME)
      if (result && name != NULL)
	prctl (PR_SET_NAME, name);
#endif
    }

  pthread_attr_destroy (&attr);

  return result;
}

void
sys_thread_yield (void)
{
  sched_yield ();
}

#else

#error port me

#endif
