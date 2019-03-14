/* Evaluator for GNU Emacs Lisp interpreter.

Copyright (C) 1985-1987, 1993-1995, 1999-2018 Free Software Foundation,
Inc.

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


#include <config.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include "lisp.h"
#include "blockinput.h"
#include "commands.h"
#include "keyboard.h"
#include "dispextern.h"
#include "buffer.h"

/* CACHEABLE is ordinarily nothing, except it is 'volatile' if
   necessary to cajole GCC into not warning incorrectly that a
   variable should be volatile.  */
#if defined GCC_LINT || defined lint
# define CACHEABLE volatile
#else
# define CACHEABLE /* empty */
#endif

/* Chain of condition and catch handlers currently in effect.  */

/* struct handler *handlerlist; */

/* Non-nil means record all fset's and provide's, to be undone
   if the file being autoloaded is not fully loaded.
   They are recorded by being consed onto the front of Vautoload_queue:
   (FUN . ODEF) for a defun, (0 . OFEATURES) for a provide.  */

Lisp_Object Vautoload_queue;

/* This holds either the symbol `run-hooks' or nil.
   It is nil at an early stage of startup, and when Emacs
   is shutting down.  */
Lisp_Object Vrun_hooks;

/* The commented-out variables below are macros defined in thread.h.  */

/* Current number of specbindings allocated in specpdl, not counting
   the dummy entry specpdl[-1].  */

/* ptrdiff_t specpdl_size; */

/* Pointer to beginning of specpdl.  A dummy entry specpdl[-1] exists
   only so that its address can be taken.  */

/* union specbinding *specpdl; */

/* Pointer to first unused element in specpdl.  */

/* union specbinding *specpdl_ptr; */

/* Depth in Lisp evaluations and function calls.  */

/* static EMACS_INT lisp_eval_depth; */

/* The value of num_nonmacro_input_events as of the last time we
   started to enter the debugger.  If we decide to enter the debugger
   again when this is still equal to num_nonmacro_input_events, then we
   know that the debugger itself has an error, and we should just
   signal the error instead of entering an infinite loop of debugger
   invocations.  */

static EMACS_INT when_entered_debugger;

/* The function from which the last `signal' was called.  Set in
   Fsignal.  */
/* FIXME: We should probably get rid of this!  */
Lisp_Object Vsignaling_function;

/* If non-nil, Lisp code must not be run since some part of Emacs is in
   an inconsistent state.  Currently unused.  */
Lisp_Object inhibit_lisp_code;

/* These would ordinarily be static, but they need to be visible to GDB.  */
bool backtrace_p (union specbinding *) EXTERNALLY_VISIBLE;
Lisp_Object *backtrace_args (union specbinding *) EXTERNALLY_VISIBLE;
Lisp_Object backtrace_function (union specbinding *) EXTERNALLY_VISIBLE;
union specbinding *backtrace_next (union specbinding *) EXTERNALLY_VISIBLE;
union specbinding *backtrace_top (void) EXTERNALLY_VISIBLE;

static Lisp_Object apply_lambda (Lisp_Object, Lisp_Object, ptrdiff_t);
static Lisp_Object lambda_arity (Lisp_Object);

static Lisp_Object
specpdl_symbol (union specbinding *pdl)
{
  eassert (pdl->kind >= SPECPDL_LET);
  return pdl->let.symbol;
}

static enum specbind_tag
specpdl_kind (union specbinding *pdl)
{
  eassert (pdl->kind >= SPECPDL_LET);
  return pdl->let.kind;
}

static Lisp_Object
specpdl_old_value (union specbinding *pdl)
{
  eassert (pdl->kind >= SPECPDL_LET);
  return pdl->let.old_value;
}

static void
set_specpdl_old_value (union specbinding *pdl, Lisp_Object val)
{
  eassert (pdl->kind >= SPECPDL_LET);
  pdl->let.old_value = val;
}

static Lisp_Object
specpdl_where (union specbinding *pdl)
{
  eassert (pdl->kind > SPECPDL_LET);
  return pdl->let.where;
}

static Lisp_Object
specpdl_saved_value (union specbinding *pdl)
{
  eassert (pdl->kind >= SPECPDL_LET);
  return pdl->let.saved_value;
}

static Lisp_Object
specpdl_arg (union specbinding *pdl)
{
  eassert (pdl->kind == SPECPDL_UNWIND);
  return pdl->unwind.arg;
}

Lisp_Object
backtrace_function (union specbinding *pdl)
{
  eassert (pdl->kind == SPECPDL_BACKTRACE);
  return pdl->bt.function;
}

static ptrdiff_t
backtrace_nargs (union specbinding *pdl)
{
  eassert (pdl->kind == SPECPDL_BACKTRACE);
  return pdl->bt.nargs;
}

Lisp_Object *
backtrace_args (union specbinding *pdl)
{
  eassert (pdl->kind == SPECPDL_BACKTRACE);
  return pdl->bt.args;
}

bool
backtrace_debug_on_exit (union specbinding *pdl)
{
  eassert (pdl->kind == SPECPDL_BACKTRACE);
  return pdl->bt.debug_on_exit;
}

/* Functions to modify slots of backtrace records.  */

static void
set_backtrace_args (union specbinding *pdl, Lisp_Object *args, ptrdiff_t nargs)
{
  eassert (pdl->kind == SPECPDL_BACKTRACE);
  pdl->bt.args = args;
  pdl->bt.nargs = nargs;
}

static void
set_backtrace_debug_on_exit (union specbinding *pdl, bool doe)
{
  eassert (pdl->kind == SPECPDL_BACKTRACE);
  pdl->bt.debug_on_exit = doe;
}

/* Helper functions to scan the backtrace.  */

bool
backtrace_p (union specbinding *pdl)
{ return pdl >= specpdl; }

union specbinding *
backtrace_top (void)
{
  union specbinding *pdl = specpdl_ptr - 1;
  while (backtrace_p (pdl) && pdl->kind != SPECPDL_BACKTRACE)
    pdl--;
  return pdl;
}

union specbinding *
backtrace_next (union specbinding *pdl)
{
  pdl--;
  while (backtrace_p (pdl) && pdl->kind != SPECPDL_BACKTRACE)
    pdl--;
  return pdl;
}

void
init_eval_once (void)
{
  enum { size = 50 };
  union specbinding *pdlvec = xmalloc ((size + 1) * sizeof *specpdl);
  specpdl_size = size;
  specpdl = specpdl_ptr = pdlvec + 1;
  /* Don't forget to update docs (lispref node "Local Variables").  */
  max_specpdl_size = 1300; /* 1000 is not enough for CEDET's c-by.el.  */
  max_lisp_eval_depth = 800;

  Vrun_hooks = Qnil;
}

/* static struct handler handlerlist_sentinel; */

void
init_eval (void)
{
  specpdl_ptr = specpdl;
  { /* Put a dummy catcher at top-level so that handlerlist is never NULL.
       This is important since handlerlist->nextfree holds the freelist
       which would otherwise leak every time we unwind back to top-level.   */
    handlerlist_sentinel = xzalloc (sizeof (struct handler));
    handlerlist = handlerlist_sentinel->nextfree = handlerlist_sentinel;
    struct handler *c = push_handler (Qunbound, CATCHER);
    eassert (c == handlerlist_sentinel);
    handlerlist_sentinel->nextfree = NULL;
    handlerlist_sentinel->next = NULL;
  }
  Vquit_flag = Qnil;
  debug_on_next_call = 0;
  lisp_eval_depth = 0;
  /* This is less than the initial value of num_nonmacro_input_events.  */
  when_entered_debugger = -1;
}

/* Unwind-protect function used by call_debugger.  */

static void
restore_stack_limits (Lisp_Object data)
{
  max_specpdl_size = XINT (XCAR (data));
  max_lisp_eval_depth = XINT (XCDR (data));
}

/* Call the Lisp debugger, giving it argument ARG.  */

Lisp_Object
call_debugger (Lisp_Object arg)
{
  bool debug_while_redisplaying;
  ptrdiff_t count = SPECPDL_INDEX ();
  Lisp_Object val;
  EMACS_INT old_depth = max_lisp_eval_depth;
  /* Do not allow max_specpdl_size less than actual depth (Bug#16603).  */
  EMACS_INT old_max = max (max_specpdl_size, count);

  if (lisp_eval_depth + 40 > max_lisp_eval_depth)
    max_lisp_eval_depth = lisp_eval_depth + 40;

  /* While debugging Bug#16603, previous value of 100 was found
     too small to avoid specpdl overflow in the debugger itself.  */
  if (max_specpdl_size - 200 < count)
    max_specpdl_size = count + 200;

  if (old_max == count)
    {
      /* We can enter the debugger due to specpdl overflow (Bug#16603).  */
      specpdl_ptr--;
      grow_specpdl ();
    }

  /* Restore limits after leaving the debugger.  */
  record_unwind_protect (restore_stack_limits,
			 Fcons (make_number (old_max),
				make_number (old_depth)));

#ifdef HAVE_WINDOW_SYSTEM
  if (display_hourglass_p)
    cancel_hourglass ();
#endif

  debug_on_next_call = 0;
  when_entered_debugger = num_nonmacro_input_events;

  /* Resetting redisplaying_p to 0 makes sure that debug output is
     displayed if the debugger is invoked during redisplay.  */
  debug_while_redisplaying = redisplaying_p;
  redisplaying_p = 0;
  specbind (intern ("debugger-may-continue"),
	    debug_while_redisplaying ? Qnil : Qt);
  specbind (Qinhibit_redisplay, Qnil);
  specbind (Qinhibit_debugger, Qt);

  /* If we are debugging an error while `inhibit-changing-match-data'
     is bound to non-nil (e.g., within a call to `string-match-p'),
     then make sure debugger code can still use match data.  */
  specbind (Qinhibit_changing_match_data, Qnil);

#if 0 /* Binding this prevents execution of Lisp code during
	 redisplay, which necessarily leads to display problems.  */
  specbind (Qinhibit_eval_during_redisplay, Qt);
#endif

  val = apply1 (Vdebugger, arg);

  /* Interrupting redisplay and resuming it later is not safe under
     all circumstances.  So, when the debugger returns, abort the
     interrupted redisplay by going back to the top-level.  */
  if (debug_while_redisplaying)
    Ftop_level ();

  return unbind_to (count, val);
}

void
do_debug_on_call (Lisp_Object code, ptrdiff_t count)
{
  debug_on_next_call = 0;
  set_backtrace_debug_on_exit (specpdl + count, true);
  call_debugger (list1 (code));
}

/* NOTE!!! Every function that can call EVAL must protect its args
   and temporaries from garbage collection while it needs them.
   The definition of `For' shows what you have to do.  */

DEFUN ("defvaralias", Fdefvaralias, Sdefvaralias, 2, 3, 0,
       doc: /* Make NEW-ALIAS a variable alias for symbol BASE-VARIABLE.
Aliased variables always have the same value; setting one sets the other.
Third arg DOCSTRING, if non-nil, is documentation for NEW-ALIAS.  If it is
omitted or nil, NEW-ALIAS gets the documentation string of BASE-VARIABLE,
or of the variable at the end of the chain of aliases, if BASE-VARIABLE is
itself an alias.  If NEW-ALIAS is bound, and BASE-VARIABLE is not,
then the value of BASE-VARIABLE is set to that of NEW-ALIAS.
The return value is BASE-VARIABLE.  */)
  (Lisp_Object new_alias, Lisp_Object base_variable, Lisp_Object docstring)
{
  struct Lisp_Symbol *sym;

  CHECK_SYMBOL (new_alias);
  CHECK_SYMBOL (base_variable);

  if (SYMBOL_CONSTANT_P (new_alias))
    /* Making it an alias effectively changes its value.  */
    error ("Cannot make a constant an alias");

  sym = XSYMBOL (new_alias);

  switch (sym->u.s.redirect)
    {
    case SYMBOL_FORWARDED:
      error ("Cannot make an internal variable an alias");
    case SYMBOL_LOCALIZED:
      error ("Don't know how to make a localized variable an alias");
    case SYMBOL_PLAINVAL:
    case SYMBOL_VARALIAS:
      break;
    default:
      emacs_abort ();
    }

  /* https://lists.gnu.org/r/emacs-devel/2008-04/msg00834.html
     If n_a is bound, but b_v is not, set the value of b_v to n_a,
     so that old-code that affects n_a before the aliasing is setup
     still works.  */
  if (NILP (Fboundp (base_variable)))
    set_internal (base_variable, find_symbol_value (new_alias),
                  Qnil, SET_INTERNAL_BIND);
  {
    union specbinding *p;

    for (p = specpdl_ptr; p > specpdl; )
      if ((--p)->kind >= SPECPDL_LET
	  && (EQ (new_alias, specpdl_symbol (p))))
	error ("Don't know how to make a let-bound variable an alias");
  }

  if (sym->u.s.trapped_write == SYMBOL_TRAPPED_WRITE)
    notify_variable_watchers (new_alias, base_variable, Qdefvaralias, Qnil);

  sym->u.s.declared_special = true;
  XSYMBOL (base_variable)->u.s.declared_special = true;
  sym->u.s.redirect = SYMBOL_VARALIAS;
  SET_SYMBOL_ALIAS (sym, XSYMBOL (base_variable));
  sym->u.s.trapped_write = XSYMBOL (base_variable)->u.s.trapped_write;
  LOADHIST_ATTACH (new_alias);
  /* Even if docstring is nil: remove old docstring.  */
  Fput (new_alias, Qvariable_documentation, docstring);

  return base_variable;
}

static union specbinding *
default_toplevel_binding (Lisp_Object symbol)
{
  union specbinding *binding = NULL;
  union specbinding *pdl = specpdl_ptr;
  while (pdl > specpdl)
    {
      switch ((--pdl)->kind)
	{
	case SPECPDL_LET_DEFAULT:
	case SPECPDL_LET:
	  if (EQ (specpdl_symbol (pdl), symbol))
	    binding = pdl;
	  break;

	case SPECPDL_UNWIND:
	case SPECPDL_UNWIND_PTR:
	case SPECPDL_UNWIND_INT:
	case SPECPDL_UNWIND_VOID:
	case SPECPDL_BACKTRACE:
	case SPECPDL_LET_LOCAL:
	  break;

	default:
	  emacs_abort ();
	}
    }
  return binding;
}

DEFUN ("default-toplevel-value", Fdefault_toplevel_value, Sdefault_toplevel_value, 1, 1, 0,
       doc: /* Return SYMBOL's toplevel default value.
"Toplevel" means outside of any let binding.  */)
  (Lisp_Object symbol)
{
  union specbinding *binding = default_toplevel_binding (symbol);
  Lisp_Object value
    = binding ? specpdl_old_value (binding) : Fdefault_value (symbol);
  if (!EQ (value, Qunbound))
    return value;
  xsignal1 (Qvoid_variable, symbol);
}

DEFUN ("set-default-toplevel-value", Fset_default_toplevel_value,
       Sset_default_toplevel_value, 2, 2, 0,
       doc: /* Set SYMBOL's toplevel default value to VALUE.
"Toplevel" means outside of any let binding.  */)
     (Lisp_Object symbol, Lisp_Object value)
{
  union specbinding *binding = default_toplevel_binding (symbol);
  if (binding)
    set_specpdl_old_value (binding, value);
  else
    Fset_default (symbol, value);
  return Qnil;
}

DEFUN ("defvar", Fdefvar, Sdefvar, 1, UNEVALLED, 0,
       doc: /* Define SYMBOL as a variable, and return SYMBOL.
You are not required to define a variable in order to use it, but
defining it lets you supply an initial value and documentation, which
can be referred to by the Emacs help facilities and other programming
tools.  The `defvar' form also declares the variable as \"special\",
so that it is always dynamically bound even if `lexical-binding' is t.

If SYMBOL's value is void and the optional argument INITVALUE is
provided, INITVALUE is evaluated and the result used to set SYMBOL's
value.  If SYMBOL is buffer-local, its default value is what is set;
buffer-local values are not affected.  If INITVALUE is missing,
SYMBOL's value is not set.

If SYMBOL has a local binding, then this form affects the local
binding.  This is usually not what you want.  Thus, if you need to
load a file defining variables, with this form or with `defconst' or
`defcustom', you should always load that file _outside_ any bindings
for these variables.  (`defconst' and `defcustom' behave similarly in
this respect.)

The optional argument DOCSTRING is a documentation string for the
variable.

To define a user option, use `defcustom' instead of `defvar'.
usage: (defvar SYMBOL &optional INITVALUE DOCSTRING)  */)
  (Lisp_Object args)
{
  Lisp_Object sym, tem, tail;

  sym = XCAR (args);
  tail = XCDR (args);

  if (!NILP (tail))
    {
      if (!NILP (XCDR (tail)) && !NILP (XCDR (XCDR (tail))))
	error ("Too many arguments");

      tem = Fdefault_boundp (sym);

      /* Do it before evaluating the initial value, for self-references.  */
      XSYMBOL (sym)->u.s.declared_special = true;

      if (NILP (tem))
	Fset_default (sym, eval_sub (XCAR (tail)));
      else
	{ /* Check if there is really a global binding rather than just a let
	     binding that shadows the global unboundness of the var.  */
	  union specbinding *binding = default_toplevel_binding (sym);
	  if (binding && EQ (specpdl_old_value (binding), Qunbound))
	    {
	      set_specpdl_old_value (binding, eval_sub (XCAR (tail)));
	    }
	}
      tail = XCDR (tail);
      tem = Fcar (tail);
      if (!NILP (tem))
	{
	  if (!NILP (Vpurify_flag))
	    tem = Fpurecopy (tem);
	  Fput (sym, Qvariable_documentation, tem);
	}
      LOADHIST_ATTACH (sym);
    }
  else if (!NILP (Vinternal_interpreter_environment)
	   && !XSYMBOL (sym)->u.s.declared_special)
    /* A simple (defvar foo) with lexical scoping does "nothing" except
       declare that var to be dynamically scoped *locally* (i.e. within
       the current file or let-block).  */
    Vinternal_interpreter_environment
      = Fcons (sym, Vinternal_interpreter_environment);
  else
    {
      /* Simple (defvar <var>) should not count as a definition at all.
	 It could get in the way of other definitions, and unloading this
	 package could try to make the variable unbound.  */
    }

  return sym;
}

/* Assert that E is true, but do not evaluate E.  Use this instead of
   eassert (E) when E contains variables that might be clobbered by a
   longjmp.  */

#define clobbered_eassert(E) verify (sizeof (E) != 0)

/* Set up a catch, then call C function FUNC on argument ARG.
   FUNC should return a Lisp_Object.
   This is how catches are done from within C code.  */

Lisp_Object
internal_catch (Lisp_Object tag,
		Lisp_Object (*func) (Lisp_Object), Lisp_Object arg)
{
  /* This structure is made part of the chain `catchlist'.  */
  struct handler *c = push_handler (tag, CATCHER);

  /* Call FUNC.  */
  if (! sys_setjmp (c->jmp))
    {
      Lisp_Object val = func (arg);
      eassert (handlerlist == c);
      handlerlist = c->next;
      return val;
    }
  else
    { /* Throw works by a longjmp that comes right here.  */
      Lisp_Object val = handlerlist->val;
      clobbered_eassert (handlerlist == c);
      handlerlist = handlerlist->next;
      return val;
    }
}

/* Unwind the specbind, catch, and handler stacks back to CATCH, and
   jump to that CATCH, returning VALUE as the value of that catch.

   This is the guts of Fthrow and Fsignal; they differ only in the way
   they choose the catch tag to throw to.  A catch tag for a
   condition-case form has a TAG of Qnil.

   Before each catch is discarded, unbind all special bindings and
   execute all unwind-protect clauses made above that catch.  Unwind
   the handler stack as we go, so that the proper handlers are in
   effect for each unwind-protect clause we run.  At the end, restore
   some static info saved in CATCH, and longjmp to the location
   specified there.

   This is used for correct unwinding in Fthrow and Fsignal.  */

static _Noreturn void
unwind_to_catch (struct handler *catch, Lisp_Object value)
{
  bool last_time;

  eassert (catch->next);

  /* Save the value in the tag.  */
  catch->val = value;

  /* Restore certain special C variables.  */
  set_poll_suppress_count (catch->poll_suppress_count);
  unblock_input_to (catch->interrupt_input_blocked);

  do
    {
      /* Unwind the specpdl stack, and then restore the proper set of
	 handlers.  */
      unbind_to (handlerlist->pdlcount, Qnil);
      last_time = handlerlist == catch;
      if (! last_time)
	handlerlist = handlerlist->next;
    }
  while (! last_time);

  eassert (handlerlist == catch);

  lisp_eval_depth = catch->f_lisp_eval_depth;

  sys_longjmp (catch->jmp, 1);
}

DEFUN ("throw", Fthrow, Sthrow, 2, 2, 0,
       doc: /* Throw to the catch for TAG and return VALUE from it.
Both TAG and VALUE are evalled.  */
       attributes: noreturn)
  (register Lisp_Object tag, Lisp_Object value)
{
  struct handler *c;

  if (!NILP (tag))
    for (c = handlerlist; c; c = c->next)
      {
	if (c->type == CATCHER_ALL)
          unwind_to_catch (c, Fcons (tag, value));
	if (c->type == CATCHER && EQ (c->tag_or_ch, tag))
	  unwind_to_catch (c, value);
      }
  xsignal2 (Qno_catch, tag, value);
}

/* Like Fcondition_case, but the args are separate
   rather than passed in a list.  Used by Fbyte_code.  */

Lisp_Object
internal_lisp_condition_case (Lisp_Object var, Lisp_Object bodyform,
			      Lisp_Object handlers)
{
  struct handler *oldhandlerlist = handlerlist;
  ptrdiff_t CACHEABLE clausenb = 0;

  CHECK_SYMBOL (var);

  for (Lisp_Object tail = handlers; CONSP (tail); tail = XCDR (tail))
    {
      Lisp_Object tem = XCAR (tail);
      clausenb++;
      if (! (NILP (tem)
	     || (CONSP (tem)
		 && (SYMBOLP (XCAR (tem))
		     || CONSP (XCAR (tem))))))
	error ("Invalid condition handler: %s",
	       SDATA (Fprin1_to_string (tem, Qt)));
    }

  /* The first clause is the one that should be checked first, so it
     should be added to handlerlist last.  So build in CLAUSES a table
     that contains HANDLERS but in reverse order.  CLAUSES is pointer
     to volatile to avoid issues with setjmp and local storage.
     SAFE_ALLOCA won't work here due to the setjmp, so impose a
     MAX_ALLOCA limit.  */
  if (MAX_ALLOCA / word_size < clausenb)
    memory_full (SIZE_MAX);
  Lisp_Object volatile *clauses = alloca (clausenb * sizeof *clauses);
  clauses += clausenb;
  for (Lisp_Object tail = handlers; CONSP (tail); tail = XCDR (tail))
    *--clauses = XCAR (tail);
  for (ptrdiff_t i = 0; i < clausenb; i++)
    {
      Lisp_Object clause = clauses[i];
      Lisp_Object condition = CONSP (clause) ? XCAR (clause) : Qnil;
      if (!CONSP (condition))
	condition = list1 (condition);
      struct handler *c = push_handler (condition, CONDITION_CASE);
      if (sys_setjmp (c->jmp))
	{
	  Lisp_Object val = handlerlist->val;
	  Lisp_Object volatile *chosen_clause = clauses;
	  for (struct handler *h = handlerlist->next; h != oldhandlerlist;
	       h = h->next)
	    chosen_clause++;
	  Lisp_Object handler_body = XCDR (*chosen_clause);
	  handlerlist = oldhandlerlist;

	  if (NILP (var))
	    return Fprogn (handler_body);

	  Lisp_Object handler_var = var;
	  if (!NILP (Vinternal_interpreter_environment))
	    {
	      val = Fcons (Fcons (var, val),
			   Vinternal_interpreter_environment);
	      handler_var = Qinternal_interpreter_environment;
	    }

	  /* Bind HANDLER_VAR to VAL while evaluating HANDLER_BODY.
	     The unbind_to undoes just this binding; whoever longjumped
	     to us unwound the stack to C->pdlcount before throwing.  */
	  ptrdiff_t count = SPECPDL_INDEX ();
	  specbind (handler_var, val);
	  return unbind_to (count, Fprogn (handler_body));
	}
    }

  Lisp_Object result = eval_sub (bodyform);
  handlerlist = oldhandlerlist;
  return result;
}

/* Call the function BFUN with no arguments, catching errors within it
   according to HANDLERS.  If there is an error, call HFUN with
   one argument which is the data that describes the error:
   (SIGNALNAME . DATA)

   HANDLERS can be a list of conditions to catch.
   If HANDLERS is Qt, catch all errors.
   If HANDLERS is Qerror, catch all errors
   but allow the debugger to run if that is enabled.  */

Lisp_Object
internal_condition_case (Lisp_Object (*bfun) (void), Lisp_Object handlers,
			 Lisp_Object (*hfun) (Lisp_Object))
{
  struct handler *c = push_handler (handlers, CONDITION_CASE);
  if (sys_setjmp (c->jmp))
    {
      Lisp_Object val = handlerlist->val;
      clobbered_eassert (handlerlist == c);
      handlerlist = handlerlist->next;
      return hfun (val);
    }
  else
    {
      Lisp_Object val = bfun ();
      eassert (handlerlist == c);
      handlerlist = c->next;
      return val;
    }
}

/* Like internal_condition_case but call BFUN with ARG as its argument.  */

Lisp_Object
internal_condition_case_1 (Lisp_Object (*bfun) (Lisp_Object), Lisp_Object arg,
			   Lisp_Object handlers,
			   Lisp_Object (*hfun) (Lisp_Object))
{
  struct handler *c = push_handler (handlers, CONDITION_CASE);
  if (sys_setjmp (c->jmp))
    {
      Lisp_Object val = handlerlist->val;
      clobbered_eassert (handlerlist == c);
      handlerlist = handlerlist->next;
      return hfun (val);
    }
  else
    {
      Lisp_Object val = bfun (arg);
      eassert (handlerlist == c);
      handlerlist = c->next;
      return val;
    }
}

/* Like internal_condition_case_1 but call BFUN with ARG1 and ARG2 as
   its arguments.  */

Lisp_Object
internal_condition_case_2 (Lisp_Object (*bfun) (Lisp_Object, Lisp_Object),
			   Lisp_Object arg1,
			   Lisp_Object arg2,
			   Lisp_Object handlers,
			   Lisp_Object (*hfun) (Lisp_Object))
{
  struct handler *c = push_handler (handlers, CONDITION_CASE);
  if (sys_setjmp (c->jmp))
    {
      Lisp_Object val = handlerlist->val;
      clobbered_eassert (handlerlist == c);
      handlerlist = handlerlist->next;
      return hfun (val);
    }
  else
    {
      Lisp_Object val = bfun (arg1, arg2);
      eassert (handlerlist == c);
      handlerlist = c->next;
      return val;
    }
}

/* Like internal_condition_case but call BFUN with NARGS as first,
   and ARGS as second argument.  */

Lisp_Object
internal_condition_case_n (Lisp_Object (*bfun) (ptrdiff_t, Lisp_Object *),
			   ptrdiff_t nargs,
			   Lisp_Object *args,
			   Lisp_Object handlers,
			   Lisp_Object (*hfun) (Lisp_Object err,
						ptrdiff_t nargs,
						Lisp_Object *args))
{
  struct handler *c = push_handler (handlers, CONDITION_CASE);
  if (sys_setjmp (c->jmp))
    {
      Lisp_Object val = handlerlist->val;
      clobbered_eassert (handlerlist == c);
      handlerlist = handlerlist->next;
      return hfun (val, nargs, args);
    }
  else
    {
      Lisp_Object val = bfun (nargs, args);
      eassert (handlerlist == c);
      handlerlist = c->next;
      return val;
    }
}

static Lisp_Object
internal_catch_all_1 (Lisp_Object (*function) (void *), void *argument)
{
  struct handler *c = push_handler_nosignal (Qt, CATCHER_ALL);
  if (c == NULL)
    return Qcatch_all_memory_full;

  if (sys_setjmp (c->jmp) == 0)
    {
      Lisp_Object val = function (argument);
      eassert (handlerlist == c);
      handlerlist = c->next;
      return val;
    }
  else
    {
      eassert (handlerlist == c);
      Lisp_Object val = c->val;
      handlerlist = c->next;
      xsignal (Qno_catch, val);
    }
}

/* Like a combination of internal_condition_case_1 and internal_catch.
   Catches all signals and throws.  Never exits nonlocally; returns
   Qcatch_all_memory_full if no handler could be allocated.  */

Lisp_Object
internal_catch_all (Lisp_Object (*function) (void *), void *argument,
                    Lisp_Object (*handler) (Lisp_Object))
{
  struct handler *c = push_handler_nosignal (Qt, CONDITION_CASE);
  if (c == NULL)
    return Qcatch_all_memory_full;

  if (sys_setjmp (c->jmp) == 0)
    {
      Lisp_Object val = internal_catch_all_1 (function, argument);
      eassert (handlerlist == c);
      handlerlist = c->next;
      return val;
    }
  else
    {
      eassert (handlerlist == c);
      Lisp_Object val = c->val;
      handlerlist = c->next;
      return handler (val);
    }
}

struct handler *
push_handler (Lisp_Object tag_ch_val, enum handlertype handlertype)
{
  struct handler *c = push_handler_nosignal (tag_ch_val, handlertype);
  if (!c)
    memory_full (sizeof *c);
  return c;
}

struct handler *
push_handler_nosignal (Lisp_Object tag_ch_val, enum handlertype handlertype)
{
  struct handler *CACHEABLE c = handlerlist->nextfree;
  if (!c)
    {
      c = malloc (sizeof *c);
      if (!c)
	return c;
      if (profiler_memory_running)
	malloc_probe (sizeof *c);
      c->nextfree = NULL;
      handlerlist->nextfree = c;
    }
  c->type = handlertype;
  c->tag_or_ch = tag_ch_val;
  c->val = Qnil;
  c->next = handlerlist;
  c->f_lisp_eval_depth = lisp_eval_depth;
  c->pdlcount = SPECPDL_INDEX ();
  c->poll_suppress_count = poll_suppress_count;
  c->interrupt_input_blocked = interrupt_input_blocked;
  handlerlist = c;
  return c;
}


static Lisp_Object find_handler_clause (Lisp_Object, Lisp_Object);
static bool maybe_call_debugger (Lisp_Object conditions, Lisp_Object sig,
				 Lisp_Object data);

static void
process_quit_flag (void)
{
  Lisp_Object flag = Vquit_flag;
  Vquit_flag = Qnil;
  if (EQ (flag, Qkill_emacs))
    Fkill_emacs (Qnil);
  if (EQ (Vthrow_on_input, flag))
    Fthrow (Vthrow_on_input, Qt);
  quit ();
}

/* Check quit-flag and quit if it is non-nil.  Typing C-g does not
   directly cause a quit; it only sets Vquit_flag.  So the program
   needs to call maybe_quit at times when it is safe to quit.  Every
   loop that might run for a long time or might not exit ought to call
   maybe_quit at least once, at a safe place.  Unless that is
   impossible, of course.  But it is very desirable to avoid creating
   loops where maybe_quit is impossible.

   If quit-flag is set to `kill-emacs' the SIGINT handler has received
   a request to exit Emacs when it is safe to do.

   When not quitting, process any pending signals.

   If you change this function, also adapt module_should_quit in
   emacs-module.c.  */

void
maybe_quit (void)
{
  if (!NILP (Vquit_flag) && NILP (Vinhibit_quit))
    process_quit_flag ();
  else if (pending_signals)
    process_pending_signals ();
}

/* Quit, in response to a keyboard quit request.  */
Lisp_Object
quit (void)
{
  return signal_or_quit (Qquit, Qnil, true);
}

/* Signal an error, or quit.  ERROR_SYMBOL and DATA are as with Fsignal.
   If KEYBOARD_QUIT, this is a quit; ERROR_SYMBOL should be
   Qquit and DATA should be Qnil, and this function may return.
   Otherwise this function is like Fsignal and does not return.  */

Lisp_Object
signal_or_quit (Lisp_Object error_symbol, Lisp_Object data, bool keyboard_quit)
{
  /* When memory is full, ERROR-SYMBOL is nil,
     and DATA is (REAL-ERROR-SYMBOL . REAL-DATA).
     That is a special case--don't do this in other situations.  */
  Lisp_Object conditions;
  Lisp_Object string;
  Lisp_Object real_error_symbol
    = (NILP (error_symbol) ? Fcar (data) : error_symbol);
  Lisp_Object clause = Qnil;
  struct handler *h;

  if (gc_in_progress || waiting_for_input)
    emacs_abort ();

#if 0 /* rms: I don't know why this was here,
	 but it is surely wrong for an error that is handled.  */
#ifdef HAVE_WINDOW_SYSTEM
  if (display_hourglass_p)
    cancel_hourglass ();
#endif
#endif

  /* This hook is used by edebug.  */
  if (! NILP (Vsignal_hook_function)
      && ! NILP (error_symbol)
      /* Don't try to call a lisp function if we've already overflowed
         the specpdl stack.  */
      && specpdl_ptr < specpdl + specpdl_size)
    {
      /* Edebug takes care of restoring these variables when it exits.  */
      if (lisp_eval_depth + 20 > max_lisp_eval_depth)
	max_lisp_eval_depth = lisp_eval_depth + 20;

      if (SPECPDL_INDEX () + 40 > max_specpdl_size)
	max_specpdl_size = SPECPDL_INDEX () + 40;

      call2 (Vsignal_hook_function, error_symbol, data);
    }

  conditions = Fget (real_error_symbol, Qerror_conditions);

  /* Remember from where signal was called.  Skip over the frame for
     `signal' itself.  If a frame for `error' follows, skip that,
     too.  Don't do this when ERROR_SYMBOL is nil, because that
     is a memory-full error.  */
  Vsignaling_function = Qnil;
  if (!NILP (error_symbol))
    {
      union specbinding *pdl = backtrace_next (backtrace_top ());
      if (backtrace_p (pdl) && EQ (backtrace_function (pdl), Qerror))
	pdl = backtrace_next (pdl);
      if (backtrace_p (pdl))
	Vsignaling_function = backtrace_function (pdl);
    }

  for (h = handlerlist; h; h = h->next)
    {
      if (h->type != CONDITION_CASE)
	continue;
      clause = find_handler_clause (h->tag_or_ch, conditions);
      if (!NILP (clause))
	break;
    }

  if (/* Don't run the debugger for a memory-full error.
	 (There is no room in memory to do that!)  */
      !NILP (error_symbol)
      && (!NILP (Vdebug_on_signal)
	  /* If no handler is present now, try to run the debugger.  */
	  || NILP (clause)
	  /* A `debug' symbol in the handler list disables the normal
	     suppression of the debugger.  */
	  || (CONSP (clause) && !NILP (Fmemq (Qdebug, clause)))
	  /* Special handler that means "print a message and run debugger
	     if requested".  */
	  || EQ (h->tag_or_ch, Qerror)))
    {
      bool debugger_called
	= maybe_call_debugger (conditions, error_symbol, data);
      /* We can't return values to code which signaled an error, but we
	 can continue code which has signaled a quit.  */
      if (keyboard_quit && debugger_called && EQ (real_error_symbol, Qquit))
	return Qnil;
    }

  if (!NILP (clause))
    {
      Lisp_Object unwind_data
	= (NILP (error_symbol) ? data : Fcons (error_symbol, data));

      unwind_to_catch (h, unwind_data);
    }
  else
    {
      if (handlerlist != handlerlist_sentinel)
	/* FIXME: This will come right back here if there's no `top-level'
	   catcher.  A better solution would be to abort here, and instead
	   add a catch-all condition handler so we never come here.  */
	Fthrow (Qtop_level, Qt);
    }

  if (! NILP (error_symbol))
    data = Fcons (error_symbol, data);

  string = Ferror_message_string (data);
  fatal ("%s", SDATA (string));
}

/* Like xsignal, but takes 0, 1, 2, or 3 args instead of a list.  */

void
xsignal0 (Lisp_Object error_symbol)
{
  xsignal (error_symbol, Qnil);
}

void
xsignal1 (Lisp_Object error_symbol, Lisp_Object arg)
{
  xsignal (error_symbol, list1 (arg));
}

void
xsignal2 (Lisp_Object error_symbol, Lisp_Object arg1, Lisp_Object arg2)
{
  xsignal (error_symbol, list2 (arg1, arg2));
}

void
xsignal3 (Lisp_Object error_symbol, Lisp_Object arg1, Lisp_Object arg2, Lisp_Object arg3)
{
  xsignal (error_symbol, list3 (arg1, arg2, arg3));
}

/* Signal `error' with message S, and additional arg ARG.
   If ARG is not a genuine list, make it a one-element list.  */

void
signal_error (const char *s, Lisp_Object arg)
{
  Lisp_Object tortoise, hare;

  hare = tortoise = arg;
  while (CONSP (hare))
    {
      hare = XCDR (hare);
      if (!CONSP (hare))
	break;

      hare = XCDR (hare);
      tortoise = XCDR (tortoise);

      if (EQ (hare, tortoise))
	break;
    }

  if (!NILP (hare))
    arg = list1 (arg);

  xsignal (Qerror, Fcons (build_string (s), arg));
}


/* Return true if LIST is a non-nil atom or
   a list containing one of CONDITIONS.  */

static bool
wants_debugger (Lisp_Object list, Lisp_Object conditions)
{
  if (NILP (list))
    return 0;
  if (! CONSP (list))
    return 1;

  while (CONSP (conditions))
    {
      Lisp_Object this, tail;
      this = XCAR (conditions);
      for (tail = list; CONSP (tail); tail = XCDR (tail))
	if (EQ (XCAR (tail), this))
	  return 1;
      conditions = XCDR (conditions);
    }
  return 0;
}

/* Return true if an error with condition-symbols CONDITIONS,
   and described by SIGNAL-DATA, should skip the debugger
   according to debugger-ignored-errors.  */

static bool
skip_debugger (Lisp_Object conditions, Lisp_Object data)
{
  Lisp_Object tail;
  bool first_string = 1;
  Lisp_Object error_message;

  error_message = Qnil;
  for (tail = Vdebug_ignored_errors; CONSP (tail); tail = XCDR (tail))
    {
      if (STRINGP (XCAR (tail)))
	{
	  if (first_string)
	    {
	      error_message = Ferror_message_string (data);
	      first_string = 0;
	    }

	  if (fast_string_match (XCAR (tail), error_message) >= 0)
	    return 1;
	}
      else
	{
	  Lisp_Object contail;

	  for (contail = conditions; CONSP (contail); contail = XCDR (contail))
	    if (EQ (XCAR (tail), XCAR (contail)))
	      return 1;
	}
    }

  return 0;
}

/* Call the debugger if calling it is currently enabled for CONDITIONS.
   SIG and DATA describe the signal.  There are two ways to pass them:
    = SIG is the error symbol, and DATA is the rest of the data.
    = SIG is nil, and DATA is (SYMBOL . REST-OF-DATA).
      This is for memory-full errors only.  */
static bool
maybe_call_debugger (Lisp_Object conditions, Lisp_Object sig, Lisp_Object data)
{
  Lisp_Object combined_data;

  combined_data = Fcons (sig, data);

  if (
      /* Don't try to run the debugger with interrupts blocked.
	 The editing loop would return anyway.  */
      ! input_blocked_p ()
      && NILP (Vinhibit_debugger)
      /* Does user want to enter debugger for this kind of error?  */
      && (EQ (sig, Qquit)
	  ? debug_on_quit
	  : wants_debugger (Vdebug_on_error, conditions))
      && ! skip_debugger (conditions, combined_data)
      /* RMS: What's this for?  */
      && when_entered_debugger < num_nonmacro_input_events)
    {
      call_debugger (list2 (Qerror, combined_data));
      return 1;
    }

  return 0;
}

static Lisp_Object
find_handler_clause (Lisp_Object handlers, Lisp_Object conditions)
{
  register Lisp_Object h;

  /* t is used by handlers for all conditions, set up by C code.  */
  if (EQ (handlers, Qt))
    return Qt;

  /* error is used similarly, but means print an error message
     and run the debugger if that is enabled.  */
  if (EQ (handlers, Qerror))
    return Qt;

  for (h = handlers; CONSP (h); h = XCDR (h))
    {
      Lisp_Object handler = XCAR (h);
      if (!NILP (Fmemq (handler, conditions)))
	return handlers;
    }

  return Qnil;
}


/* Format and return a string; called like vprintf.  */
Lisp_Object
vformat_string (const char *m, va_list ap)
{
  char buf[4000];
  ptrdiff_t size = sizeof buf;
  ptrdiff_t size_max = STRING_BYTES_BOUND + 1;
  char *buffer = buf;
  ptrdiff_t used;
  Lisp_Object string;

  used = evxprintf (&buffer, &size, buf, size_max, m, ap);
  string = make_string (buffer, used);
  if (buffer != buf)
    xfree (buffer);

  return string;
}

/* Dump an error message; called like vprintf.  */
void
verror (const char *m, va_list ap)
{
  xsignal1 (Qerror, vformat_string (m, ap));
}


/* Dump an error message; called like printf.  */

/* VARARGS 1 */
void
error (const char *m, ...)
{
  va_list ap;
  va_start (ap, m);
  verror (m, ap);
}

/* Grow the specpdl stack by one entry.
   The caller should have already initialized the entry.
   Signal an error on stack overflow.

   Make sure that there is always one unused entry past the top of the
   stack, so that the just-initialized entry is safely unwound if
   memory exhausted and an error is signaled here.  Also, allocate a
   never-used entry just before the bottom of the stack; sometimes its
   address is taken.  */

void
grow_specpdl (void)
{
  specpdl_ptr++;

  if (specpdl_ptr == specpdl + specpdl_size)
    {
      ptrdiff_t count = SPECPDL_INDEX ();
      ptrdiff_t max_size = min (max_specpdl_size, PTRDIFF_MAX - 1000);
      union specbinding *pdlvec = specpdl - 1;
      ptrdiff_t pdlvecsize = specpdl_size + 1;
      if (max_size <= specpdl_size)
	{
	  if (max_specpdl_size < 400)
	    max_size = max_specpdl_size = 400;
	  if (max_size <= specpdl_size)
	    signal_error ("Variable binding depth exceeds max-specpdl-size",
			  Qnil);
	}
      pdlvec = xpalloc (pdlvec, &pdlvecsize, 1, max_size + 1, sizeof *specpdl);
      specpdl = pdlvec + 1;
      specpdl_size = pdlvecsize - 1;
      specpdl_ptr = specpdl + count;
    }
}

ptrdiff_t
record_in_backtrace (Lisp_Object function, Lisp_Object *args, ptrdiff_t nargs)
{
  ptrdiff_t count = SPECPDL_INDEX ();

  eassert (nargs >= UNEVALLED);
  specpdl_ptr->bt.kind = SPECPDL_BACKTRACE;
  specpdl_ptr->bt.debug_on_exit = false;
  specpdl_ptr->bt.function = function;
  current_thread->stack_top = specpdl_ptr->bt.args = args;
  specpdl_ptr->bt.nargs = nargs;
  grow_specpdl ();

  return count;
}

/* Eval a sub-expression of the current expression (i.e. in the same
   lexical scope).  */
Lisp_Object
eval_sub (Lisp_Object form)
{
  Lisp_Object fun, val, original_fun, original_args;
  Lisp_Object funcar;
  ptrdiff_t count;

  /* Declare here, as this array may be accessed by call_debugger near
     the end of this function.  See Bug#21245.  */
  Lisp_Object argvals[8];

  if (SYMBOLP (form))
    {
      /* Look up its binding in the lexical environment.
	 We do not pay attention to the declared_special flag here, since we
	 already did that when let-binding the variable.  */
      Lisp_Object lex_binding
	= !NILP (Vinternal_interpreter_environment) /* Mere optimization!  */
	? Fassq (form, Vinternal_interpreter_environment)
	: Qnil;
      if (CONSP (lex_binding))
	return XCDR (lex_binding);
      else
	return Fsymbol_value (form);
    }

  if (!CONSP (form))
    return form;

  maybe_quit ();

  maybe_gc ();

  if (++lisp_eval_depth > max_lisp_eval_depth)
    {
      if (max_lisp_eval_depth < 100)
	max_lisp_eval_depth = 100;
      if (lisp_eval_depth > max_lisp_eval_depth)
	error ("Lisp nesting exceeds `max-lisp-eval-depth'");
    }

  original_fun = XCAR (form);
  original_args = XCDR (form);
  CHECK_LIST (original_args);

  /* This also protects them from gc.  */
  count = record_in_backtrace (original_fun, &original_args, UNEVALLED);

  if (debug_on_next_call)
    do_debug_on_call (Qt, count);

  /* At this point, only original_fun and original_args
     have values that will be used below.  */
 retry:

  /* Optimize for no indirection.  */
  fun = original_fun;
  if (!SYMBOLP (fun))
    fun = Ffunction (Fcons (fun, Qnil));
  else if (!NILP (fun) && (fun = XSYMBOL (fun)->u.s.function, SYMBOLP (fun)))
    fun = indirect_function (fun);

  if (SUBRP (fun))
    {
      Lisp_Object args_left = original_args;
      Lisp_Object numargs = Flength (args_left);

      check_cons_list ();

      if (XINT (numargs) < XSUBR (fun)->min_args
	  || (XSUBR (fun)->max_args >= 0
	      && XSUBR (fun)->max_args < XINT (numargs)))
	xsignal2 (Qwrong_number_of_arguments, original_fun, numargs);

      else if (XSUBR (fun)->max_args == UNEVALLED)
	val = (XSUBR (fun)->function.aUNEVALLED) (args_left);
      else if (XSUBR (fun)->max_args == MANY)
	{
	  /* Pass a vector of evaluated arguments.  */
	  Lisp_Object *vals;
	  ptrdiff_t argnum = 0;
	  USE_SAFE_ALLOCA;

	  SAFE_ALLOCA_LISP (vals, XINT (numargs));

	  while (CONSP (args_left) && argnum < XINT (numargs))
	    {
	      Lisp_Object arg = XCAR (args_left);
	      args_left = XCDR (args_left);
	      vals[argnum++] = eval_sub (arg);
	    }

	  set_backtrace_args (specpdl + count, vals, argnum);

	  val = XSUBR (fun)->function.aMANY (argnum, vals);

	  check_cons_list ();
	  lisp_eval_depth--;
	  /* Do the debug-on-exit now, while VALS still exists.  */
	  if (backtrace_debug_on_exit (specpdl + count))
	    val = call_debugger (list2 (Qexit, val));
	  SAFE_FREE ();
	  specpdl_ptr--;
	  return val;
	}
      else
	{
	  int i, maxargs = XSUBR (fun)->max_args;

	  for (i = 0; i < maxargs; i++)
	    {
	      argvals[i] = eval_sub (Fcar (args_left));
	      args_left = Fcdr (args_left);
	    }

	  set_backtrace_args (specpdl + count, argvals, XINT (numargs));

	  switch (i)
	    {
	    case 0:
	      val = (XSUBR (fun)->function.a0 ());
	      break;
	    case 1:
	      val = (XSUBR (fun)->function.a1 (argvals[0]));
	      break;
	    case 2:
	      val = (XSUBR (fun)->function.a2 (argvals[0], argvals[1]));
	      break;
	    case 3:
	      val = (XSUBR (fun)->function.a3
		     (argvals[0], argvals[1], argvals[2]));
	      break;
	    case 4:
	      val = (XSUBR (fun)->function.a4
		     (argvals[0], argvals[1], argvals[2], argvals[3]));
	      break;
	    case 5:
	      val = (XSUBR (fun)->function.a5
		     (argvals[0], argvals[1], argvals[2], argvals[3],
		      argvals[4]));
	      break;
	    case 6:
	      val = (XSUBR (fun)->function.a6
		     (argvals[0], argvals[1], argvals[2], argvals[3],
		      argvals[4], argvals[5]));
	      break;
	    case 7:
	      val = (XSUBR (fun)->function.a7
		     (argvals[0], argvals[1], argvals[2], argvals[3],
		      argvals[4], argvals[5], argvals[6]));
	      break;

	    case 8:
	      val = (XSUBR (fun)->function.a8
		     (argvals[0], argvals[1], argvals[2], argvals[3],
		      argvals[4], argvals[5], argvals[6], argvals[7]));
	      break;

	    default:
	      /* Someone has created a subr that takes more arguments than
		 is supported by this code.  We need to either rewrite the
		 subr to use a different argument protocol, or add more
		 cases to this switch.  */
	      emacs_abort ();
	    }
	}
    }
  else if (COMPILEDP (fun) || MODULE_FUNCTIONP (fun))
    return apply_lambda (fun, original_args, count);
  else
    {
      if (NILP (fun))
	xsignal1 (Qvoid_function, original_fun);
      if (!CONSP (fun))
	xsignal1 (Qinvalid_function, original_fun);
      funcar = XCAR (fun);
      if (!SYMBOLP (funcar))
	xsignal1 (Qinvalid_function, original_fun);
      if (EQ (funcar, Qautoload))
	{
	  Fautoload_do_load (fun, original_fun, Qnil);
	  goto retry;
	}
      if (EQ (funcar, Qmacro))
	{
	  ptrdiff_t count1 = SPECPDL_INDEX ();
	  Lisp_Object exp;
	  /* Bind lexical-binding during expansion of the macro, so the
	     macro can know reliably if the code it outputs will be
	     interpreted using lexical-binding or not.  */
	  specbind (Qlexical_binding,
		    NILP (Vinternal_interpreter_environment) ? Qnil : Qt);
	  exp = apply1 (Fcdr (fun), original_args);
	  unbind_to (count1, Qnil);
	  val = eval_sub (exp);
	}
      else if (EQ (funcar, Qlambda)
	       || EQ (funcar, Qclosure))
	return apply_lambda (fun, original_args, count);
      else
	xsignal1 (Qinvalid_function, original_fun);
    }
  check_cons_list ();

  lisp_eval_depth--;
  if (backtrace_debug_on_exit (specpdl + count))
    val = call_debugger (list2 (Qexit, val));
  specpdl_ptr--;

  return val;
}

DEFUN ("apply", Fapply, Sapply, 1, MANY, 0,
       doc: /* Call FUNCTION with our remaining args, using our last arg as list of args.
Then return the value FUNCTION returns.
Thus, (apply \\='+ 1 2 \\='(3 4)) returns 10.
usage: (apply FUNCTION &rest ARGUMENTS)  */)
  (ptrdiff_t nargs, Lisp_Object *args)
{
  ptrdiff_t i, numargs, funcall_nargs;
  register Lisp_Object *funcall_args = NULL;
  register Lisp_Object spread_arg = args[nargs - 1];
  Lisp_Object fun = args[0];
  Lisp_Object retval;
  USE_SAFE_ALLOCA;

  CHECK_LIST (spread_arg);

  numargs = XINT (Flength (spread_arg));

  if (numargs == 0)
    return Ffuncall (nargs - 1, args);
  else if (numargs == 1)
    {
      args [nargs - 1] = XCAR (spread_arg);
      return Ffuncall (nargs, args);
    }

  numargs += nargs - 2;

  /* Optimize for no indirection.  */
  if (SYMBOLP (fun) && !NILP (fun)
      && (fun = XSYMBOL (fun)->u.s.function, SYMBOLP (fun)))
    {
      fun = indirect_function (fun);
      if (NILP (fun))
	/* Let funcall get the error.  */
	fun = args[0];
    }

  if (SUBRP (fun) && XSUBR (fun)->max_args > numargs
      /* Don't hide an error by adding missing arguments.  */
      && numargs >= XSUBR (fun)->min_args)
    {
      /* Avoid making funcall cons up a yet another new vector of arguments
	 by explicitly supplying nil's for optional values.  */
      SAFE_ALLOCA_LISP (funcall_args, 1 + XSUBR (fun)->max_args);
      memclear (funcall_args + numargs + 1,
		(XSUBR (fun)->max_args - numargs) * word_size);
      funcall_nargs = 1 + XSUBR (fun)->max_args;
    }
  else
    { /* We add 1 to numargs because funcall_args includes the
	 function itself as well as its arguments.  */
      SAFE_ALLOCA_LISP (funcall_args, 1 + numargs);
      funcall_nargs = 1 + numargs;
    }

  memcpy (funcall_args, args, nargs * word_size);
  /* Spread the last arg we got.  Its first element goes in
     the slot that it used to occupy, hence this value of I.  */
  i = nargs - 1;
  while (!NILP (spread_arg))
    {
      funcall_args [i++] = XCAR (spread_arg);
      spread_arg = XCDR (spread_arg);
    }

  retval = Ffuncall (funcall_nargs, funcall_args);

  SAFE_FREE ();
  return retval;
}

/* Run hook variables in various ways.  */

/* ARGS[0] should be a hook symbol.
   Call each of the functions in the hook value, passing each of them
   as arguments all the rest of ARGS (all NARGS - 1 elements).
   FUNCALL specifies how to call each function on the hook.  */

Lisp_Object
run_hook_with_args (ptrdiff_t nargs, Lisp_Object *args,
		    Lisp_Object (*funcall) (ptrdiff_t nargs, Lisp_Object *args))
{
  Lisp_Object sym, val, ret = Qnil;

  /* If we are dying or still initializing,
     don't do anything--it would probably crash if we tried.  */
  if (NILP (Vrun_hooks))
    return Qnil;

  sym = args[0];
  val = find_symbol_value (sym);

  if (EQ (val, Qunbound) || NILP (val))
    return ret;
  else if (!CONSP (val) || FUNCTIONP (val))
    {
      args[0] = val;
      return funcall (nargs, args);
    }
  else
    {
      Lisp_Object global_vals = Qnil;

      for (;
	   CONSP (val) && NILP (ret);
	   val = XCDR (val))
	{
	  if (EQ (XCAR (val), Qt))
	    {
	      /* t indicates this hook has a local binding;
		 it means to run the global binding too.  */
	      global_vals = Fdefault_value (sym);
	      if (NILP (global_vals)) continue;

	      if (!CONSP (global_vals) || EQ (XCAR (global_vals), Qlambda))
		{
		  args[0] = global_vals;
		  ret = funcall (nargs, args);
		}
	      else
		{
		  for (;
		       CONSP (global_vals) && NILP (ret);
		       global_vals = XCDR (global_vals))
		    {
		      args[0] = XCAR (global_vals);
		      /* In a global value, t should not occur.  If it does, we
			 must ignore it to avoid an endless loop.  */
		      if (!EQ (args[0], Qt))
			ret = funcall (nargs, args);
		    }
		}
	    }
	  else
	    {
	      args[0] = XCAR (val);
	      ret = funcall (nargs, args);
	    }
	}

      return ret;
    }
}

/* Run the hook HOOK, giving each function the two args ARG1 and ARG2.  */

void
run_hook_with_args_2 (Lisp_Object hook, Lisp_Object arg1, Lisp_Object arg2)
{
  CALLN (Frun_hook_with_args, hook, arg1, arg2);
}

/* Call function fn on no arguments.  */
Lisp_Object
call0 (Lisp_Object fn)
{
  return Ffuncall (1, &fn);
}

/* Call function fn with 1 argument arg1.  */
/* ARGSUSED */
Lisp_Object
call1 (Lisp_Object fn, Lisp_Object arg1)
{
  return CALLN (Ffuncall, fn, arg1);
}

/* Call function fn with 2 arguments arg1, arg2.  */
/* ARGSUSED */
Lisp_Object
call2 (Lisp_Object fn, Lisp_Object arg1, Lisp_Object arg2)
{
  return CALLN (Ffuncall, fn, arg1, arg2);
}

/* Call function fn with 3 arguments arg1, arg2, arg3.  */
/* ARGSUSED */
Lisp_Object
call3 (Lisp_Object fn, Lisp_Object arg1, Lisp_Object arg2, Lisp_Object arg3)
{
  return CALLN (Ffuncall, fn, arg1, arg2, arg3);
}

/* Call function fn with 4 arguments arg1, arg2, arg3, arg4.  */
/* ARGSUSED */
Lisp_Object
call4 (Lisp_Object fn, Lisp_Object arg1, Lisp_Object arg2, Lisp_Object arg3,
       Lisp_Object arg4)
{
  return CALLN (Ffuncall, fn, arg1, arg2, arg3, arg4);
}

/* Call function fn with 5 arguments arg1, arg2, arg3, arg4, arg5.  */
/* ARGSUSED */
Lisp_Object
call5 (Lisp_Object fn, Lisp_Object arg1, Lisp_Object arg2, Lisp_Object arg3,
       Lisp_Object arg4, Lisp_Object arg5)
{
  return CALLN (Ffuncall, fn, arg1, arg2, arg3, arg4, arg5);
}

/* Call function fn with 6 arguments arg1, arg2, arg3, arg4, arg5, arg6.  */
/* ARGSUSED */
Lisp_Object
call6 (Lisp_Object fn, Lisp_Object arg1, Lisp_Object arg2, Lisp_Object arg3,
       Lisp_Object arg4, Lisp_Object arg5, Lisp_Object arg6)
{
  return CALLN (Ffuncall, fn, arg1, arg2, arg3, arg4, arg5, arg6);
}

/* Call function fn with 7 arguments arg1, arg2, arg3, arg4, arg5, arg6, arg7.  */
/* ARGSUSED */
Lisp_Object
call7 (Lisp_Object fn, Lisp_Object arg1, Lisp_Object arg2, Lisp_Object arg3,
       Lisp_Object arg4, Lisp_Object arg5, Lisp_Object arg6, Lisp_Object arg7)
{
  return CALLN (Ffuncall, fn, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
}

/* Call function fn with 8 arguments arg1, arg2, arg3, arg4, arg5,
   arg6, arg7, arg8.  */
/* ARGSUSED */
Lisp_Object
call8 (Lisp_Object fn, Lisp_Object arg1, Lisp_Object arg2, Lisp_Object arg3,
       Lisp_Object arg4, Lisp_Object arg5, Lisp_Object arg6, Lisp_Object arg7,
       Lisp_Object arg8)
{
  return CALLN (Ffuncall, fn, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);
}

/* Apply a C subroutine SUBR to the NUMARGS evaluated arguments in ARG_VECTOR
   and return the result of evaluation.  */

Lisp_Object
funcall_subr (struct Lisp_Subr *subr, ptrdiff_t numargs, Lisp_Object *args)
{
  if (numargs < subr->min_args
      || (subr->max_args >= 0 && subr->max_args < numargs))
    {
      Lisp_Object fun;
      XSETSUBR (fun, subr);
      xsignal2 (Qwrong_number_of_arguments, fun, make_number (numargs));
    }

  else if (subr->max_args == UNEVALLED)
    {
      Lisp_Object fun;
      XSETSUBR (fun, subr);
      xsignal1 (Qinvalid_function, fun);
    }

  else if (subr->max_args == MANY)
    return (subr->function.aMANY) (numargs, args);
  else
    {
      Lisp_Object internal_argbuf[8];
      Lisp_Object *internal_args;
      if (subr->max_args > numargs)
        {
          eassert (subr->max_args <= ARRAYELTS (internal_argbuf));
          internal_args = internal_argbuf;
          memcpy (internal_args, args, numargs * word_size);
          memclear (internal_args + numargs,
                    (subr->max_args - numargs) * word_size);
        }
      else
        internal_args = args;
      switch (subr->max_args)
        {
        case 0:
          return (subr->function.a0 ());
        case 1:
          return (subr->function.a1 (internal_args[0]));
        case 2:
          return (subr->function.a2
                  (internal_args[0], internal_args[1]));
        case 3:
          return (subr->function.a3
                  (internal_args[0], internal_args[1], internal_args[2]));
        case 4:
          return (subr->function.a4
                  (internal_args[0], internal_args[1], internal_args[2],
                   internal_args[3]));
        case 5:
          return (subr->function.a5
                  (internal_args[0], internal_args[1], internal_args[2],
                   internal_args[3], internal_args[4]));
        case 6:
          return (subr->function.a6
                  (internal_args[0], internal_args[1], internal_args[2],
                   internal_args[3], internal_args[4], internal_args[5]));
        case 7:
          return (subr->function.a7
                  (internal_args[0], internal_args[1], internal_args[2],
                   internal_args[3], internal_args[4], internal_args[5],
                   internal_args[6]));
        case 8:
          return (subr->function.a8
                  (internal_args[0], internal_args[1], internal_args[2],
                   internal_args[3], internal_args[4], internal_args[5],
                   internal_args[6], internal_args[7]));

        default:

          /* If a subr takes more than 8 arguments without using MANY
             or UNEVALLED, we need to extend this function to support it.
             Until this is done, there is no way to call the function.  */
          emacs_abort ();
        }
    }
}

static Lisp_Object
apply_lambda (Lisp_Object fun, Lisp_Object args, ptrdiff_t count)
{
  Lisp_Object args_left;
  ptrdiff_t i;
  EMACS_INT numargs;
  Lisp_Object *arg_vector;
  Lisp_Object tem;
  USE_SAFE_ALLOCA;

  numargs = XFASTINT (Flength (args));
  SAFE_ALLOCA_LISP (arg_vector, numargs);
  args_left = args;

  for (i = 0; i < numargs; )
    {
      tem = Fcar (args_left), args_left = Fcdr (args_left);
      tem = eval_sub (tem);
      arg_vector[i++] = tem;
    }

  set_backtrace_args (specpdl + count, arg_vector, i);
  tem = funcall_lambda (fun, numargs, arg_vector);

  check_cons_list ();
  lisp_eval_depth--;
  /* Do the debug-on-exit now, while arg_vector still exists.  */
  if (backtrace_debug_on_exit (specpdl + count))
    tem = call_debugger (list2 (Qexit, tem));
  SAFE_FREE ();
  specpdl_ptr--;
  return tem;
}

/* Apply a Lisp function FUN to the NARGS evaluated arguments in ARG_VECTOR
   and return the result of evaluation.
   FUN must be either a lambda-expression, a compiled-code object,
   or a module function.  */

Lisp_Object
funcall_lambda (Lisp_Object fun, ptrdiff_t nargs,
		register Lisp_Object *arg_vector)
{
  Lisp_Object val, syms_left, next, lexenv;
  ptrdiff_t count = SPECPDL_INDEX ();
  ptrdiff_t i;
  bool optional, rest;

  if (CONSP (fun))
    {
      if (EQ (XCAR (fun), Qclosure))
	{
	  Lisp_Object cdr = XCDR (fun);	/* Drop `closure'.  */
	  if (! CONSP (cdr))
	    xsignal1 (Qinvalid_function, fun);
	  fun = cdr;
	  lexenv = XCAR (fun);
	}
      else
	lexenv = Qnil;
      syms_left = XCDR (fun);
      if (CONSP (syms_left))
	syms_left = XCAR (syms_left);
      else
	xsignal1 (Qinvalid_function, fun);
    }
  else if (COMPILEDP (fun))
    {
      ptrdiff_t size = PVSIZE (fun);
      if (size <= COMPILED_STACK_DEPTH)
	xsignal1 (Qinvalid_function, fun);
      syms_left = AREF (fun, COMPILED_ARGLIST);
      if (INTEGERP (syms_left))
	/* A byte-code object with an integer args template means we
	   shouldn't bind any arguments, instead just call the byte-code
	   interpreter directly; it will push arguments as necessary.

	   Byte-code objects with a nil args template (the default)
	   have dynamically-bound arguments, and use the
	   argument-binding code below instead (as do all interpreted
	   functions, even lexically bound ones).  */
	{
	  /* If we have not actually read the bytecode string
	     and constants vector yet, fetch them from the file.  */
	  if (CONSP (AREF (fun, COMPILED_BYTECODE)))
	    Ffetch_bytecode (fun);
	  return exec_byte_code (AREF (fun, COMPILED_BYTECODE),
				 AREF (fun, COMPILED_CONSTANTS),
				 AREF (fun, COMPILED_STACK_DEPTH),
				 syms_left,
				 nargs, arg_vector);
	}
      lexenv = Qnil;
    }
#ifdef HAVE_MODULES
  else if (MODULE_FUNCTIONP (fun))
    return funcall_module (fun, nargs, arg_vector);
#endif
  else
    emacs_abort ();

  i = optional = rest = 0;
  bool previous_optional_or_rest = false;
  for (; CONSP (syms_left); syms_left = XCDR (syms_left))
    {
      maybe_quit ();

      next = XCAR (syms_left);
      if (!SYMBOLP (next))
	xsignal1 (Qinvalid_function, fun);

      if (EQ (next, Qand_rest))
        {
          if (rest || previous_optional_or_rest)
            xsignal1 (Qinvalid_function, fun);
          rest = 1;
          previous_optional_or_rest = true;
        }
      else if (EQ (next, Qand_optional))
        {
          if (optional || rest || previous_optional_or_rest)
            xsignal1 (Qinvalid_function, fun);
          optional = 1;
          previous_optional_or_rest = true;
        }
      else
	{
	  Lisp_Object arg;
	  if (rest)
	    {
	      arg = Flist (nargs - i, &arg_vector[i]);
	      i = nargs;
	    }
	  else if (i < nargs)
	    arg = arg_vector[i++];
	  else if (!optional)
	    xsignal2 (Qwrong_number_of_arguments, fun, make_number (nargs));
	  else
	    arg = Qnil;

	  /* Bind the argument.  */
	  if (!NILP (lexenv) && SYMBOLP (next))
	    /* Lexically bind NEXT by adding it to the lexenv alist.  */
	    lexenv = Fcons (Fcons (next, arg), lexenv);
	  else
	    /* Dynamically bind NEXT.  */
	    specbind (next, arg);
          previous_optional_or_rest = false;
	}
    }

  if (!NILP (syms_left) || previous_optional_or_rest)
    xsignal1 (Qinvalid_function, fun);
  else if (i < nargs)
    xsignal2 (Qwrong_number_of_arguments, fun, make_number (nargs));

  if (!EQ (lexenv, Vinternal_interpreter_environment))
    /* Instantiate a new lexical environment.  */
    specbind (Qinternal_interpreter_environment, lexenv);

  if (CONSP (fun))
    val = Fprogn (XCDR (XCDR (fun)));
  else
    {
      /* If we have not actually read the bytecode string
	 and constants vector yet, fetch them from the file.  */
      if (CONSP (AREF (fun, COMPILED_BYTECODE)))
	Ffetch_bytecode (fun);
      val = exec_byte_code (AREF (fun, COMPILED_BYTECODE),
			    AREF (fun, COMPILED_CONSTANTS),
			    AREF (fun, COMPILED_STACK_DEPTH),
			    Qnil, 0, 0);
    }

  return unbind_to (count, val);
}

DEFUN ("func-arity", Ffunc_arity, Sfunc_arity, 1, 1, 0,
       doc: /* Return minimum and maximum number of args allowed for FUNCTION.
FUNCTION must be a function of some kind.
The returned value is a cons cell (MIN . MAX).  MIN is the minimum number
of args.  MAX is the maximum number, or the symbol `many', for a
function with `&rest' args, or `unevalled' for a special form.  */)
  (Lisp_Object function)
{
  Lisp_Object original;
  Lisp_Object funcar;
  Lisp_Object result;

  original = function;

 retry:

  /* Optimize for no indirection.  */
  function = original;
  if (SYMBOLP (function) && !NILP (function))
    {
      function = XSYMBOL (function)->u.s.function;
      if (SYMBOLP (function))
	function = indirect_function (function);
    }

  if (CONSP (function) && EQ (XCAR (function), Qmacro))
    function = XCDR (function);

  if (SUBRP (function))
    result = Fsubr_arity (function);
  else if (COMPILEDP (function))
    result = lambda_arity (function);
#ifdef HAVE_MODULES
  else if (MODULE_FUNCTIONP (function))
    result = module_function_arity (XMODULE_FUNCTION (function));
#endif
  else
    {
      if (NILP (function))
	xsignal1 (Qvoid_function, original);
      if (!CONSP (function))
	xsignal1 (Qinvalid_function, original);
      funcar = XCAR (function);
      if (!SYMBOLP (funcar))
	xsignal1 (Qinvalid_function, original);
      if (EQ (funcar, Qlambda)
	  || EQ (funcar, Qclosure))
	result = lambda_arity (function);
      else if (EQ (funcar, Qautoload))
	{
	  Fautoload_do_load (function, original, Qnil);
	  goto retry;
	}
      else
	xsignal1 (Qinvalid_function, original);
    }
  return result;
}

/* FUN must be either a lambda-expression or a compiled-code object.  */
static Lisp_Object
lambda_arity (Lisp_Object fun)
{
  Lisp_Object syms_left;

  if (CONSP (fun))
    {
      if (EQ (XCAR (fun), Qclosure))
	{
	  fun = XCDR (fun);	/* Drop `closure'.  */
	  CHECK_CONS (fun);
	}
      syms_left = XCDR (fun);
      if (CONSP (syms_left))
	syms_left = XCAR (syms_left);
      else
	xsignal1 (Qinvalid_function, fun);
    }
  else if (COMPILEDP (fun))
    {
      ptrdiff_t size = PVSIZE (fun);
      if (size <= COMPILED_STACK_DEPTH)
	xsignal1 (Qinvalid_function, fun);
      syms_left = AREF (fun, COMPILED_ARGLIST);
      if (INTEGERP (syms_left))
        return get_byte_code_arity (syms_left);
    }
  else
    emacs_abort ();

  EMACS_INT minargs = 0, maxargs = 0;
  bool optional = false;
  for (; CONSP (syms_left); syms_left = XCDR (syms_left))
    {
      Lisp_Object next = XCAR (syms_left);
      if (!SYMBOLP (next))
	xsignal1 (Qinvalid_function, fun);

      if (EQ (next, Qand_rest))
	return Fcons (make_number (minargs), Qmany);
      else if (EQ (next, Qand_optional))
	optional = true;
      else
	{
          if (!optional)
            minargs++;
          maxargs++;
        }
    }

  if (!NILP (syms_left))
    xsignal1 (Qinvalid_function, fun);

  return Fcons (make_number (minargs), make_number (maxargs));
}

DEFUN ("fetch-bytecode", Ffetch_bytecode, Sfetch_bytecode,
       1, 1, 0,
       doc: /* If byte-compiled OBJECT is lazy-loaded, fetch it now.  */)
  (Lisp_Object object)
{
  Lisp_Object tem;

  if (COMPILEDP (object))
    {
      ptrdiff_t size = PVSIZE (object);
      if (size <= COMPILED_STACK_DEPTH)
	xsignal1 (Qinvalid_function, object);
      if (CONSP (AREF (object, COMPILED_BYTECODE)))
	{
	  tem = read_doc_string (AREF (object, COMPILED_BYTECODE));
	  if (!CONSP (tem))
	    {
	      tem = AREF (object, COMPILED_BYTECODE);
	      if (CONSP (tem) && STRINGP (XCAR (tem)))
		error ("Invalid byte code in %s", SDATA (XCAR (tem)));
	      else
		error ("Invalid byte code");
	    }
	  ASET (object, COMPILED_BYTECODE, XCAR (tem));
	  ASET (object, COMPILED_CONSTANTS, XCDR (tem));
	}
    }
  return object;
}

/* Return true if SYMBOL currently has a let-binding
   which was made in the buffer that is now current.  */

bool
let_shadows_buffer_binding_p (struct Lisp_Symbol *symbol)
{
  union specbinding *p;
  Lisp_Object buf = Fcurrent_buffer ();

  for (p = specpdl_ptr; p > specpdl; )
    if ((--p)->kind > SPECPDL_LET)
      {
	struct Lisp_Symbol *let_bound_symbol = XSYMBOL (specpdl_symbol (p));
	eassert (let_bound_symbol->u.s.redirect != SYMBOL_VARALIAS);
	if (symbol == let_bound_symbol
	    && EQ (specpdl_where (p), buf))
	  return 1;
      }

  return 0;
}

static void
do_specbind (struct Lisp_Symbol *sym, union specbinding *bind,
             Lisp_Object value, enum Set_Internal_Bind bindflag)
{
  switch (sym->u.s.redirect)
    {
    case SYMBOL_PLAINVAL:
      if (!sym->u.s.trapped_write)
	SET_SYMBOL_VAL (sym, value);
      else
        set_internal (specpdl_symbol (bind), value, Qnil, bindflag);
      break;

    case SYMBOL_FORWARDED:
      if (BUFFER_OBJFWDP (SYMBOL_FWD (sym))
	  && specpdl_kind (bind) == SPECPDL_LET_DEFAULT)
	{
          set_default_internal (specpdl_symbol (bind), value, bindflag);
	  return;
	}
      FALLTHROUGH;
    case SYMBOL_LOCALIZED:
      set_internal (specpdl_symbol (bind), value, Qnil, bindflag);
      break;

    default:
      emacs_abort ();
    }
}

/* `specpdl_ptr' describes which variable is
   let-bound, so it can be properly undone when we unbind_to.
   It can be either a plain SPECPDL_LET or a SPECPDL_LET_LOCAL/DEFAULT.
   - SYMBOL is the variable being bound.  Note that it should not be
     aliased (i.e. when let-binding V1 that's aliased to V2, we want
     to record V2 here).
   - WHERE tells us in which buffer the binding took place.
     This is used for SPECPDL_LET_LOCAL bindings (i.e. bindings to a
     buffer-local variable) as well as for SPECPDL_LET_DEFAULT bindings,
     i.e. bindings to the default value of a variable which can be
     buffer-local.  */

void
specbind (Lisp_Object symbol, Lisp_Object value)
{
  struct Lisp_Symbol *sym;

  CHECK_SYMBOL (symbol);
  sym = XSYMBOL (symbol);

 start:
  switch (sym->u.s.redirect)
    {
    case SYMBOL_VARALIAS:
      sym = indirect_variable (sym); XSETSYMBOL (symbol, sym); goto start;
    case SYMBOL_PLAINVAL:
      /* The most common case is that of a non-constant symbol with a
	 trivial value.  Make that as fast as we can.  */
      specpdl_ptr->let.kind = SPECPDL_LET;
      specpdl_ptr->let.symbol = symbol;
      specpdl_ptr->let.old_value = SYMBOL_VAL (sym);
      specpdl_ptr->let.saved_value = Qnil;
      grow_specpdl ();
      do_specbind (sym, specpdl_ptr - 1, value, SET_INTERNAL_BIND);
      break;
    case SYMBOL_LOCALIZED:
    case SYMBOL_FORWARDED:
      {
	Lisp_Object ovalue = find_symbol_value (symbol);
	specpdl_ptr->let.kind = SPECPDL_LET_LOCAL;
	specpdl_ptr->let.symbol = symbol;
	specpdl_ptr->let.old_value = ovalue;
	specpdl_ptr->let.where = Fcurrent_buffer ();
	specpdl_ptr->let.saved_value = Qnil;

	eassert (sym->u.s.redirect != SYMBOL_LOCALIZED
		 || (EQ (SYMBOL_BLV (sym)->where, Fcurrent_buffer ())));

	if (sym->u.s.redirect == SYMBOL_LOCALIZED)
	  {
	    if (!blv_found (SYMBOL_BLV (sym)))
	      specpdl_ptr->let.kind = SPECPDL_LET_DEFAULT;
	  }
	else if (BUFFER_OBJFWDP (SYMBOL_FWD (sym)))
	  {
	    /* If SYMBOL is a per-buffer variable which doesn't have a
	       buffer-local value here, make the `let' change the global
	       value by changing the value of SYMBOL in all buffers not
	       having their own value.  This is consistent with what
	       happens with other buffer-local variables.  */
	    if (NILP (Flocal_variable_p (symbol, Qnil)))
	      {
		specpdl_ptr->let.kind = SPECPDL_LET_DEFAULT;
		grow_specpdl ();
                do_specbind (sym, specpdl_ptr - 1, value, SET_INTERNAL_BIND);
		return;
	      }
	  }
	else
	  specpdl_ptr->let.kind = SPECPDL_LET;

	grow_specpdl ();
        do_specbind (sym, specpdl_ptr - 1, value, SET_INTERNAL_BIND);
	break;
      }
    default: emacs_abort ();
    }
}

/* Push unwind-protect entries of various types.  */

void
rebind_for_thread_switch (void)
{
  union specbinding *bind;

  for (bind = specpdl; bind != specpdl_ptr; ++bind)
    {
      if (bind->kind >= SPECPDL_LET)
	{
	  Lisp_Object value = specpdl_saved_value (bind);
	  Lisp_Object sym = specpdl_symbol (bind);
	  bind->let.saved_value = Qnil;
          do_specbind (XSYMBOL (sym), bind, value,
                       SET_INTERNAL_THREAD_SWITCH);
	}
    }
}

void
do_one_unbind (union specbinding *this_binding, bool unwinding,
               enum Set_Internal_Bind bindflag)
{
  eassert (unwinding || this_binding->kind >= SPECPDL_LET);
  switch (this_binding->kind)
    {
    case SPECPDL_UNWIND:
      this_binding->unwind.func (this_binding->unwind.arg);
      break;
    case SPECPDL_UNWIND_PTR:
      this_binding->unwind_ptr.func (this_binding->unwind_ptr.arg);
      break;
    case SPECPDL_UNWIND_INT:
      this_binding->unwind_int.func (this_binding->unwind_int.arg);
      break;
    case SPECPDL_UNWIND_VOID:
      this_binding->unwind_void.func ();
      break;
    case SPECPDL_BACKTRACE:
      break;
    case SPECPDL_LET:
      { /* If variable has a trivial value (no forwarding), and isn't
	   trapped, we can just set it.  */
	Lisp_Object sym = specpdl_symbol (this_binding);
	if (SYMBOLP (sym) && XSYMBOL (sym)->u.s.redirect == SYMBOL_PLAINVAL)
	  {
	    if (XSYMBOL (sym)->u.s.trapped_write == SYMBOL_UNTRAPPED_WRITE)
	      SET_SYMBOL_VAL (XSYMBOL (sym), specpdl_old_value (this_binding));
	    else
	      set_internal (sym, specpdl_old_value (this_binding),
                            Qnil, bindflag);
	    break;
	  }
      }
      /* Come here only if make_local_foo was used for the first time
	 on this var within this let.  */
      FALLTHROUGH;
    case SPECPDL_LET_DEFAULT:
      set_default_internal (specpdl_symbol (this_binding),
                            specpdl_old_value (this_binding),
                            bindflag);
      break;
    case SPECPDL_LET_LOCAL:
      {
	Lisp_Object symbol = specpdl_symbol (this_binding);
	Lisp_Object where = specpdl_where (this_binding);
	Lisp_Object old_value = specpdl_old_value (this_binding);
	eassert (BUFFERP (where));

	/* If this was a local binding, reset the value in the appropriate
	   buffer, but only if that buffer's binding still exists.  */
	if (!NILP (Flocal_variable_p (symbol, where)))
          set_internal (symbol, old_value, where, bindflag);
      }
      break;
    }
}

static void
do_nothing (void)
{}

/* Push an unwind-protect entry that does nothing, so that
   set_unwind_protect_ptr can overwrite it later.  */

void
record_unwind_protect_nothing (void)
{
  record_unwind_protect_void (do_nothing);
}

/* Clear the unwind-protect entry COUNT, so that it does nothing.
   It need not be at the top of the stack.  */

void
clear_unwind_protect (ptrdiff_t count)
{
  union specbinding *p = specpdl + count;
  p->unwind_void.kind = SPECPDL_UNWIND_VOID;
  p->unwind_void.func = do_nothing;
}

/* Set the unwind-protect entry COUNT so that it invokes FUNC (ARG).
   It need not be at the top of the stack.  Discard the entry's
   previous value without invoking it.  */

void
set_unwind_protect (ptrdiff_t count, void (*func) (Lisp_Object),
		    Lisp_Object arg)
{
  union specbinding *p = specpdl + count;
  p->unwind.kind = SPECPDL_UNWIND;
  p->unwind.func = func;
  p->unwind.arg = arg;
}

void
set_unwind_protect_ptr (ptrdiff_t count, void (*func) (void *), void *arg)
{
  union specbinding *p = specpdl + count;
  p->unwind_ptr.kind = SPECPDL_UNWIND_PTR;
  p->unwind_ptr.func = func;
  p->unwind_ptr.arg = arg;
}

void
unbind_for_thread_switch (struct thread_state *thr)
{
  union specbinding *bind;

  for (bind = thr->m_specpdl_ptr; bind > thr->m_specpdl;)
    {
      if ((--bind)->kind >= SPECPDL_LET)
	{
	  Lisp_Object sym = specpdl_symbol (bind);
	  bind->let.saved_value = find_symbol_value (sym);
          do_one_unbind (bind, false, SET_INTERNAL_THREAD_SWITCH);
	}
    }
}


static union specbinding *
get_backtrace_starting_at (Lisp_Object base)
{
  union specbinding *pdl = backtrace_top ();

  if (!NILP (base))
    { /* Skip up to `base'.  */
      base = Findirect_function (base, Qt);
      while (backtrace_p (pdl)
             && !EQ (base, Findirect_function (backtrace_function (pdl), Qt)))
        pdl = backtrace_next (pdl);
    }

  return pdl;
}

static union specbinding *
get_backtrace_frame (Lisp_Object nframes, Lisp_Object base)
{
  register EMACS_INT i;

  CHECK_NATNUM (nframes);
  union specbinding *pdl = get_backtrace_starting_at (base);

  /* Find the frame requested.  */
  for (i = XFASTINT (nframes); i > 0 && backtrace_p (pdl); i--)
    pdl = backtrace_next (pdl);

  return pdl;
}

static Lisp_Object
backtrace_frame_apply (Lisp_Object function, union specbinding *pdl)
{
  if (!backtrace_p (pdl))
    return Qnil;

  Lisp_Object flags = Qnil;
  if (backtrace_debug_on_exit (pdl))
    flags = Fcons (QCdebug_on_exit, Fcons (Qt, Qnil));

  if (backtrace_nargs (pdl) == UNEVALLED)
    return call4 (function, Qnil, backtrace_function (pdl), *backtrace_args (pdl), flags);
  else
    {
      Lisp_Object tem = Flist (backtrace_nargs (pdl), backtrace_args (pdl));
      return call4 (function, Qt, backtrace_function (pdl), tem, flags);
    }
}

DEFUN ("backtrace-debug", Fbacktrace_debug, Sbacktrace_debug, 2, 2, 0,
       doc: /* Set the debug-on-exit flag of eval frame LEVEL levels down to FLAG.
The debugger is entered when that frame exits, if the flag is non-nil.  */)
  (Lisp_Object level, Lisp_Object flag)
{
  CHECK_NUMBER (level);
  union specbinding *pdl = get_backtrace_frame(level, Qnil);

  if (backtrace_p (pdl))
    set_backtrace_debug_on_exit (pdl, !NILP (flag));

  return flag;
}

DEFUN ("mapbacktrace", Fmapbacktrace, Smapbacktrace, 1, 2, 0,
       doc: /* Call FUNCTION for each frame in backtrace.
If BASE is non-nil, it should be a function and iteration will start
from its nearest activation frame.
FUNCTION is called with 4 arguments: EVALD, FUNC, ARGS, and FLAGS.  If
a frame has not evaluated its arguments yet or is a special form,
EVALD is nil and ARGS is a list of forms.  If a frame has evaluated
its arguments and called its function already, EVALD is t and ARGS is
a list of values.
FLAGS is a plist of properties of the current frame: currently, the
only supported property is :debug-on-exit.  `mapbacktrace' always
returns nil.  */)
     (Lisp_Object function, Lisp_Object base)
{
  union specbinding *pdl = get_backtrace_starting_at (base);

  while (backtrace_p (pdl))
    {
      ptrdiff_t i = pdl - specpdl;
      backtrace_frame_apply (function, pdl);
      /* Beware! PDL is no longer valid here because FUNCTION might
         have caused grow_specpdl to reallocate pdlvec.  We must use
         the saved index, cf. Bug#27258.  */
      pdl = backtrace_next (&specpdl[i]);
    }

  return Qnil;
}

DEFUN ("backtrace-frame--internal", Fbacktrace_frame_internal,
       Sbacktrace_frame_internal, 3, 3, NULL,
       doc: /* Call FUNCTION on stack frame NFRAMES away from BASE.
Return the result of FUNCTION, or nil if no matching frame could be found. */)
     (Lisp_Object function, Lisp_Object nframes, Lisp_Object base)
{
  return backtrace_frame_apply (function, get_backtrace_frame (nframes, base));
}

/* For backtrace-eval, we want to temporarily unwind the last few elements of
   the specpdl stack, and then rewind them.  We store the pre-unwind values
   directly in the pre-existing specpdl elements (i.e. we swap the current
   value and the old value stored in the specpdl), kind of like the inplace
   pointer-reversal trick.  As it turns out, the rewind does the same as the
   unwind, except it starts from the other end of the specpdl stack, so we use
   the same function for both unwind and rewind.  */
static void
backtrace_eval_unrewind (int distance)
{
  union specbinding *tmp = specpdl_ptr;
  int step = -1;
  if (distance < 0)
    { /* It's a rewind rather than unwind.  */
      tmp += distance - 1;
      step = 1;
      distance = -distance;
    }

  for (; distance > 0; distance--)
    {
      tmp += step;
      switch (tmp->kind)
	{
	  /* FIXME: Ideally we'd like to "temporarily unwind" (some of) those
	     unwind_protect, but the problem is that we don't know how to
	     rewind them afterwards.  */
	case SPECPDL_UNWIND:
	  {
	    Lisp_Object oldarg = tmp->unwind.arg;
	    if (tmp->unwind.func == set_buffer_if_live)
	      tmp->unwind.arg = Fcurrent_buffer ();
	    else if (tmp->unwind.func == save_excursion_restore)
	      tmp->unwind.arg = save_excursion_save ();
	    else
	      break;
	    tmp->unwind.func (oldarg);
	    break;
	  }

	case SPECPDL_UNWIND_PTR:
	case SPECPDL_UNWIND_INT:
	case SPECPDL_UNWIND_VOID:
	case SPECPDL_BACKTRACE:
	  break;
	case SPECPDL_LET:
	  { /* If variable has a trivial value (no forwarding), we can
	       just set it.  No need to check for constant symbols here,
	       since that was already done by specbind.  */
	    Lisp_Object sym = specpdl_symbol (tmp);
	    if (SYMBOLP (sym)
		&& XSYMBOL (sym)->u.s.redirect == SYMBOL_PLAINVAL)
	      {
		Lisp_Object old_value = specpdl_old_value (tmp);
		set_specpdl_old_value (tmp, SYMBOL_VAL (XSYMBOL (sym)));
		SET_SYMBOL_VAL (XSYMBOL (sym), old_value);
		break;
	      }
	  }
	  /* Come here only if make_local_foo was used for the first
	     time on this var within this let.  */
	  FALLTHROUGH;
	case SPECPDL_LET_DEFAULT:
	  {
	    Lisp_Object sym = specpdl_symbol (tmp);
	    Lisp_Object old_value = specpdl_old_value (tmp);
	    set_specpdl_old_value (tmp, Fdefault_value (sym));
	    Fset_default (sym, old_value);
	  }
	  break;
	case SPECPDL_LET_LOCAL:
	  {
	    Lisp_Object symbol = specpdl_symbol (tmp);
	    Lisp_Object where = specpdl_where (tmp);
	    Lisp_Object old_value = specpdl_old_value (tmp);
	    eassert (BUFFERP (where));

	    /* If this was a local binding, reset the value in the appropriate
	       buffer, but only if that buffer's binding still exists.  */
	    if (!NILP (Flocal_variable_p (symbol, where)))
	      {
		set_specpdl_old_value
		  (tmp, Fbuffer_local_value (symbol, where));
                set_internal (symbol, old_value, where, SET_INTERNAL_UNBIND);
	      }
	  }
	  break;
	}
    }
}

DEFUN ("backtrace-eval", Fbacktrace_eval, Sbacktrace_eval, 2, 3, NULL,
       doc: /* Evaluate EXP in the context of some activation frame.
NFRAMES and BASE specify the activation frame to use, as in `backtrace-frame'.  */)
     (Lisp_Object exp, Lisp_Object nframes, Lisp_Object base)
{
  union specbinding *pdl = get_backtrace_frame (nframes, base);
  ptrdiff_t count = SPECPDL_INDEX ();
  ptrdiff_t distance = specpdl_ptr - pdl;
  eassert (distance >= 0);

  if (!backtrace_p (pdl))
    error ("Activation frame not found!");

  backtrace_eval_unrewind (distance);
  record_unwind_protect_int (backtrace_eval_unrewind, -distance);

  /* Use eval_sub rather than Feval since the main motivation behind
     backtrace-eval is to be able to get/set the value of lexical variables
     from the debugger.  */
  return unbind_to (count, eval_sub (exp));
}

DEFUN ("backtrace--locals", Fbacktrace__locals, Sbacktrace__locals, 1, 2, NULL,
       doc: /* Return names and values of local variables of a stack frame.
NFRAMES and BASE specify the activation frame to use, as in `backtrace-frame'.  */)
  (Lisp_Object nframes, Lisp_Object base)
{
  union specbinding *frame = get_backtrace_frame (nframes, base);
  union specbinding *prevframe
    = get_backtrace_frame (make_number (XFASTINT (nframes) - 1), base);
  ptrdiff_t distance = specpdl_ptr - frame;
  Lisp_Object result = Qnil;
  eassert (distance >= 0);

  if (!backtrace_p (prevframe))
    error ("Activation frame not found!");
  if (!backtrace_p (frame))
    error ("Activation frame not found!");

  /* The specpdl entries normally contain the symbol being bound along with its
     `old_value', so it can be restored.  The new value to which it is bound is
     available in one of two places: either in the current value of the
     variable (if it hasn't been rebound yet) or in the `old_value' slot of the
     next specpdl entry for it.
     `backtrace_eval_unrewind' happens to swap the role of `old_value'
     and "new value", so we abuse it here, to fetch the new value.
     It's ugly (we'd rather not modify global data) and a bit inefficient,
     but it does the job for now.  */
  backtrace_eval_unrewind (distance);

  /* Grab values.  */
  {
    union specbinding *tmp = prevframe;
    for (; tmp > frame; tmp--)
      {
	switch (tmp->kind)
	  {
	  case SPECPDL_LET:
	  case SPECPDL_LET_DEFAULT:
	  case SPECPDL_LET_LOCAL:
	    {
	      Lisp_Object sym = specpdl_symbol (tmp);
	      Lisp_Object val = specpdl_old_value (tmp);
	      if (EQ (sym, Qinternal_interpreter_environment))
		{
		  Lisp_Object env = val;
		  for (; CONSP (env); env = XCDR (env))
		    {
		      Lisp_Object binding = XCAR (env);
		      if (CONSP (binding))
			result = Fcons (Fcons (XCAR (binding),
					       XCDR (binding)),
					result);
		    }
		}
	      else
		result = Fcons (Fcons (sym, val), result);
	    }
	    break;

	  case SPECPDL_UNWIND:
	  case SPECPDL_UNWIND_PTR:
	  case SPECPDL_UNWIND_INT:
	  case SPECPDL_UNWIND_VOID:
	  case SPECPDL_BACKTRACE:
	    break;

	  default:
	    emacs_abort ();
	  }
      }
  }

  /* Restore values from specpdl to original place.  */
  backtrace_eval_unrewind (-distance);

  return result;
}


void
mark_specpdl (union specbinding *first, union specbinding *ptr)
{
  union specbinding *pdl;
  for (pdl = first; pdl != ptr; pdl++)
    {
      switch (pdl->kind)
	{
	case SPECPDL_UNWIND:
	  mark_object (specpdl_arg (pdl));
	  break;

	case SPECPDL_BACKTRACE:
	  {
	    ptrdiff_t nargs = backtrace_nargs (pdl);
	    mark_object (backtrace_function (pdl));
	    if (nargs == UNEVALLED)
	      nargs = 1;
	    while (nargs--)
	      mark_object (backtrace_args (pdl)[nargs]);
	  }
	  break;

	case SPECPDL_LET_DEFAULT:
	case SPECPDL_LET_LOCAL:
	  mark_object (specpdl_where (pdl));
	  FALLTHROUGH;
	case SPECPDL_LET:
	  mark_object (specpdl_symbol (pdl));
	  mark_object (specpdl_old_value (pdl));
	  mark_object (specpdl_saved_value (pdl));
	  break;

	case SPECPDL_UNWIND_PTR:
	case SPECPDL_UNWIND_INT:
	case SPECPDL_UNWIND_VOID:
	  break;

	default:
	  emacs_abort ();
	}
    }
}

void
get_backtrace (Lisp_Object array)
{
  union specbinding *pdl = backtrace_next (backtrace_top ());
  ptrdiff_t i = 0, asize = ASIZE (array);

  /* Copy the backtrace contents into working memory.  */
  for (; i < asize; i++)
    {
      if (backtrace_p (pdl))
	{
	  ASET (array, i, backtrace_function (pdl));
	  pdl = backtrace_next (pdl);
	}
      else
	ASET (array, i, Qnil);
    }
}

Lisp_Object backtrace_top_function (void)
{
  union specbinding *pdl = backtrace_top ();
  return (backtrace_p (pdl) ? backtrace_function (pdl) : Qnil);
}

void
syms_of_eval (void)
{
  DEFVAR_INT ("max-specpdl-size", max_specpdl_size,
	      doc: /* Limit on number of Lisp variable bindings and `unwind-protect's.
If Lisp code tries to increase the total number past this amount,
an error is signaled.
You can safely use a value considerably larger than the default value,
if that proves inconveniently small.  However, if you increase it too far,
Emacs could run out of memory trying to make the stack bigger.
Note that this limit may be silently increased by the debugger
if `debug-on-error' or `debug-on-quit' is set.  */);

  DEFVAR_INT ("max-lisp-eval-depth", max_lisp_eval_depth,
	      doc: /* Limit on depth in `eval', `apply' and `funcall' before error.

This limit serves to catch infinite recursions for you before they cause
actual stack overflow in C, which would be fatal for Emacs.
You can safely make it considerably larger than its default value,
if that proves inconveniently small.  However, if you increase it too far,
Emacs could overflow the real C stack, and crash.  */);

  DEFVAR_LISP ("quit-flag", Vquit_flag,
	       doc: /* Non-nil causes `eval' to abort, unless `inhibit-quit' is non-nil.
If the value is t, that means do an ordinary quit.
If the value equals `throw-on-input', that means quit by throwing
to the tag specified in `throw-on-input'; it's for handling `while-no-input'.
Typing C-g sets `quit-flag' to t, regardless of `inhibit-quit',
but `inhibit-quit' non-nil prevents anything from taking notice of that.  */);
  Vquit_flag = Qnil;

  DEFVAR_LISP ("inhibit-quit", Vinhibit_quit,
	       doc: /* Non-nil inhibits C-g quitting from happening immediately.
Note that `quit-flag' will still be set by typing C-g,
so a quit will be signaled as soon as `inhibit-quit' is nil.
To prevent this happening, set `quit-flag' to nil
before making `inhibit-quit' nil.  */);
  Vinhibit_quit = Qnil;

  DEFSYM (Qinhibit_quit, "inhibit-quit");
  DEFSYM (Qinhibit_debugger, "inhibit-debugger");
  DEFSYM (Qmacro, "macro");

  /* Note that the process handling also uses Qexit, but we don't want
     to staticpro it twice, so we just do it here.  */
  DEFSYM (Qexit, "exit");

  DEFSYM (Qinteractive, "interactive");
  DEFSYM (Qand_rest, "&rest");
  DEFSYM (Qand_optional, "&optional");
  DEFSYM (Qclosure, "closure");
  DEFSYM (QCdocumentation, ":documentation");
  DEFSYM (Qdebug, "debug");

  DEFVAR_LISP ("inhibit-debugger", Vinhibit_debugger,
	       doc: /* Non-nil means never enter the debugger.
Normally set while the debugger is already active, to avoid recursive
invocations.  */);
  Vinhibit_debugger = Qnil;

  DEFVAR_LISP ("debug-on-error", Vdebug_on_error,
	       doc: /* Non-nil means enter debugger if an error is signaled.
Does not apply to errors handled by `condition-case' or those
matched by `debug-ignored-errors'.
If the value is a list, an error only means to enter the debugger
if one of its condition symbols appears in the list.
When you evaluate an expression interactively, this variable
is temporarily non-nil if `eval-expression-debug-on-error' is non-nil.
The command `toggle-debug-on-error' toggles this.
See also the variable `debug-on-quit' and `inhibit-debugger'.  */);
  Vdebug_on_error = Qnil;

  DEFVAR_LISP ("debug-ignored-errors", Vdebug_ignored_errors,
    doc: /* List of errors for which the debugger should not be called.
Each element may be a condition-name or a regexp that matches error messages.
If any element applies to a given error, that error skips the debugger
and just returns to top level.
This overrides the variable `debug-on-error'.
It does not apply to errors handled by `condition-case'.  */);
  Vdebug_ignored_errors = Qnil;

  DEFVAR_BOOL ("debug-on-quit", debug_on_quit,
    doc: /* Non-nil means enter debugger if quit is signaled (C-g, for example).
Does not apply if quit is handled by a `condition-case'.  */);
  debug_on_quit = 0;

  DEFVAR_BOOL ("debug-on-next-call", debug_on_next_call,
	       doc: /* Non-nil means enter debugger before next `eval', `apply' or `funcall'.  */);

  DEFVAR_BOOL ("debugger-may-continue", debugger_may_continue,
	       doc: /* Non-nil means debugger may continue execution.
This is nil when the debugger is called under circumstances where it
might not be safe to continue.  */);
  debugger_may_continue = 1;

  DEFVAR_BOOL ("debugger-stack-frame-as-list", debugger_stack_frame_as_list,
	       doc: /* Non-nil means display call stack frames as lists. */);
  debugger_stack_frame_as_list = 0;

  DEFVAR_LISP ("debugger", Vdebugger,
	       doc: /* Function to call to invoke debugger.
If due to frame exit, args are `exit' and the value being returned;
 this function's value will be returned instead of that.
If due to error, args are `error' and a list of the args to `signal'.
If due to `apply' or `funcall' entry, one arg, `lambda'.
If due to `eval' entry, one arg, t.  */);
  Vdebugger = Qnil;

  DEFVAR_LISP ("signal-hook-function", Vsignal_hook_function,
	       doc: /* If non-nil, this is a function for `signal' to call.
It receives the same arguments that `signal' was given.
The Edebug package uses this to regain control.  */);
  Vsignal_hook_function = Qnil;

  DEFVAR_LISP ("debug-on-signal", Vdebug_on_signal,
	       doc: /* Non-nil means call the debugger regardless of condition handlers.
Note that `debug-on-error', `debug-on-quit' and friends
still determine whether to handle the particular condition.  */);
  Vdebug_on_signal = Qnil;

  /* When lexical binding is being used,
   Vinternal_interpreter_environment is non-nil, and contains an alist
   of lexically-bound variable, or (t), indicating an empty
   environment.  The lisp name of this variable would be
   `internal-interpreter-environment' if it weren't hidden.
   Every element of this list can be either a cons (VAR . VAL)
   specifying a lexical binding, or a single symbol VAR indicating
   that this variable should use dynamic scoping.  */
  DEFSYM (Qinternal_interpreter_environment,
	  "internal-interpreter-environment");
  DEFVAR_LISP ("internal-interpreter-environment",
		Vinternal_interpreter_environment,
	       doc: /* If non-nil, the current lexical environment of the lisp interpreter.
When lexical binding is not being used, this variable is nil.
A value of `(t)' indicates an empty environment, otherwise it is an
alist of active lexical bindings.  */);
  Vinternal_interpreter_environment = Qnil;
  /* Don't export this variable to Elisp, so no one can mess with it
     (Just imagine if someone makes it buffer-local).  */
  Funintern (Qinternal_interpreter_environment, Qnil);

  Vrun_hooks = intern_c_string ("run-hooks");
  staticpro (&Vrun_hooks);

  staticpro (&Vautoload_queue);
  Vautoload_queue = Qnil;
  staticpro (&Vsignaling_function);
  Vsignaling_function = Qnil;

  inhibit_lisp_code = Qnil;

  DEFSYM (Qcatch_all_memory_full, "catch-all-memory-full");
  Funintern (Qcatch_all_memory_full, Qnil);

  defsubr (&Sdefault_toplevel_value);
  defsubr (&Sset_default_toplevel_value);
  defsubr (&Sdefvar);
  defsubr (&Sdefvaralias);
  DEFSYM (Qdefvaralias, "defvaralias");
  defsubr (&Sthrow);
  defsubr (&Sapply);
  defsubr (&Sfunc_arity);
  defsubr (&Sfetch_bytecode);
  defsubr (&Sbacktrace_debug);
  DEFSYM (QCdebug_on_exit, ":debug-on-exit");
  defsubr (&Smapbacktrace);
  defsubr (&Sbacktrace_frame_internal);
  defsubr (&Sbacktrace_eval);
  defsubr (&Sbacktrace__locals);
}
