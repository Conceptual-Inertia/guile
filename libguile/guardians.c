/*	Copyright (C) 1998 Free Software Foundation, Inc.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307 USA
 *
 * As a special exception, the Free Software Foundation gives permission
 * for additional uses of the text contained in its release of GUILE.
 *
 * The exception is that, if you link the GUILE library with other files
 * to produce an executable, this does not by itself cause the
 * resulting executable to be covered by the GNU General Public License.
 * Your use of that executable is in no way restricted on account of
 * linking the GUILE library code into it.
 *
 * This exception does not however invalidate any other reasons why
 * the executable file might be covered by the GNU General Public License.
 *
 * This exception applies only to the code released by the
 * Free Software Foundation under the name GUILE.  If you copy
 * code from other Free Software Foundation releases into a copy of
 * GUILE, as the General Public License permits, the exception does
 * not apply to the code that you add in this way.  To avoid misleading
 * anyone as to the status of such modified files, you must delete
 * this exception notice from them.
 *
 * If you write modifications of your own for GUILE, it is your choice
 * whether to permit this exception to apply to your modifications.
 * If you do not wish that, delete this exception notice.  */


/* This is an implementation of guardians as described in
 * R. Kent Dybvig, Carl Bruggeman, and David Eby (1993) "Guardians in
 * a Generation-Based Garbage Collector" ACM SIGPLAN Conference on
 * Programming Language Design and Implementation, June 1993
 * ftp://ftp.cs.indiana.edu/pub/scheme-repository/doc/pubs/guardians.ps.gz
 *
 * Author:      Michael N. Livshin
 * Modified by: Mikael Djurfeldt
 */

#include <stdio.h>
#include <assert.h>

#include "_scm.h"
#include "print.h"
#include "smob.h"

#include "guardians.h"

static long scm_tc16_guardian;

/* The live and zombies FIFOs are implemented as tconcs as described
   in Dybvig's paper.  This decouples addition and removal of elements
   so that no synchronization between these needs to take place.
*/
#define TCONC_IN(tc, obj, pair) \
{ \
  SCM_SETCAR ((tc).tail, obj); \
  SCM_SETCAR (pair, SCM_BOOL_F); \
  SCM_SETCDR (pair, SCM_BOOL_F); \
  SCM_SETCDR ((tc).tail, pair); \
  (tc).tail = pair; \
} \

#define TCONC_OUT(tc, res) \
{ \
  (res) = SCM_CAR ((tc).head); \
  (tc).head = SCM_CDR ((tc).head); \
} \

#define TCONC_EMPTYP(tc) ((tc).head == (tc).tail)

typedef struct tconc_t
{
  SCM head;
  SCM tail;
} tconc_t;

typedef struct guardian_t
{
  tconc_t live;
  tconc_t zombies;
} guardian_t;

#define GUARDIAN(x) ((guardian_t *) SCM_CDR (x))
#define GUARDIAN_LIVE(x) (GUARDIAN (x)->live)
#define GUARDIAN_ZOMBIES(x) (GUARDIAN (x)->zombies)

static SCM *guardians = NULL;
static scm_sizet guardians_size = 0;
static scm_sizet n_guardians;

static SCM
g_mark (SCM ptr)
{
  if (n_guardians >= guardians_size)
    {
      SCM_SYSCALL (guardians =
		   (SCM *) realloc((char *) guardians,
				   sizeof (SCM) * (guardians_size *= 2)));
      if (!guardians)
	{
	  scm_puts ("active guardian table", scm_cur_errp);
	  scm_puts ("\nFATAL ERROR DURING CRITICAL SCM_CODE SECTION\n",
		    scm_cur_errp);
	  exit (SCM_EXIT_FAILURE);
	}
    }
  guardians[n_guardians++] = ptr;
  /* Can't mark zombies here since they can refer to objects which are
     living dead, thereby preventing them to join the zombies. */
  return SCM_BOOL_F;
}

static scm_sizet
g_free (SCM ptr)
{
  scm_must_free ((char *) GUARDIAN (ptr));
  return sizeof (guardian_t);
}

static int
g_print (SCM exp, SCM port, scm_print_state *pstate)
{
  char buf[256];
  sprintf (buf, "#<guardian live objs: %lu zombies: %lu>",
	   scm_ilength (SCM_CDR (GUARDIAN_LIVE (exp).head)),
	   scm_ilength (SCM_CDR (GUARDIAN_ZOMBIES (exp).head)));
  scm_puts (buf, port);

  return 1;
}

static scm_smobfuns g_smob = {
  g_mark,
  g_free,
  g_print,
  0 /* g_equalp */
};

#define CCLO_G(cclo) (SCM_VELTS (cclo)[1])

static SCM
guard (SCM cclo, SCM arg)
{
  if (!SCM_UNBNDP (arg))
    {
      scm_guard (cclo, arg);
      return SCM_UNSPECIFIED;
    }
  else
    return scm_get_one_zombie (cclo);
}

static SCM guard1;

SCM_PROC (s_make_guardian, "make-guardian", 0, 0, 0, scm_make_guardian);
SCM
scm_make_guardian ()
{
  SCM cclo = scm_makcclo (guard1, 2L);
  guardian_t *g = (guardian_t *) scm_must_malloc (sizeof (guardian_t),
						  s_make_guardian);
  SCM z1 = scm_cons (SCM_BOOL_F, SCM_BOOL_F);
  SCM z2 = scm_cons (SCM_BOOL_F, SCM_BOOL_F);
  SCM z;
  SCM_NEWCELL (z);

  SCM_DEFER_INTS;
  /* A tconc starts out with one tail pair. */
  g->live.head = g->live.tail = z1;
  g->zombies.head = g->zombies.tail = z2;
  SCM_SETCDR (z, g);
  SCM_SETCAR (z, scm_tc16_guardian);
  SCM_ALLOW_INTS;

  CCLO_G (cclo) = z;

  return cclo;
}

void
scm_guardian_gc_init()
{
  n_guardians = 0;
}

void
scm_guardian_zombify ()
{
  int i;
  for (i = 0; i < n_guardians; ++i)
    {
      SCM g = guardians[i];
      /* Loop through the live list and
	 1. move unmarked objects to the zombies tconc
	 2. mark the live tconc.
      */
      SCM tconc_tail = GUARDIAN_LIVE (g).tail;
      SCM prev_pair = SCM_BOOL_F;
      SCM pair = GUARDIAN_LIVE (g).head;
      while (pair != tconc_tail)
	{
	  SCM next_pair = SCM_CDR (pair);

	  if (SCM_NMARKEDP (SCM_CAR (pair)))
	    {
	      /* got you, zombie! */

	      /* out of the live list! */
	      if (SCM_FALSEP (prev_pair))
		GUARDIAN_LIVE (g).head = next_pair;
	      else
                /* mark previous pair */
		SCM_SETCDR (prev_pair, next_pair | 1);

	      /* to the zombie list! */
	      TCONC_IN (GUARDIAN_ZOMBIES (g), SCM_CAR (pair), pair);
	    }
	  else
	    {
	      if (SCM_NFALSEP (prev_pair))
                /* mark previous pair */
		SCM_SETCDR (prev_pair, pair | 1);
	      prev_pair = pair;
	    }

	  pair = next_pair;
	}
      if (SCM_NFALSEP (prev_pair))
	/* mark previous pair */
	SCM_SETCDR (prev_pair, pair | 1);
      /* mark live list tail */
      SCM_SETOR_CDR (tconc_tail, 1);
      
      scm_gc_mark (GUARDIAN_ZOMBIES (g).head);
    }
}

void
scm_guard (SCM guardian, SCM obj)
{
  SCM g = CCLO_G (guardian);

  if (SCM_NIMP (obj))
    {
      SCM z;
      
      SCM_NEWCELL (z);

      /* This critical section barrier will be replaced by a mutex. */
      SCM_DEFER_INTS;
      TCONC_IN (GUARDIAN_LIVE (g), obj, z);
      SCM_ALLOW_INTS;
    }
}

SCM
scm_get_one_zombie (SCM guardian)
{
  SCM g = CCLO_G (guardian);
  SCM res = SCM_BOOL_F;

  /* This critical section barrier will be replaced by a mutex. */
  SCM_DEFER_INTS;
  if (!TCONC_EMPTYP (GUARDIAN_ZOMBIES (g)))
    TCONC_OUT (GUARDIAN_ZOMBIES (g), res);
  SCM_ALLOW_INTS;

  return res;
}

void
scm_init_guardian()
{
  scm_tc16_guardian = scm_newsmob (&g_smob);
  if (!(guardians = (SCM *) malloc ((guardians_size = 32) * sizeof (SCM))))
    {
      fprintf (stderr, "trouble!\n");
      exit (SCM_EXIT_FAILURE);
    }
  guard1 = scm_make_subr_opt ("guardian", scm_tc7_subr_2o, guard, 0);

#include "guardians.x"
}
