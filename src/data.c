/* Primitive operations on Lisp data types for GNU Emacs Lisp interpreter.
   Copyright (C) 1985-1986, 1988, 1993-1995, 1997-2018 Free Software
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


#include <config.h>

#include <math.h>
#include <stdio.h>

#include <byteswap.h>
#include <intprops.h>

#include "lisp.h"
#include "puresize.h"
#include "character.h"
#include "buffer.h"
#include "keyboard.h"
#include "process.h"
#include "frame.h"
#include "keymap.h"
#include "remacs-lib.h"

extern bool KBOARD_OBJFWDP (union Lisp_Fwd *a);
extern bool OBJFWDP (union Lisp_Fwd *a);

_Noreturn void wrong_range (Lisp_Object, Lisp_Object, Lisp_Object);

static void
set_blv_found (struct Lisp_Buffer_Local_Value *blv, int found)
{
  eassert (found == !EQ (blv->defcell, blv->valcell));
  blv->found = found;
}

static void
set_blv_value (struct Lisp_Buffer_Local_Value *blv, Lisp_Object val)
{
  XSETCDR (blv->valcell, val);
}

static void
set_blv_where (struct Lisp_Buffer_Local_Value *blv, Lisp_Object val)
{
  blv->where = val;
}

static void
set_blv_defcell (struct Lisp_Buffer_Local_Value *blv, Lisp_Object val)
{
  blv->defcell = val;
}

static void
set_blv_valcell (struct Lisp_Buffer_Local_Value *blv, Lisp_Object val)
{
  blv->valcell = val;
}

static _Noreturn void
wrong_length_argument (Lisp_Object a1, Lisp_Object a2, Lisp_Object a3)
{
  Lisp_Object size1 = make_number (bool_vector_size (a1));
  Lisp_Object size2 = make_number (bool_vector_size (a2));
  if (NILP (a3))
    xsignal2 (Qwrong_length_argument, size1, size2);
  else
    xsignal3 (Qwrong_length_argument, size1, size2,
	      make_number (bool_vector_size (a3)));
}

_Noreturn void
wrong_type_argument (register Lisp_Object predicate, register Lisp_Object value)
{
  /* If VALUE is not even a valid Lisp object, we'd want to abort here
     where we can get a backtrace showing where it came from.  We used
     to try and do that by checking the tagbits, but nowadays all
     tagbits are potentially valid.  */
  /* if ((unsigned int) XTYPE (value) >= Lisp_Type_Limit)
   *   emacs_abort (); */

  xsignal2 (Qwrong_type_argument, predicate, value);
}

void
pure_write_error (Lisp_Object obj)
{
  xsignal2 (Qerror, build_string ("Attempt to modify read-only object"), obj);
}

void
args_out_of_range (Lisp_Object a1, Lisp_Object a2)
{
  xsignal2 (Qargs_out_of_range, a1, a2);
}

void
args_out_of_range_3 (Lisp_Object a1, Lisp_Object a2, Lisp_Object a3)
{
  xsignal3 (Qargs_out_of_range, a1, a2, a3);
}



/* Data type predicates.  */

#ifdef HAVE_MODULES
DEFUN ("user-ptrp", Fuser_ptrp, Suser_ptrp, 1, 1, 0,
       doc: /* Return t if OBJECT is a module user pointer.  */)
     (Lisp_Object object)
{
  if (USER_PTRP (object))
    return Qt;
  return Qnil;
}
#endif

DEFUN ("module-function-p", Fmodule_function_p, Smodule_function_p, 1, 1, NULL,
       doc: /* Return t if OBJECT is a function loaded from a dynamic module.  */
       attributes: const)
  (Lisp_Object object)
{
  return MODULE_FUNCTIONP (object) ? Qt : Qnil;
}


/* Extract and set components of symbols.  */

DEFUN ("interactive-form", Finteractive_form, Sinteractive_form, 1, 1, 0,
       doc: /* Return the interactive form of CMD or nil if none.
If CMD is not a command, the return value is nil.
Value, if non-nil, is a list (interactive SPEC).  */)
  (Lisp_Object cmd)
{
  Lisp_Object fun = indirect_function (cmd); /* Check cycles.  */

  if (NILP (fun))
    return Qnil;

  /* Use an `interactive-form' property if present, analogous to the
     function-documentation property.  */
  fun = cmd;
  while (SYMBOLP (fun))
    {
      Lisp_Object tmp = Fget (fun, Qinteractive_form);
      if (!NILP (tmp))
	return tmp;
      else
	fun = Fsymbol_function (fun);
    }

  if (SUBRP (fun))
    {
      const char *spec = XSUBR (fun)->intspec;
      if (spec)
	return list2 (Qinteractive,
		      (*spec != '(') ? build_string (spec) :
		      Fcar (Fread_from_string (build_string (spec), Qnil, Qnil)));
    }
  else if (COMPILEDP (fun))
    {
      if (PVSIZE (fun) > COMPILED_INTERACTIVE)
	return list2 (Qinteractive, AREF (fun, COMPILED_INTERACTIVE));
    }
  else if (AUTOLOADP (fun))
    return Finteractive_form (Fautoload_do_load (fun, cmd, Qnil));
  else if (CONSP (fun))
    {
      Lisp_Object funcar = XCAR (fun);
      if (EQ (funcar, Qclosure))
	return Fassq (Qinteractive, Fcdr (Fcdr (XCDR (fun))));
      else if (EQ (funcar, Qlambda))
	return Fassq (Qinteractive, Fcdr (XCDR (fun)));
    }
  return Qnil;
}


/***********************************************************************
		Getting and Setting Values of Symbols
 ***********************************************************************/

/* Used to signal a user-friendly error when symbol WRONG is
   not a member of CHOICE, which should be a list of symbols.  */

extern void
wrong_choice (Lisp_Object choice, Lisp_Object wrong)
{
  ptrdiff_t i = 0, len = XINT (Flength (choice));
  Lisp_Object obj, *args;
  AUTO_STRING (one_of, "One of ");
  AUTO_STRING (comma, ", ");
  AUTO_STRING (or, " or ");
  AUTO_STRING (should_be_specified, " should be specified");

  USE_SAFE_ALLOCA;
  SAFE_ALLOCA_LISP (args, len * 2 + 1);

  args[i++] = one_of;

  for (obj = choice; !NILP (obj); obj = XCDR (obj))
    {
      args[i++] = SYMBOL_NAME (XCAR (obj));
      args[i++] = (NILP (XCDR (obj)) ? should_be_specified
		   : NILP (XCDR (XCDR (obj))) ? or : comma);
    }

  obj = Fconcat (i, args);
  SAFE_FREE ();
  xsignal2 (Qerror, obj, wrong);
}

/* Used to signal a user-friendly error if WRONG is not a number or
   integer/floating-point number outsize of inclusive MIN..MAX range.  */

_Noreturn extern void
wrong_range (Lisp_Object min, Lisp_Object max, Lisp_Object wrong)
{
  AUTO_STRING (value_should_be_from, "Value should be from ");
  AUTO_STRING (to, " to ");
  xsignal2 (Qerror,
	    CALLN (Fconcat, value_should_be_from, Fnumber_to_string (min),
		   to, Fnumber_to_string (max)),
	    wrong);
}

/* Set up SYMBOL to refer to its global binding.  This makes it safe
   to alter the status of other bindings.  BEWARE: this may be called
   during the mark phase of GC, where we assume that Lisp_Object slots
   of BLV are marked after this function has changed them.  */

void
swap_in_global_binding (struct Lisp_Symbol *symbol)
{
  struct Lisp_Buffer_Local_Value *blv = SYMBOL_BLV (symbol);

  /* Unload the previously loaded binding.  */
  if (blv->fwd)
    set_blv_value (blv, do_symval_forwarding (blv->fwd));

  /* Select the global binding in the symbol.  */
  set_blv_valcell (blv, blv->defcell);
  if (blv->fwd)
    store_symval_forwarding (blv->fwd, XCDR (blv->defcell), NULL);

  /* Indicate that the global binding is set up now.  */
  set_blv_where (blv, Qnil);
  set_blv_found (blv, 0);
}

/* Set up the buffer-local symbol SYMBOL for validity in the current buffer.
   VALCONTENTS is the contents of its value cell,
   which points to a struct Lisp_Buffer_Local_Value.

   Return the value forwarded one step past the buffer-local stage.
   This could be another forwarding pointer.  */

void
swap_in_symval_forwarding (struct Lisp_Symbol *symbol, struct Lisp_Buffer_Local_Value *blv)
{
  register Lisp_Object tem1;

  eassert (blv == SYMBOL_BLV (symbol));

  tem1 = blv->where;

  if (NILP (tem1)
      || current_buffer != XBUFFER (tem1))
    {

      /* Unload the previously loaded binding.  */
      tem1 = blv->valcell;
      if (blv->fwd)
	set_blv_value (blv, do_symval_forwarding (blv->fwd));
      /* Choose the new binding.  */
      {
	Lisp_Object var;
	XSETSYMBOL (var, symbol);
	tem1 = assq_no_quit (var, BVAR (current_buffer, local_var_alist));
	set_blv_where (blv, Fcurrent_buffer ());
      }
      if (!(blv->found = !NILP (tem1)))
	tem1 = blv->defcell;

      /* Load the new binding.  */
      set_blv_valcell (blv, tem1);
      if (blv->fwd)
	store_symval_forwarding (blv->fwd, get_blv_value (blv), NULL);
    }
}


/* Store the value NEWVAL into SYMBOL.
   If buffer-locality is an issue, WHERE specifies which context to use.
   (nil stands for the current buffer/frame).

   If BINDFLAG is SET_INTERNAL_SET, then if this symbol is supposed to
   become local in every buffer where it is set, then we make it
   local.  If BINDFLAG is SET_INTERNAL_BIND or SET_INTERNAL_UNBIND, we
   don't do that.  */

void
set_internal (Lisp_Object symbol, Lisp_Object newval, Lisp_Object where,
              enum Set_Internal_Bind bindflag)
{
  bool voide = EQ (newval, Qunbound);
  struct Lisp_Symbol *sym;
  Lisp_Object tem1;

  /* If restoring in a dead buffer, do nothing.  */
  /* if (BUFFERP (where) && NILP (XBUFFER (where)->name))
      return; */

  CHECK_SYMBOL (symbol);
  sym = XSYMBOL (symbol);
  switch (sym->u.s.trapped_write)
    {
    case SYMBOL_NOWRITE:
      if (NILP (Fkeywordp (symbol))
          || !EQ (newval, Fsymbol_value (symbol)))
        xsignal1 (Qsetting_constant, symbol);
      else
        /* Allow setting keywords to their own value.  */
        return;

    case SYMBOL_TRAPPED_WRITE:
      /* Setting due to thread-switching doesn't count.  */
      if (bindflag != SET_INTERNAL_THREAD_SWITCH)
        notify_variable_watchers (symbol, voide? Qnil : newval,
                                  (bindflag == SET_INTERNAL_BIND? Qlet :
                                   bindflag == SET_INTERNAL_UNBIND? Qunlet :
                                   voide? Qmakunbound : Qset),
                                  where);
      /* FALLTHROUGH!  */
    case SYMBOL_UNTRAPPED_WRITE:
        break;

    default: emacs_abort ();
    }

 start:
  switch (sym->u.s.redirect)
    {
    case SYMBOL_VARALIAS: sym = indirect_variable (sym); goto start;
    case SYMBOL_PLAINVAL: SET_SYMBOL_VAL (sym , newval); return;
    case SYMBOL_LOCALIZED:
      {
	struct Lisp_Buffer_Local_Value *blv = SYMBOL_BLV (sym);
	if (NILP (where))
	  XSETBUFFER (where, current_buffer);

	/* If the current buffer is not the buffer whose binding is
	   loaded, or if it's a Lisp_Buffer_Local_Value and
	   the default binding is loaded, the loaded binding may be the
	   wrong one.  */
	if (!EQ (blv->where, where)
	    /* Also unload a global binding (if the var is local_if_set).  */
	    || (EQ (blv->valcell, blv->defcell)))
	  {
	    /* The currently loaded binding is not necessarily valid.
	       We need to unload it, and choose a new binding.  */

	    /* Write out `realvalue' to the old loaded binding.  */
	    if (blv->fwd)
	      set_blv_value (blv, do_symval_forwarding (blv->fwd));

	    /* Find the new binding.  */
	    XSETSYMBOL (symbol, sym); /* May have changed via aliasing.  */
	    tem1 = assq_no_quit (symbol,
				 BVAR (XBUFFER (where), local_var_alist));
	    set_blv_where (blv, where);
	    blv->found = 1;

	    if (NILP (tem1))
	      {
		/* This buffer still sees the default value.  */

		/* If the variable is a Lisp_Some_Buffer_Local_Value,
		   or if this is `let' rather than `set',
		   make CURRENT-ALIST-ELEMENT point to itself,
		   indicating that we're seeing the default value.
		   Likewise if the variable has been let-bound
		   in the current buffer.  */
		if (bindflag || !blv->local_if_set
		    || let_shadows_buffer_binding_p (sym))
		  {
		    blv->found = 0;
		    tem1 = blv->defcell;
		  }
		/* If it's a local_if_set, being set not bound,
		   and we're not within a let that was made for this buffer,
		   create a new buffer-local binding for the variable.
		   That means, give this buffer a new assoc for a local value
		   and load that binding.  */
		else
		  {
		    tem1 = Fcons (symbol, XCDR (blv->defcell));
		    bset_local_var_alist
		      (XBUFFER (where),
		       Fcons (tem1, BVAR (XBUFFER (where), local_var_alist)));
		  }
	      }

	    /* Record which binding is now loaded.  */
	    set_blv_valcell (blv, tem1);
	  }

	/* Store the new value in the cons cell.  */
	set_blv_value (blv, newval);

	if (blv->fwd)
	  {
	    if (voide)
	      /* If storing void (making the symbol void), forward only through
		 buffer-local indicator, not through Lisp_Objfwd, etc.  */
	      blv->fwd = NULL;
	    else
	      store_symval_forwarding (blv->fwd, newval,
				       BUFFERP (where)
				       ? XBUFFER (where) : current_buffer);
	  }
	break;
      }
    case SYMBOL_FORWARDED:
      {
	struct buffer *buf
	  = BUFFERP (where) ? XBUFFER (where) : current_buffer;
	union Lisp_Fwd *innercontents = SYMBOL_FWD (sym);
	if (BUFFER_OBJFWDP (innercontents))
	  {
	    int offset = XBUFFER_OBJFWD (innercontents)->offset;
	    int idx = PER_BUFFER_IDX (offset);
	    if (idx > 0
                && bindflag == SET_INTERNAL_SET
		&& !let_shadows_buffer_binding_p (sym))
	      SET_PER_BUFFER_VALUE_P (buf, idx, 1);
	  }

	if (voide)
	  { /* If storing void (making the symbol void), forward only through
	       buffer-local indicator, not through Lisp_Objfwd, etc.  */
	    sym->u.s.redirect = SYMBOL_PLAINVAL;
	    SET_SYMBOL_VAL (sym, newval);
	  }
	else
	  store_symval_forwarding (/* sym, */ innercontents, newval, buf);
	break;
      }
    default: emacs_abort ();
    }
  return;
}

static void
set_symbol_trapped_write (Lisp_Object symbol, enum symbol_trapped_write trap)
{
  struct Lisp_Symbol *sym = XSYMBOL (symbol);
  if (sym->u.s.trapped_write == SYMBOL_NOWRITE)
    xsignal1 (Qtrapping_constant, symbol);
  sym->u.s.trapped_write = trap;
}

static void
restore_symbol_trapped_write (Lisp_Object symbol)
{
  set_symbol_trapped_write (symbol, SYMBOL_TRAPPED_WRITE);
}

void
notify_variable_watchers (Lisp_Object symbol,
                          Lisp_Object newval,
                          Lisp_Object operation,
                          Lisp_Object where)
{
  symbol = Findirect_variable (symbol);

  ptrdiff_t count = SPECPDL_INDEX ();
  record_unwind_protect (restore_symbol_trapped_write, symbol);
  /* Avoid recursion.  */
  set_symbol_trapped_write (symbol, SYMBOL_UNTRAPPED_WRITE);

  if (NILP (where)
      && !EQ (operation, Qset_default) && !EQ (operation, Qmakunbound)
      && !NILP (Flocal_variable_if_set_p (symbol, Fcurrent_buffer ())))
    {
      XSETBUFFER (where, current_buffer);
    }

  if (EQ (operation, Qset_default))
    operation = Qset;

  for (Lisp_Object watchers = Fget (symbol, Qwatchers);
       CONSP (watchers);
       watchers = XCDR (watchers))
    {
      Lisp_Object watcher = XCAR (watchers);
      /* Call subr directly to avoid gc.  */
      if (SUBRP (watcher))
        {
          Lisp_Object args[] = { symbol, newval, operation, where };
          funcall_subr (XSUBR (watcher), ARRAYELTS (args), args);
        }
      else
        CALLN (Ffuncall, watcher, symbol, newval, operation, where);
    }

  unbind_to (count, Qnil);
}


/* Access or set a buffer-local symbol's default value.  */

void
set_default_internal (Lisp_Object symbol, Lisp_Object value,
                      enum Set_Internal_Bind bindflag)
{
  struct Lisp_Symbol *sym;

  CHECK_SYMBOL (symbol);
  sym = XSYMBOL (symbol);
  switch (sym->u.s.trapped_write)
    {
    case SYMBOL_NOWRITE:
      if (NILP (Fkeywordp (symbol))
          || !EQ (value, Fsymbol_value (symbol)))
        xsignal1 (Qsetting_constant, symbol);
      else
        /* Allow setting keywords to their own value.  */
        return;

    case SYMBOL_TRAPPED_WRITE:
      /* Don't notify here if we're going to call Fset anyway.  */
      if (sym->u.s.redirect != SYMBOL_PLAINVAL
          /* Setting due to thread switching doesn't count.  */
          && bindflag != SET_INTERNAL_THREAD_SWITCH)
        notify_variable_watchers (symbol, value, Qset_default, Qnil);
      /* FALLTHROUGH!  */
    case SYMBOL_UNTRAPPED_WRITE:
        break;

    default: emacs_abort ();
    }

 start:
  switch (sym->u.s.redirect)
    {
    case SYMBOL_VARALIAS: sym = indirect_variable (sym); goto start;
    case SYMBOL_PLAINVAL: set_internal (symbol, value, Qnil, bindflag); return;
    case SYMBOL_LOCALIZED:
      {
	struct Lisp_Buffer_Local_Value *blv = SYMBOL_BLV (sym);

	/* Store new value into the DEFAULT-VALUE slot.  */
	XSETCDR (blv->defcell, value);

	/* If the default binding is now loaded, set the REALVALUE slot too.  */
	if (blv->fwd && EQ (blv->defcell, blv->valcell))
	  store_symval_forwarding (blv->fwd, value, NULL);
        return;
      }
    case SYMBOL_FORWARDED:
      {
	union Lisp_Fwd *valcontents = SYMBOL_FWD (sym);

	/* Handle variables like case-fold-search that have special slots
	   in the buffer.
	   Make them work apparently like Lisp_Buffer_Local_Value variables.  */
	if (BUFFER_OBJFWDP (valcontents))
	  {
	    int offset = XBUFFER_OBJFWD (valcontents)->offset;
	    int idx = PER_BUFFER_IDX (offset);

	    set_per_buffer_default (offset, value);

	    /* If this variable is not always local in all buffers,
	       set it in the buffers that don't nominally have a local value.  */
	    if (idx > 0)
	      {
		struct buffer *b;

		FOR_EACH_BUFFER (b)
		  if (!PER_BUFFER_VALUE_P (b, idx))
		    set_per_buffer_value (b, offset, value);
	      }
	  }
	else
          set_internal (symbol, value, Qnil, bindflag);
        return;
      }
    default: emacs_abort ();
    }
}

DEFUN ("setq-default", Fsetq_default, Ssetq_default, 0, UNEVALLED, 0,
       doc: /* Set the default value of variable VAR to VALUE.
VAR, the variable name, is literal (not evaluated);
VALUE is an expression: it is evaluated and its value returned.
The default value of a variable is seen in buffers
that do not have their own values for the variable.

More generally, you can use multiple variables and values, as in
  (setq-default VAR VALUE VAR VALUE...)
This sets each VAR's default value to the corresponding VALUE.
The VALUE for the Nth VAR can refer to the new default values
of previous VARs.
usage: (setq-default [VAR VALUE]...)  */)
  (Lisp_Object args)
{
  Lisp_Object args_left, symbol, val;

  args_left = val = args;

  while (CONSP (args_left))
    {
      val = eval_sub (Fcar (XCDR (args_left)));
      symbol = XCAR (args_left);
      Fset_default (symbol, val);
      args_left = Fcdr (XCDR (args_left));
    }

  return val;
}

/* Lisp functions for creating and removing buffer-local variables.  */

union Lisp_Val_Fwd
  {
    Lisp_Object value;
    union Lisp_Fwd *fwd;
  };

static struct Lisp_Buffer_Local_Value *
make_blv (struct Lisp_Symbol *sym, bool forwarded,
	  union Lisp_Val_Fwd valcontents)
{
  struct Lisp_Buffer_Local_Value *blv = xmalloc (sizeof *blv);
  Lisp_Object symbol;
  Lisp_Object tem;

 XSETSYMBOL (symbol, sym);
 tem = Fcons (symbol, (forwarded
                       ? do_symval_forwarding (valcontents.fwd)
                       : valcontents.value));

  /* Buffer_Local_Values cannot have as realval a buffer-local
     or keyboard-local forwarding.  */
  eassert (!(forwarded && BUFFER_OBJFWDP (valcontents.fwd)));
  eassert (!(forwarded && KBOARD_OBJFWDP (valcontents.fwd)));
  blv->fwd = forwarded ? valcontents.fwd : NULL;
  set_blv_where (blv, Qnil);
  blv->local_if_set = 0;
  set_blv_defcell (blv, tem);
  set_blv_valcell (blv, tem);
  set_blv_found (blv, 0);
  return blv;
}

DEFUN ("make-variable-buffer-local", Fmake_variable_buffer_local,
       Smake_variable_buffer_local, 1, 1, "vMake Variable Buffer Local: ",
       doc: /* Make VARIABLE become buffer-local whenever it is set.
At any time, the value for the current buffer is in effect,
unless the variable has never been set in this buffer,
in which case the default value is in effect.
Note that binding the variable with `let', or setting it while
a `let'-style binding made in this buffer is in effect,
does not make the variable buffer-local.  Return VARIABLE.

This globally affects all uses of this variable, so it belongs together with
the variable declaration, rather than with its uses (if you just want to make
a variable local to the current buffer for one particular use, use
`make-local-variable').  Buffer-local bindings are normally cleared
while setting up a new major mode, unless they have a `permanent-local'
property.

The function `default-value' gets the default value and `set-default' sets it.  */)
  (register Lisp_Object variable)
{
  struct Lisp_Symbol *sym;
  struct Lisp_Buffer_Local_Value *blv = NULL;
  union Lisp_Val_Fwd valcontents;
  bool forwarded UNINIT;

  CHECK_SYMBOL (variable);
  sym = XSYMBOL (variable);

 start:
  switch (sym->u.s.redirect)
    {
    case SYMBOL_VARALIAS: sym = indirect_variable (sym); goto start;
    case SYMBOL_PLAINVAL:
      forwarded = 0; valcontents.value = SYMBOL_VAL (sym);
      if (EQ (valcontents.value, Qunbound))
	valcontents.value = Qnil;
      break;
    case SYMBOL_LOCALIZED:
      blv = SYMBOL_BLV (sym);
      break;
    case SYMBOL_FORWARDED:
      forwarded = 1; valcontents.fwd = SYMBOL_FWD (sym);
      if (KBOARD_OBJFWDP (valcontents.fwd))
	error ("Symbol %s may not be buffer-local",
	       SDATA (SYMBOL_NAME (variable)));
      else if (BUFFER_OBJFWDP (valcontents.fwd))
	return variable;
      break;
    default: emacs_abort ();
    }

  if (SYMBOL_CONSTANT_P (variable))
    xsignal1 (Qsetting_constant, variable);

  if (!blv)
    {
      blv = make_blv (sym, forwarded, valcontents);
      sym->u.s.redirect = SYMBOL_LOCALIZED;
      SET_SYMBOL_BLV (sym, blv);
    }

  blv->local_if_set = 1;
  return variable;
}

DEFUN ("make-local-variable", Fmake_local_variable, Smake_local_variable,
       1, 1, "vMake Local Variable: ",
       doc: /* Make VARIABLE have a separate value in the current buffer.
Other buffers will continue to share a common default value.
\(The buffer-local value of VARIABLE starts out as the same value
VARIABLE previously had.  If VARIABLE was void, it remains void.)
Return VARIABLE.

If the variable is already arranged to become local when set,
this function causes a local value to exist for this buffer,
just as setting the variable would do.

This function returns VARIABLE, and therefore
  (set (make-local-variable \\='VARIABLE) VALUE-EXP)
works.

See also `make-variable-buffer-local'.

Do not use `make-local-variable' to make a hook variable buffer-local.
Instead, use `add-hook' and specify t for the LOCAL argument.  */)
  (Lisp_Object variable)
{
  Lisp_Object tem;
  bool forwarded UNINIT;
  union Lisp_Val_Fwd valcontents;
  struct Lisp_Symbol *sym;
  struct Lisp_Buffer_Local_Value *blv = NULL;

  CHECK_SYMBOL (variable);
  sym = XSYMBOL (variable);

 start:
  switch (sym->u.s.redirect)
    {
    case SYMBOL_VARALIAS: sym = indirect_variable (sym); goto start;
    case SYMBOL_PLAINVAL:
      forwarded = 0; valcontents.value = SYMBOL_VAL (sym); break;
    case SYMBOL_LOCALIZED:
      blv = SYMBOL_BLV (sym);
      break;
    case SYMBOL_FORWARDED:
      forwarded = 1; valcontents.fwd = SYMBOL_FWD (sym);
      if (KBOARD_OBJFWDP (valcontents.fwd))
	error ("Symbol %s may not be buffer-local",
	       SDATA (SYMBOL_NAME (variable)));
      break;
    default: emacs_abort ();
    }

  if (sym->u.s.trapped_write == SYMBOL_NOWRITE)
    xsignal1 (Qsetting_constant, variable);

  if (blv ? blv->local_if_set
      : (forwarded && BUFFER_OBJFWDP (valcontents.fwd)))
    {
      tem = Fboundp (variable);
      /* Make sure the symbol has a local value in this particular buffer,
	 by setting it to the same value it already has.  */
      Fset (variable, (EQ (tem, Qt) ? Fsymbol_value (variable) : Qunbound));
      return variable;
    }
  if (!blv)
    {
      blv = make_blv (sym, forwarded, valcontents);
      sym->u.s.redirect = SYMBOL_LOCALIZED;
      SET_SYMBOL_BLV (sym, blv);
    }

  /* Make sure this buffer has its own value of symbol.  */
  XSETSYMBOL (variable, sym);	/* Update in case of aliasing.  */
  tem = Fassq (variable, BVAR (current_buffer, local_var_alist));
  if (NILP (tem))
    {
      if (let_shadows_buffer_binding_p (sym))
	{
	  AUTO_STRING (format,
		       "Making %s buffer-local while locally let-bound!");
	  CALLN (Fmessage, format, SYMBOL_NAME (variable));
	}

      /* Swap out any local binding for some other buffer, and make
	 sure the current value is permanently recorded, if it's the
	 default value.  */
      find_symbol_value (variable);

      bset_local_var_alist
	(current_buffer,
	 Fcons (Fcons (variable, XCDR (blv->defcell)),
		BVAR (current_buffer, local_var_alist)));

      /* Make sure symbol does not think it is set up for this buffer;
	 force it to look once again for this buffer's value.  */
      if (current_buffer == XBUFFER (blv->where))
	set_blv_where (blv, Qnil);
      set_blv_found (blv, 0);
    }

  /* If the symbol forwards into a C variable, then load the binding
     for this buffer now.  If C code modifies the variable before we
     load the binding in, then that new value will clobber the default
     binding the next time we unload it.  */
  if (blv->fwd)
    swap_in_symval_forwarding (sym, blv);

  return variable;
}

DEFUN ("kill-local-variable", Fkill_local_variable, Skill_local_variable,
       1, 1, "vKill Local Variable: ",
       doc: /* Make VARIABLE no longer have a separate value in the current buffer.
From now on the default value will apply in this buffer.  Return VARIABLE.  */)
  (register Lisp_Object variable)
{
  register Lisp_Object tem;
  struct Lisp_Buffer_Local_Value *blv;
  struct Lisp_Symbol *sym;

  CHECK_SYMBOL (variable);
  sym = XSYMBOL (variable);

 start:
  switch (sym->u.s.redirect)
    {
    case SYMBOL_VARALIAS: sym = indirect_variable (sym); goto start;
    case SYMBOL_PLAINVAL: return variable;
    case SYMBOL_FORWARDED:
      {
	union Lisp_Fwd *valcontents = SYMBOL_FWD (sym);
	if (BUFFER_OBJFWDP (valcontents))
	  {
	    int offset = XBUFFER_OBJFWD (valcontents)->offset;
	    int idx = PER_BUFFER_IDX (offset);

	    if (idx > 0)
	      {
		SET_PER_BUFFER_VALUE_P (current_buffer, idx, 0);
		set_per_buffer_value (current_buffer, offset,
				      per_buffer_default (offset));
	      }
	  }
	return variable;
      }
    case SYMBOL_LOCALIZED:
      blv = SYMBOL_BLV (sym);
      break;
    default: emacs_abort ();
    }

  if (sym->u.s.trapped_write == SYMBOL_TRAPPED_WRITE)
    notify_variable_watchers (variable, Qnil, Qmakunbound, Fcurrent_buffer ());

  /* Get rid of this buffer's alist element, if any.  */
  XSETSYMBOL (variable, sym);	/* Propagate variable indirection.  */
  tem = Fassq (variable, BVAR (current_buffer, local_var_alist));
  if (!NILP (tem))
    bset_local_var_alist
      (current_buffer,
       Fdelq (tem, BVAR (current_buffer, local_var_alist)));

  /* If the symbol is set up with the current buffer's binding
     loaded, recompute its value.  We have to do it now, or else
     forwarded objects won't work right.  */
  {
    Lisp_Object buf; XSETBUFFER (buf, current_buffer);
    if (EQ (buf, blv->where))
      {
	set_blv_where (blv, Qnil);
	blv->found = 0;
	find_symbol_value (variable);
      }
  }

  return variable;
}

/* Lisp functions for creating and removing buffer-local variables.  */

DEFUN ("local-variable-if-set-p", Flocal_variable_if_set_p, Slocal_variable_if_set_p,
       1, 2, 0,
       doc: /* Non-nil if VARIABLE is local in buffer BUFFER when set there.
BUFFER defaults to the current buffer.

More precisely, return non-nil if either VARIABLE already has a local
value in BUFFER, or if VARIABLE is automatically buffer-local (see
`make-variable-buffer-local').  */)
  (register Lisp_Object variable, Lisp_Object buffer)
{
  struct Lisp_Symbol *sym;

  CHECK_SYMBOL (variable);
  sym = XSYMBOL (variable);

 start:
  switch (sym->u.s.redirect)
    {
    case SYMBOL_VARALIAS: sym = indirect_variable (sym); goto start;
    case SYMBOL_PLAINVAL: return Qnil;
    case SYMBOL_LOCALIZED:
      {
	struct Lisp_Buffer_Local_Value *blv = SYMBOL_BLV (sym);
	if (blv->local_if_set)
	  return Qt;
	XSETSYMBOL (variable, sym); /* Update in case of aliasing.  */
	return Flocal_variable_p (variable, buffer);
      }
    case SYMBOL_FORWARDED:
      /* All BUFFER_OBJFWD slots become local if they are set.  */
      return (BUFFER_OBJFWDP (SYMBOL_FWD (sym)) ? Qt : Qnil);
    default: emacs_abort ();
    }
}

DEFUN ("variable-binding-locus", Fvariable_binding_locus, Svariable_binding_locus,
       1, 1, 0,
       doc: /* Return a value indicating where VARIABLE's current binding comes from.
If the current binding is buffer-local, the value is the current buffer.
If the current binding is global (the default), the value is nil.  */)
  (register Lisp_Object variable)
{
  struct Lisp_Symbol *sym;

  CHECK_SYMBOL (variable);
  sym = XSYMBOL (variable);

  /* Make sure the current binding is actually swapped in.  */
  find_symbol_value (variable);

 start:
  switch (sym->u.s.redirect)
    {
    case SYMBOL_VARALIAS: sym = indirect_variable (sym); goto start;
    case SYMBOL_PLAINVAL: return Qnil;
    case SYMBOL_FORWARDED:
      {
	union Lisp_Fwd *valcontents = SYMBOL_FWD (sym);
	if (KBOARD_OBJFWDP (valcontents))
	  return Fframe_terminal (selected_frame);
	else if (!BUFFER_OBJFWDP (valcontents))
	  return Qnil;
      }
      FALLTHROUGH;
    case SYMBOL_LOCALIZED:
      /* For a local variable, record both the symbol and which
	 buffer's or frame's value we are saving.  */
      if (!NILP (Flocal_variable_p (variable, Qnil)))
	return Fcurrent_buffer ();
      else if (sym->u.s.redirect == SYMBOL_LOCALIZED
	       && blv_found (SYMBOL_BLV (sym)))
	return SYMBOL_BLV (sym)->where;
      else
	return Qnil;
    default: emacs_abort ();
    }
}


void
aset_multibyte_string(register Lisp_Object array, EMACS_INT idxval, int c)
{
  ptrdiff_t idxval_byte, nbytes;
  int prev_bytes, new_bytes;
  unsigned char workbuf[MAX_MULTIBYTE_LENGTH], *p0 = workbuf, *p1;

  nbytes = SBYTES (array);
  idxval_byte = string_char_to_byte (array, idxval);
  p1 = SDATA (array) + idxval_byte;
  prev_bytes = BYTES_BY_CHAR_HEAD (*p1);
  new_bytes = CHAR_STRING (c, p0);
  if (prev_bytes != new_bytes)
    {
      /* We must relocate the string data.  */
      ptrdiff_t nchars = SCHARS (array);
      USE_SAFE_ALLOCA;
      unsigned char *str = SAFE_ALLOCA (nbytes);

      memcpy (str, SDATA (array), nbytes);
      allocate_string_data (XSTRING (array), nchars,
                            nbytes + new_bytes - prev_bytes);
      memcpy (SDATA (array), str, idxval_byte);
      p1 = SDATA (array) + idxval_byte;
      memcpy (p1 + new_bytes, str + idxval_byte + prev_bytes,
              nbytes - (idxval_byte + prev_bytes));
      SAFE_FREE ();
      clear_string_char_byte_cache ();
    }
  while (new_bytes--)
    *p1++ = *p0++;
}


/* Convert the integer I to a cons-of-integers, where I is not in
   fixnum range.  */

#define INTBIG_TO_LISP(i, extremum)				    \
  (eassert (FIXNUM_OVERFLOW_P (i)),				    \
   (! (FIXNUM_OVERFLOW_P ((extremum) >> 16)			    \
       && FIXNUM_OVERFLOW_P ((i) >> 16))			    \
    ? Fcons (make_number ((i) >> 16), make_number ((i) & 0xffff))   \
    : ! (FIXNUM_OVERFLOW_P ((extremum) >> 16 >> 24)		    \
	 && FIXNUM_OVERFLOW_P ((i) >> 16 >> 24))		    \
    ? Fcons (make_number ((i) >> 16 >> 24),			    \
	     Fcons (make_number ((i) >> 16 & 0xffffff),		    \
		    make_number ((i) & 0xffff)))		    \
    : make_float (i)))

Lisp_Object
intbig_to_lisp (intmax_t i)
{
  return INTBIG_TO_LISP (i, INTMAX_MIN);
}

Lisp_Object
uintbig_to_lisp (uintmax_t i)
{
  return INTBIG_TO_LISP (i, UINTMAX_MAX);
}

/* Convert the cons-of-integers, integer, or float value C to an
   unsigned value with maximum value MAX, where MAX is one less than a
   power of 2.  Signal an error if C does not have a valid format or
   is out of range.  */
uintmax_t
cons_to_unsigned (Lisp_Object c, uintmax_t max)
{
  bool valid = false;
  uintmax_t val UNINIT;
  if (INTEGERP (c))
    {
      valid = XINT (c) >= 0;
      val = XINT (c);
    }
  else if (FLOATP (c))
    {
      double d = XFLOAT_DATA (c);
      if (d >= 0 && d < 1.0 + max)
	{
	  val = d;
	  valid = val == d;
	}
    }
  else if (CONSP (c) && NATNUMP (XCAR (c)))
    {
      uintmax_t top = XFASTINT (XCAR (c));
      Lisp_Object rest = XCDR (c);
      if (top <= UINTMAX_MAX >> 24 >> 16
	  && CONSP (rest)
	  && NATNUMP (XCAR (rest)) && XFASTINT (XCAR (rest)) < 1 << 24
	  && NATNUMP (XCDR (rest)) && XFASTINT (XCDR (rest)) < 1 << 16)
	{
	  uintmax_t mid = XFASTINT (XCAR (rest));
	  val = top << 24 << 16 | mid << 16 | XFASTINT (XCDR (rest));
	  valid = true;
	}
      else if (top <= UINTMAX_MAX >> 16)
	{
	  if (CONSP (rest))
	    rest = XCAR (rest);
	  if (NATNUMP (rest) && XFASTINT (rest) < 1 << 16)
	    {
	      val = top << 16 | XFASTINT (rest);
	      valid = true;
	    }
	}
    }

  if (! (valid && val <= max))
    error ("Not an in-range integer, integral float, or cons of integers");
  return val;
}

/* Convert the cons-of-integers, integer, or float value C to a signed
   value with extrema MIN and MAX.  MAX should be one less than a
   power of 2, and MIN should be zero or the negative of a power of 2.
   Signal an error if C does not have a valid format or is out of
   range.  */
intmax_t
cons_to_signed (Lisp_Object c, intmax_t min, intmax_t max)
{
  bool valid = false;
  intmax_t val UNINIT;
  if (INTEGERP (c))
    {
      val = XINT (c);
      valid = true;
    }
  else if (FLOATP (c))
    {
      double d = XFLOAT_DATA (c);
      if (d >= min && d < 1.0 + max)
	{
	  val = d;
	  valid = val == d;
	}
    }
  else if (CONSP (c) && INTEGERP (XCAR (c)))
    {
      intmax_t top = XINT (XCAR (c));
      Lisp_Object rest = XCDR (c);
      if (top >= INTMAX_MIN >> 24 >> 16 && top <= INTMAX_MAX >> 24 >> 16
	  && CONSP (rest)
	  && NATNUMP (XCAR (rest)) && XFASTINT (XCAR (rest)) < 1 << 24
	  && NATNUMP (XCDR (rest)) && XFASTINT (XCDR (rest)) < 1 << 16)
	{
	  intmax_t mid = XFASTINT (XCAR (rest));
	  val = top << 24 << 16 | mid << 16 | XFASTINT (XCDR (rest));
	  valid = true;
	}
      else if (top >= INTMAX_MIN >> 16 && top <= INTMAX_MAX >> 16)
	{
	  if (CONSP (rest))
	    rest = XCAR (rest);
	  if (NATNUMP (rest) && XFASTINT (rest) < 1 << 16)
	    {
	      val = top << 16 | XFASTINT (rest);
	      valid = true;
	    }
	}
    }

  if (! (valid && min <= val && val <= max))
    error ("Not an in-range integer, integral float, or cons of integers");
  return val;
}

DEFUN ("number-to-string", Fnumber_to_string, Snumber_to_string, 1, 1, 0,
       doc: /* Return the decimal representation of NUMBER as a string.
Uses a minus sign if negative.
NUMBER may be an integer or a floating point number.  */)
  (Lisp_Object number)
{
  char buffer[max (FLOAT_TO_STRING_BUFSIZE, INT_BUFSIZE_BOUND (EMACS_INT))];
  int len;

  CHECK_NUMBER_OR_FLOAT (number);

  if (FLOATP (number))
    len = float_to_string (buffer, XFLOAT_DATA (number));
  else
    len = sprintf (buffer, "%"pI"d", XINT (number));

  return make_unibyte_string (buffer, len);
}

static Lisp_Object
ash_lsh_impl (Lisp_Object value, Lisp_Object count, bool lsh)
{
  /* This code assumes that signed right shifts are arithmetic.  */
  verify ((EMACS_INT) -1 >> 1 == -1);

  Lisp_Object val;

  CHECK_NUMBER (value);
  CHECK_NUMBER (count);

  if (XINT (count) >= EMACS_INT_WIDTH)
    XSETINT (val, 0);
  else if (XINT (count) > 0)
    XSETINT (val, XUINT (value) << XINT (count));
  else if (XINT (count) <= -EMACS_INT_WIDTH)
    XSETINT (val, lsh ? 0 : XINT (value) < 0 ? -1 : 0);
  else
    XSETINT (val, (lsh ? XUINT (value) >> -XINT (count)
		   : XINT (value) >> -XINT (count)));
  return val;
}

DEFUN ("ash", Fash, Sash, 2, 2, 0,
       doc: /* Return VALUE with its bits shifted left by COUNT.
If COUNT is negative, shifting is actually to the right.
In this case, the sign bit is duplicated.  */)
  (register Lisp_Object value, Lisp_Object count)
{
  return ash_lsh_impl (value, count, false);
}

DEFUN ("lsh", Flsh, Slsh, 2, 2, 0,
       doc: /* Return VALUE with its bits shifted left by COUNT.
If COUNT is negative, shifting is actually to the right.
In this case, zeros are shifted in on the left.  */)
  (register Lisp_Object value, Lisp_Object count)
{
  return ash_lsh_impl (value, count, true);
}

/* Because we round up the bool vector allocate size to word_size
   units, we can safely read past the "end" of the vector in the
   operations below.  These extra bits are always zero.  */

static bits_word
bool_vector_spare_mask (EMACS_INT nr_bits)
{
  return (((bits_word) 1) << (nr_bits % BITS_PER_BITS_WORD)) - 1;
}

enum bool_vector_op { bool_vector_exclusive_or,
                      bool_vector_union,
                      bool_vector_intersection,
                      bool_vector_set_difference,
                      bool_vector_subsetp };

Lisp_Object bool_vector_binop_driver (Lisp_Object, Lisp_Object, Lisp_Object, enum bool_vector_op);

Lisp_Object
bool_vector_binop_driver (Lisp_Object a,
                          Lisp_Object b,
                          Lisp_Object dest,
                          enum bool_vector_op op)
{
  EMACS_INT nr_bits;
  bits_word *adata, *bdata, *destdata;
  ptrdiff_t i = 0;
  ptrdiff_t nr_words;

  CHECK_BOOL_VECTOR (a);
  CHECK_BOOL_VECTOR (b);

  nr_bits = bool_vector_size (a);
  if (bool_vector_size (b) != nr_bits)
    wrong_length_argument (a, b, dest);

  nr_words = bool_vector_words (nr_bits);
  adata = bool_vector_data (a);
  bdata = bool_vector_data (b);

  if (NILP (dest))
    {
      dest = make_uninit_bool_vector (nr_bits);
      destdata = bool_vector_data (dest);
    }
  else
    {
      CHECK_BOOL_VECTOR (dest);
      destdata = bool_vector_data (dest);
      if (bool_vector_size (dest) != nr_bits)
	wrong_length_argument (a, b, dest);

      switch (op)
	{
	case bool_vector_exclusive_or:
	  for (; i < nr_words; i++)
	    if (destdata[i] != (adata[i] ^ bdata[i]))
	      goto set_dest;
	  break;

	case bool_vector_subsetp:
	  for (; i < nr_words; i++)
	    if (adata[i] &~ bdata[i])
	      return Qnil;
	  return Qt;

	case bool_vector_union:
	  for (; i < nr_words; i++)
	    if (destdata[i] != (adata[i] | bdata[i]))
	      goto set_dest;
	  break;

	case bool_vector_intersection:
	  for (; i < nr_words; i++)
	    if (destdata[i] != (adata[i] & bdata[i]))
	      goto set_dest;
	  break;

	case bool_vector_set_difference:
	  for (; i < nr_words; i++)
	    if (destdata[i] != (adata[i] &~ bdata[i]))
	      goto set_dest;
	  break;
	}

      return Qnil;
    }

 set_dest:
  switch (op)
    {
    case bool_vector_exclusive_or:
      for (; i < nr_words; i++)
	destdata[i] = adata[i] ^ bdata[i];
      break;

    case bool_vector_union:
      for (; i < nr_words; i++)
	destdata[i] = adata[i] | bdata[i];
      break;

    case bool_vector_intersection:
      for (; i < nr_words; i++)
	destdata[i] = adata[i] & bdata[i];
      break;

    case bool_vector_set_difference:
      for (; i < nr_words; i++)
	destdata[i] = adata[i] &~ bdata[i];
      break;

    default:
      eassume (0);
    }

  return dest;
}

static bits_word
bits_word_to_host_endian (bits_word val)
{
#ifndef WORDS_BIGENDIAN
  return val;
#else
  if (BITS_WORD_MAX >> 31 == 1)
    return bswap_32 (val);
# if HAVE_UNSIGNED_LONG_LONG
  if (BITS_WORD_MAX >> 31 >> 31 >> 1 == 1)
    return bswap_64 (val);
# endif
  {
    int i;
    bits_word r = 0;
    for (i = 0; i < sizeof val; i++)
      {
	r = ((r << 1 << (CHAR_BIT - 1))
	     | (val & ((1u << 1 << (CHAR_BIT - 1)) - 1)));
	val = val >> 1 >> (CHAR_BIT - 1);
      }
    return r;
  }
#endif
}

DEFUN ("bool-vector-not", Fbool_vector_not,
       Sbool_vector_not, 1, 2, 0,
       doc: /* Compute ~A, set complement.
If optional second argument B is given, store result into B.
A and B must be bool vectors of the same length.
Return the destination vector.  */)
  (Lisp_Object a, Lisp_Object b)
{
  EMACS_INT nr_bits;
  bits_word *bdata, *adata;
  ptrdiff_t i;

  CHECK_BOOL_VECTOR (a);
  nr_bits = bool_vector_size (a);

  if (NILP (b))
    b = make_uninit_bool_vector (nr_bits);
  else
    {
      CHECK_BOOL_VECTOR (b);
      if (bool_vector_size (b) != nr_bits)
	wrong_length_argument (a, b, Qnil);
    }

  bdata = bool_vector_data (b);
  adata = bool_vector_data (a);

  for (i = 0; i < nr_bits / BITS_PER_BITS_WORD; i++)
    bdata[i] = BITS_WORD_MAX & ~adata[i];

  if (nr_bits % BITS_PER_BITS_WORD)
    {
      bits_word mword = bits_word_to_host_endian (adata[i]);
      mword = ~mword;
      mword &= bool_vector_spare_mask (nr_bits);
      bdata[i] = bits_word_to_host_endian (mword);
    }

  return b;
}

DEFUN ("bool-vector-count-population", Fbool_vector_count_population,
       Sbool_vector_count_population, 1, 1, 0,
       doc: /* Count how many elements in A are t.
A is a bool vector.  To count A's nil elements, subtract the return
value from A's length.  */)
  (Lisp_Object a)
{
  EMACS_INT count;
  EMACS_INT nr_bits;
  bits_word *adata;
  ptrdiff_t i, nwords;

  CHECK_BOOL_VECTOR (a);

  nr_bits = bool_vector_size (a);
  nwords = bool_vector_words (nr_bits);
  count = 0;
  adata = bool_vector_data (a);

  for (i = 0; i < nwords; i++)
    count += rust_count_one_bits (adata[i]);

  return make_number (count);
}

DEFUN ("bool-vector-count-consecutive", Fbool_vector_count_consecutive,
       Sbool_vector_count_consecutive, 3, 3, 0,
       doc: /* Count how many consecutive elements in A equal B starting at I.
A is a bool vector, B is t or nil, and I is an index into A.  */)
  (Lisp_Object a, Lisp_Object b, Lisp_Object i)
{
  EMACS_INT count;
  EMACS_INT nr_bits;
  int offset;
  bits_word *adata;
  bits_word twiddle;
  bits_word mword; /* Machine word.  */
  ptrdiff_t pos, pos0;
  ptrdiff_t nr_words;

  CHECK_BOOL_VECTOR (a);
  CHECK_NATNUM (i);

  nr_bits = bool_vector_size (a);
  if (XFASTINT (i) > nr_bits) /* Allow one past the end for convenience */
    args_out_of_range (a, i);

  adata = bool_vector_data (a);
  nr_words = bool_vector_words (nr_bits);
  pos = XFASTINT (i) / BITS_PER_BITS_WORD;
  offset = XFASTINT (i) % BITS_PER_BITS_WORD;
  count = 0;

  /* By XORing with twiddle, we transform the problem of "count
     consecutive equal values" into "count the zero bits".  The latter
     operation usually has hardware support.  */
  twiddle = NILP (b) ? 0 : BITS_WORD_MAX;

  /* Scan the remainder of the mword at the current offset.  */
  if (pos < nr_words && offset != 0)
    {
      mword = bits_word_to_host_endian (adata[pos]);
      mword ^= twiddle;
      mword >>= offset;

      /* Do not count the pad bits.  */
      mword |= (bits_word) 1 << (BITS_PER_BITS_WORD - offset);

      count = rust_count_trailing_zero_bits (mword);
      pos++;
      if (count + offset < BITS_PER_BITS_WORD)
        return make_number (count);
    }

  /* Scan whole words until we either reach the end of the vector or
     find an mword that doesn't completely match.  twiddle is
     endian-independent.  */
  pos0 = pos;
  while (pos < nr_words && adata[pos] == twiddle)
    pos++;
  count += (pos - pos0) * BITS_PER_BITS_WORD;

  if (pos < nr_words)
    {
      /* If we stopped because of a mismatch, see how many bits match
         in the current mword.  */
      mword = bits_word_to_host_endian (adata[pos]);
      mword ^= twiddle;
      count += rust_count_trailing_zero_bits (mword);
    }
  else if (nr_bits % BITS_PER_BITS_WORD != 0)
    {
      /* If we hit the end, we might have overshot our count.  Reduce
         the total by the number of spare bits at the end of the
         vector.  */
      count -= BITS_PER_BITS_WORD - nr_bits % BITS_PER_BITS_WORD;
    }

  return make_number (count);
}

void rust_init_syms(void);


void
syms_of_data (void)
{
  Lisp_Object error_tail, arith_tail;
  rust_init_syms();

  DEFSYM (Qquote, "quote");
  DEFSYM (Qlambda, "lambda");
  DEFSYM (Qerror_conditions, "error-conditions");
  DEFSYM (Qerror_message, "error-message");
  DEFSYM (Qtop_level, "top-level");

  DEFSYM (Qerror, "error");
  DEFSYM (Quser_error, "user-error");
  DEFSYM (Qquit, "quit");
  DEFSYM (Qwrong_length_argument, "wrong-length-argument");
  DEFSYM (Qwrong_type_argument, "wrong-type-argument");
  DEFSYM (Qargs_out_of_range, "args-out-of-range");
  DEFSYM (Qvoid_function, "void-function");
  DEFSYM (Qcyclic_function_indirection, "cyclic-function-indirection");
  DEFSYM (Qcyclic_variable_indirection, "cyclic-variable-indirection");
  DEFSYM (Qvoid_variable, "void-variable");
  DEFSYM (Qsetting_constant, "setting-constant");
  DEFSYM (Qtrapping_constant, "trapping-constant");
  DEFSYM (Qinvalid_read_syntax, "invalid-read-syntax");

  DEFSYM (Qinvalid_function, "invalid-function");
  DEFSYM (Qwrong_number_of_arguments, "wrong-number-of-arguments");
  DEFSYM (Qno_catch, "no-catch");
  DEFSYM (Qend_of_file, "end-of-file");
  DEFSYM (Qarith_error, "arith-error");
  DEFSYM (Qbeginning_of_buffer, "beginning-of-buffer");
  DEFSYM (Qend_of_buffer, "end-of-buffer");
  DEFSYM (Qbuffer_read_only, "buffer-read-only");
  DEFSYM (Qtext_read_only, "text-read-only");
  DEFSYM (Qmark_inactive, "mark-inactive");

  DEFSYM (Qlistp, "listp");
  DEFSYM (Qconsp, "consp");
  DEFSYM (Qsymbolp, "symbolp");
  DEFSYM (Qintegerp, "integerp");
  DEFSYM (Qnatnump, "natnump");
  DEFSYM (Qwholenump, "wholenump");
  DEFSYM (Qstringp, "stringp");
  DEFSYM (Qarrayp, "arrayp");
  DEFSYM (Qsequencep, "sequencep");
  DEFSYM (Qbufferp, "bufferp");
  DEFSYM (Qvectorp, "vectorp");
  DEFSYM (Qrecordp, "recordp");
  DEFSYM (Qbool_vector_p, "bool-vector-p");
  DEFSYM (Qchar_or_string_p, "char-or-string-p");
  DEFSYM (Qmarkerp, "markerp");
#ifdef HAVE_MODULES
  DEFSYM (Quser_ptrp, "user-ptrp");
#endif
  DEFSYM (Qbuffer_or_string_p, "buffer-or-string-p");
  DEFSYM (Qinteger_or_marker_p, "integer-or-marker-p");
  DEFSYM (Qfboundp, "fboundp");

  DEFSYM (Qfloatp, "floatp");
  DEFSYM (Qnumberp, "numberp");
  DEFSYM (Qnumber_or_marker_p, "number-or-marker-p");

  DEFSYM (Qchar_table_p, "char-table-p");
  DEFSYM (Qvector_or_char_table_p, "vector-or-char-table-p");

  DEFSYM (Qsubrp, "subrp");
  DEFSYM (Qunevalled, "unevalled");
  DEFSYM (Qmany, "many");

  DEFSYM (Qcdr, "cdr");

  error_tail = pure_cons (Qerror, Qnil);

  /* ERROR is used as a signaler for random errors for which nothing else is
     right.  */

  Fput (Qerror, Qerror_conditions,
	error_tail);
  Fput (Qerror, Qerror_message,
	build_pure_c_string ("error"));

#define PUT_ERROR(sym, tail, msg)			\
  Fput (sym, Qerror_conditions, pure_cons (sym, tail)); \
  Fput (sym, Qerror_message, build_pure_c_string (msg))

  PUT_ERROR (Qquit, Qnil, "Quit");

  PUT_ERROR (Quser_error, error_tail, "");
  PUT_ERROR (Qwrong_length_argument, error_tail, "Wrong length argument");
  PUT_ERROR (Qwrong_type_argument, error_tail, "Wrong type argument");
  PUT_ERROR (Qargs_out_of_range, error_tail, "Args out of range");
  PUT_ERROR (Qvoid_function, error_tail,
	     "Symbol's function definition is void");
  PUT_ERROR (Qcyclic_function_indirection, error_tail,
	     "Symbol's chain of function indirections contains a loop");
  PUT_ERROR (Qcyclic_variable_indirection, error_tail,
	     "Symbol's chain of variable indirections contains a loop");
  DEFSYM (Qcircular_list, "circular-list");
  PUT_ERROR (Qcircular_list, error_tail, "List contains a loop");
  PUT_ERROR (Qvoid_variable, error_tail, "Symbol's value as variable is void");
  PUT_ERROR (Qsetting_constant, error_tail,
	     "Attempt to set a constant symbol");
  PUT_ERROR (Qtrapping_constant, error_tail,
             "Attempt to trap writes to a constant symbol");
  PUT_ERROR (Qinvalid_read_syntax, error_tail, "Invalid read syntax");
  PUT_ERROR (Qinvalid_function, error_tail, "Invalid function");
  PUT_ERROR (Qwrong_number_of_arguments, error_tail,
	     "Wrong number of arguments");
  PUT_ERROR (Qno_catch, error_tail, "No catch for tag");
  PUT_ERROR (Qend_of_file, error_tail, "End of file during parsing");

  arith_tail = pure_cons (Qarith_error, error_tail);
  Fput (Qarith_error, Qerror_conditions, arith_tail);
  Fput (Qarith_error, Qerror_message, build_pure_c_string ("Arithmetic error"));

  PUT_ERROR (Qbeginning_of_buffer, error_tail, "Beginning of buffer");
  PUT_ERROR (Qend_of_buffer, error_tail, "End of buffer");
  PUT_ERROR (Qbuffer_read_only, error_tail, "Buffer is read-only");
  PUT_ERROR (Qtext_read_only, pure_cons (Qbuffer_read_only, error_tail),
	     "Text is read-only");

  DEFSYM (Qrange_error, "range-error");
  DEFSYM (Qdomain_error, "domain-error");
  DEFSYM (Qsingularity_error, "singularity-error");
  DEFSYM (Qoverflow_error, "overflow-error");
  DEFSYM (Qunderflow_error, "underflow-error");

  PUT_ERROR (Qdomain_error, arith_tail, "Arithmetic domain error");

  PUT_ERROR (Qrange_error, arith_tail, "Arithmetic range error");

  PUT_ERROR (Qsingularity_error, Fcons (Qdomain_error, arith_tail),
	     "Arithmetic singularity error");

  PUT_ERROR (Qoverflow_error, Fcons (Qdomain_error, arith_tail),
	     "Arithmetic overflow error");
  PUT_ERROR (Qunderflow_error, Fcons (Qdomain_error, arith_tail),
	     "Arithmetic underflow error");

  /* Types that type-of returns.  */
  DEFSYM (Qinteger, "integer");
  DEFSYM (Qsymbol, "symbol");
  DEFSYM (Qstring, "string");
  DEFSYM (Qcons, "cons");
  DEFSYM (Qmarker, "marker");
  DEFSYM (Qoverlay, "overlay");
  DEFSYM (Qfinalizer, "finalizer");
#ifdef HAVE_MODULES
  DEFSYM (Qmodule_function, "module-function");
  DEFSYM (Quser_ptr, "user-ptr");
#endif
  DEFSYM (Qfloat, "float");
  DEFSYM (Qwindow_configuration, "window-configuration");
  DEFSYM (Qprocess, "process");
  DEFSYM (Qwindow, "window");
  DEFSYM (Qsubr, "subr");
  DEFSYM (Qcompiled_function, "compiled-function");
  DEFSYM (Qbuffer, "buffer");
  DEFSYM (Qframe, "frame");
  DEFSYM (Qvector, "vector");
  DEFSYM (Qrecord, "record");
  DEFSYM (Qchar_table, "char-table");
  DEFSYM (Qbool_vector, "bool-vector");
  DEFSYM (Qhash_table, "hash-table");
  DEFSYM (Qthread, "thread");
  DEFSYM (Qmutex, "mutex");
  DEFSYM (Qcondition_variable, "condition-variable");
  DEFSYM (Qfont_spec, "font-spec");
  DEFSYM (Qfont_entity, "font-entity");
  DEFSYM (Qfont_object, "font-object");
  DEFSYM (Qterminal, "terminal");

  DEFSYM (Qdefun, "defun");

  DEFSYM (Qinteractive_form, "interactive-form");
  DEFSYM (Qdefalias_fset_function, "defalias-fset-function");

  defsubr (&Sinteractive_form);
  defsubr (&Smodule_function_p);
  defsubr (&Ssetq_default);
  defsubr (&Smake_variable_buffer_local);
  defsubr (&Smake_local_variable);
  defsubr (&Skill_local_variable);
  defsubr (&Slocal_variable_if_set_p);
  defsubr (&Svariable_binding_locus);
#if 0                           /* XXX Remove this. --lorentey */
  defsubr (&Sterminal_local_value);
  defsubr (&Sset_terminal_local_value);
#endif
  defsubr (&Snumber_to_string);
  defsubr (&Slsh);
  defsubr (&Sash);
#ifdef HAVE_MODULES
  defsubr (&Suser_ptrp);
#endif

  defsubr (&Sbool_vector_not);
  defsubr (&Sbool_vector_count_consecutive);
  defsubr (&Sbool_vector_count_population);

  set_symbol_function (Qwholenump, XSYMBOL (Qnatnump)->u.s.function);

  DEFVAR_LISP ("most-positive-fixnum", Vmost_positive_fixnum,
	       doc: /* The largest value that is representable in a Lisp integer.
This variable cannot be set; trying to do so will signal an error.  */);
  Vmost_positive_fixnum = make_number (MOST_POSITIVE_FIXNUM);
  make_symbol_constant (intern_c_string ("most-positive-fixnum"));

  DEFVAR_LISP ("most-negative-fixnum", Vmost_negative_fixnum,
	       doc: /* The smallest value that is representable in a Lisp integer.
This variable cannot be set; trying to do so will signal an error.  */);
  Vmost_negative_fixnum = make_number (MOST_NEGATIVE_FIXNUM);
  make_symbol_constant (intern_c_string ("most-negative-fixnum"));

  DEFSYM (Qwatchers, "watchers");
  DEFSYM (Qmakunbound, "makunbound");
  DEFSYM (Qunlet, "unlet");
  DEFSYM (Qset, "set");
  DEFSYM (Qset_default, "set-default");
}
