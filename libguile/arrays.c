/* Copyright (C) 1995,1996,1997,1998,2000,2001,2002,2003,2004,2005,
 *   2006, 2009, 2010, 2011, 2012, 2013, 2014, 2015 Free Software Foundation,
 *   Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */




#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "verify.h"

#include "libguile/_scm.h"
#include "libguile/__scm.h"
#include "libguile/eq.h"
#include "libguile/chars.h"
#include "libguile/eval.h"
#include "libguile/fports.h"
#include "libguile/feature.h"
#include "libguile/strings.h"
#include "libguile/srfi-13.h"
#include "libguile/srfi-4.h"
#include "libguile/vectors.h"
#include "libguile/bitvectors.h"
#include "libguile/bytevectors.h"
#include "libguile/list.h"
#include "libguile/dynwind.h"
#include "libguile/read.h"

#include "libguile/validate.h"
#include "libguile/arrays.h"
#include "libguile/array-map.h"
#include "libguile/generalized-vectors.h"
#include "libguile/generalized-arrays.h"
#include "libguile/uniform.h"


size_t
scm_c_array_rank (SCM array)
{
  if (SCM_I_ARRAYP (array))
    return SCM_I_ARRAY_NDIM (array);
  else if (scm_is_array (array))
    return 1;
  else
    scm_wrong_type_arg_msg ("array-rank", SCM_ARG1, array, "array");
}

SCM_DEFINE (scm_array_rank, "array-rank", 1, 0, 0,
           (SCM array),
	    "Return the number of dimensions of the array @var{array.}\n")
#define FUNC_NAME s_scm_array_rank
{
  return scm_from_size_t (scm_c_array_rank (array));
}
#undef FUNC_NAME


SCM_DEFINE (scm_shared_array_root, "shared-array-root", 1, 0, 0,
           (SCM ra),
	    "Return the root vector of a shared array.")
#define FUNC_NAME s_scm_shared_array_root
{
  if (SCM_I_ARRAYP (ra))
    return SCM_I_ARRAY_V (ra);
  else if (scm_is_array (ra))
    return ra;
  else
    scm_wrong_type_arg_msg (FUNC_NAME, SCM_ARG1, ra, "array");
}
#undef FUNC_NAME


SCM_DEFINE (scm_shared_array_offset, "shared-array-offset", 1, 0, 0,
           (SCM ra),
	    "Return the root vector index of the first element in the array.")
#define FUNC_NAME s_scm_shared_array_offset
{
  if (SCM_I_ARRAYP (ra))
    return scm_from_size_t (SCM_I_ARRAY_BASE (ra));
  else if (scm_is_array (ra))
    return scm_from_size_t (0);
  else
    scm_wrong_type_arg_msg (FUNC_NAME, SCM_ARG1, ra, "array");
}
#undef FUNC_NAME


SCM_DEFINE (scm_shared_array_increments, "shared-array-increments", 1, 0, 0,
           (SCM ra),
	    "For each dimension, return the distance between elements in the root vector.")
#define FUNC_NAME s_scm_shared_array_increments
{
  if (SCM_I_ARRAYP (ra))
    {
      size_t k = SCM_I_ARRAY_NDIM (ra);
      SCM res = SCM_EOL;
      scm_t_array_dim *dims = SCM_I_ARRAY_DIMS (ra);
      while (k--)
        res = scm_cons (scm_from_ssize_t (dims[k].inc), res);
      return res;
    }
  else if (scm_is_array (ra))
    return scm_list_1 (scm_from_ssize_t (1));
  else
    scm_wrong_type_arg_msg (FUNC_NAME, SCM_ARG1, ra, "array");
}
#undef FUNC_NAME

/* FIXME: to avoid this assumption, fix the accessors in arrays.h,
   scm_i_make_array, and the array cases in system/vm/assembler.scm. */

verify (sizeof (scm_t_array_dim) == 3*sizeof (scm_t_bits));

/* Matching SCM_I_ARRAY accessors in arrays.h */
SCM
scm_i_make_array (int ndim)
{
  SCM ra = scm_words (((scm_t_bits) ndim << 17) + scm_tc7_array, 3 + ndim*3);
  SCM_I_ARRAY_SET_V (ra, SCM_BOOL_F);
  SCM_I_ARRAY_SET_BASE (ra, 0);
  /* dimensions are unset */
  return ra;
}

static char s_bad_spec[] = "Bad scm_array dimension";


/* Increments will still need to be set. */

SCM
scm_i_shap2ra (SCM args)
{
  scm_t_array_dim *s;
  SCM ra, spec;
  int ndim = scm_ilength (args);
  if (ndim < 0)
    scm_misc_error (NULL, s_bad_spec, SCM_EOL);

  ra = scm_i_make_array (ndim);
  SCM_I_ARRAY_SET_BASE (ra, 0);
  s = SCM_I_ARRAY_DIMS (ra);
  for (; !scm_is_null (args); s++, args = SCM_CDR (args))
    {
      spec = SCM_CAR (args);
      if (scm_is_integer (spec))
	{
	  s->lbnd = 0;
	  s->ubnd = scm_to_ssize_t (spec);
          if (s->ubnd < 0)
            scm_misc_error (NULL, s_bad_spec, SCM_EOL);
          --s->ubnd;
	}
      else
	{
	  if (!scm_is_pair (spec) || !scm_is_integer (SCM_CAR (spec)))
	    scm_misc_error (NULL, s_bad_spec, SCM_EOL);
	  s->lbnd = scm_to_ssize_t (SCM_CAR (spec));
	  spec = SCM_CDR (spec);
	  if (!scm_is_pair (spec)
	      || !scm_is_integer (SCM_CAR (spec))
	      || !scm_is_null (SCM_CDR (spec)))
	    scm_misc_error (NULL, s_bad_spec, SCM_EOL);
	  s->ubnd = scm_to_ssize_t (SCM_CAR (spec));
          if (s->ubnd - s->lbnd < -1)
            scm_misc_error (NULL, s_bad_spec, SCM_EOL);
	}
      s->inc = 1;
    }
  return ra;
}

SCM_DEFINE (scm_make_typed_array, "make-typed-array", 2, 0, 1,
	    (SCM type, SCM fill, SCM bounds),
	    "Create and return an array of type @var{type}.")
#define FUNC_NAME s_scm_make_typed_array
{
  size_t k, rlen = 1;
  scm_t_array_dim *s;
  SCM ra;

  ra = scm_i_shap2ra (bounds);
  SCM_SET_ARRAY_CONTIGUOUS_FLAG (ra);
  s = SCM_I_ARRAY_DIMS (ra);
  k = SCM_I_ARRAY_NDIM (ra);

  while (k--)
    {
      s[k].inc = rlen;
      SCM_ASSERT_RANGE (1, bounds, s[k].lbnd <= s[k].ubnd + 1);
      rlen = (s[k].ubnd - s[k].lbnd + 1) * s[k].inc;
    }

  if (scm_is_eq (fill, SCM_UNSPECIFIED))
    fill = SCM_UNDEFINED;

  SCM_I_ARRAY_SET_V (ra, scm_make_generalized_vector (type, scm_from_size_t (rlen), fill));

  if (1 == SCM_I_ARRAY_NDIM (ra) && 0 == SCM_I_ARRAY_BASE (ra))
    if (0 == s->lbnd)
      return SCM_I_ARRAY_V (ra);

  return ra;
}
#undef FUNC_NAME

SCM
scm_from_contiguous_typed_array (SCM type, SCM bounds, const void *bytes,
                                 size_t byte_len)
#define FUNC_NAME "scm_from_contiguous_typed_array"
{
  size_t k, rlen = 1;
  scm_t_array_dim *s;
  SCM ra;
  scm_t_array_handle h;
  void *elts;
  size_t sz;

  ra = scm_i_shap2ra (bounds);
  SCM_SET_ARRAY_CONTIGUOUS_FLAG (ra);
  s = SCM_I_ARRAY_DIMS (ra);
  k = SCM_I_ARRAY_NDIM (ra);

  while (k--)
    {
      s[k].inc = rlen;
      SCM_ASSERT_RANGE (1, bounds, s[k].lbnd <= s[k].ubnd + 1);
      rlen = (s[k].ubnd - s[k].lbnd + 1) * s[k].inc;
    }
  SCM_I_ARRAY_SET_V (ra, scm_make_generalized_vector (type, scm_from_size_t (rlen), SCM_UNDEFINED));


  scm_array_get_handle (ra, &h);
  elts = h.writable_elements;
  sz = scm_array_handle_uniform_element_bit_size (&h);
  scm_array_handle_release (&h);

  if (sz >= 8 && ((sz % 8) == 0))
    {
      if (byte_len % (sz / 8))
        SCM_MISC_ERROR ("byte length not a multiple of the unit size", SCM_EOL);
      if (byte_len / (sz / 8) != rlen)
        SCM_MISC_ERROR ("byte length and dimensions do not match", SCM_EOL);
    }
  else if (sz < 8)
    {
      /* Elements of sub-byte size (bitvectors) are addressed in 32-bit
         units.  */
      if (byte_len != ((rlen * sz + 31) / 32) * 4)
        SCM_MISC_ERROR ("byte length and dimensions do not match", SCM_EOL);
    }
  else
    /* an internal guile error, really */
    SCM_MISC_ERROR ("uniform elements larger than 8 bits must fill whole bytes", SCM_EOL);

  memcpy (elts, bytes, byte_len);

  if (1 == SCM_I_ARRAY_NDIM (ra) && 0 == SCM_I_ARRAY_BASE (ra))
    if (0 == s->lbnd)
      return SCM_I_ARRAY_V (ra);
  return ra;
}
#undef FUNC_NAME

SCM_DEFINE (scm_make_array, "make-array", 1, 0, 1,
	    (SCM fill, SCM bounds),
	    "Create and return an array.")
#define FUNC_NAME s_scm_make_array
{
  return scm_make_typed_array (SCM_BOOL_T, fill, bounds);
}
#undef FUNC_NAME

/* see scm_from_contiguous_array */
static void
scm_i_ra_set_contp (SCM ra)
{
  size_t k = SCM_I_ARRAY_NDIM (ra);
  if (k)
    {
      ssize_t inc = SCM_I_ARRAY_DIMS (ra)[k - 1].inc;
      while (k--)
	{
	  if (inc != SCM_I_ARRAY_DIMS (ra)[k].inc)
	    {
	      SCM_CLR_ARRAY_CONTIGUOUS_FLAG (ra);
	      return;
	    }
	  inc *= (SCM_I_ARRAY_DIMS (ra)[k].ubnd
		  - SCM_I_ARRAY_DIMS (ra)[k].lbnd + 1);
	}
    }
  SCM_SET_ARRAY_CONTIGUOUS_FLAG (ra);
}


SCM_DEFINE (scm_make_shared_array, "make-shared-array", 2, 0, 1,
           (SCM oldra, SCM mapfunc, SCM dims),
	    "@code{make-shared-array} can be used to create shared subarrays\n"
	    "of other arrays.  The @var{mapfunc} is a function that\n"
	    "translates coordinates in the new array into coordinates in the\n"
	    "old array.  A @var{mapfunc} must be linear, and its range must\n"
	    "stay within the bounds of the old array, but it can be\n"
	    "otherwise arbitrary.  A simple example:\n"
	    "@lisp\n"
	    "(define fred (make-array #f 8 8))\n"
	    "(define freds-diagonal\n"
	    "  (make-shared-array fred (lambda (i) (list i i)) 8))\n"
	    "(array-set! freds-diagonal 'foo 3)\n"
	    "(array-ref fred 3 3) @result{} foo\n"
	    "(define freds-center\n"
	    "  (make-shared-array fred (lambda (i j) (list (+ 3 i) (+ 3 j))) 2 2))\n"
	    "(array-ref freds-center 0 0) @result{} foo\n"
	    "@end lisp")
#define FUNC_NAME s_scm_make_shared_array
{
  scm_t_array_handle old_handle;
  SCM ra;
  SCM inds, indptr;
  SCM imap;
  size_t k;
  ssize_t i;
  long old_base, old_min, new_min, old_max, new_max;
  scm_t_array_dim *s;

  SCM_VALIDATE_REST_ARGUMENT (dims);
  SCM_VALIDATE_PROC (2, mapfunc);
  ra = scm_i_shap2ra (dims);

  scm_array_get_handle (oldra, &old_handle);

  if (SCM_I_ARRAYP (oldra))
    {
      SCM_I_ARRAY_SET_V (ra, SCM_I_ARRAY_V (oldra));
      old_base = old_min = old_max = SCM_I_ARRAY_BASE (oldra);
      s = scm_array_handle_dims (&old_handle);
      k = scm_array_handle_rank (&old_handle);
      while (k--)
	{
	  if (s[k].inc > 0)
	    old_max += (s[k].ubnd - s[k].lbnd) * s[k].inc;
	  else
	    old_min += (s[k].ubnd - s[k].lbnd) * s[k].inc;
	}
    }
  else
    {
      SCM_I_ARRAY_SET_V (ra, oldra);
      old_base = old_min = 0;
      old_max = scm_c_array_length (oldra) - 1;
    }

  inds = SCM_EOL;
  s = SCM_I_ARRAY_DIMS (ra);
  for (k = 0; k < SCM_I_ARRAY_NDIM (ra); k++)
    {
      inds = scm_cons (scm_from_ssize_t (s[k].lbnd), inds);
      if (s[k].ubnd < s[k].lbnd)
	{
	  if (1 == SCM_I_ARRAY_NDIM (ra))
	    ra = scm_make_generalized_vector (scm_array_type (ra),
                                              SCM_INUM0, SCM_UNDEFINED);
	  else
	    SCM_I_ARRAY_SET_V (ra, scm_make_generalized_vector (scm_array_type (ra),
                                                                SCM_INUM0, SCM_UNDEFINED));
	  scm_array_handle_release (&old_handle);
	  return ra;
	}
    }

  imap = scm_apply_0 (mapfunc, scm_reverse (inds));
  i = scm_array_handle_pos (&old_handle, imap);
  new_min = new_max = i + old_base;
  SCM_I_ARRAY_SET_BASE (ra, new_min);
  indptr = inds;
  k = SCM_I_ARRAY_NDIM (ra);
  while (k--)
    {
      if (s[k].ubnd > s[k].lbnd)
	{
	  SCM_SETCAR (indptr, scm_sum (SCM_CAR (indptr), scm_from_int (1)));
	  imap = scm_apply_0 (mapfunc, scm_reverse (inds));
	  s[k].inc = scm_array_handle_pos (&old_handle, imap) - i;
	  i += s[k].inc;
	  if (s[k].inc > 0)
	    new_max += (s[k].ubnd - s[k].lbnd) * s[k].inc;
	  else
	    new_min += (s[k].ubnd - s[k].lbnd) * s[k].inc;
	}
      else
	s[k].inc = new_max - new_min + 1;	/* contiguous by default */
      indptr = SCM_CDR (indptr);
    }

  scm_array_handle_release (&old_handle);

  if (old_min > new_min || old_max < new_max)
    SCM_MISC_ERROR ("mapping out of range", SCM_EOL);
  if (1 == SCM_I_ARRAY_NDIM (ra) && 0 == SCM_I_ARRAY_BASE (ra))
    {
      SCM v = SCM_I_ARRAY_V (ra);
      size_t length = scm_c_array_length (v);
      if (1 == s->inc && 0 == s->lbnd && length == 1 + s->ubnd)
	return v;
      if (s->ubnd < s->lbnd)
	return scm_make_generalized_vector (scm_array_type (ra), SCM_INUM0,
                                            SCM_UNDEFINED);
    }
  scm_i_ra_set_contp (ra);
  return ra;
}
#undef FUNC_NAME


static void
array_from_pos (scm_t_array_handle *handle, size_t *ndim, size_t *k, SCM *i, ssize_t *pos,
                scm_t_array_dim **s, char const * FUNC_NAME, SCM error_args)
{
  *s = scm_array_handle_dims (handle);
  *k = *ndim = scm_array_handle_rank (handle);
  for (; *k>0 && scm_is_pair (*i); --*k, ++*s, *i=scm_cdr (*i))
    {
      ssize_t ik = scm_to_ssize_t (scm_car (*i));
      if (ik<(*s)->lbnd || ik>(*s)->ubnd)
        {
          scm_array_handle_release (handle);
          scm_misc_error (FUNC_NAME, "indices out of range", error_args);
        }
      *pos += (ik-(*s)->lbnd) * (*s)->inc;
    }
}

static void
array_from_get_o (scm_t_array_handle *handle, size_t k, scm_t_array_dim *s, ssize_t pos,
                  SCM *o)
{
  scm_t_array_dim * os;
  *o = scm_i_make_array (k);
  SCM_I_ARRAY_SET_V (*o, handle->vector);
  SCM_I_ARRAY_SET_BASE (*o, pos + handle->base);
  os = SCM_I_ARRAY_DIMS (*o);
  for (; k>0; --k, ++s, ++os)
    {
      os->ubnd = s->ubnd;
      os->lbnd = s->lbnd;
      os->inc = s->inc;
    }
}

SCM_DEFINE (scm_array_from_s, "array-from*", 1, 0, 1,
           (SCM ra, SCM indices),
            "Return the array slice @var{ra}[@var{indices} ..., ...]\n"
            "The rank of @var{ra} must equal to the number of indices or larger.\n\n"
            "See also @code{array-ref}, @code{array-from}, @code{array-amend!}.\n\n"
            "@code{array-from*} may return a rank-0 array. For example:\n"
            "@lisp\n"
            "(array-from* #2((1 2 3) (4 5 6)) 1 1) @result{} #0(5)\n"
            "(array-from* #2((1 2 3) (4 5 6)) 1) @result{} #(4 5 6)\n"
            "(array-from* #2((1 2 3) (4 5 6))) @result{} #2((1 2 3) (4 5 6))\n"
            "(array-from* #0(5) @result{} #0(5).\n"
            "@end lisp")
#define FUNC_NAME s_scm_array_from_s
{
  SCM o, i = indices;
  size_t ndim, k;
  ssize_t pos = 0;
  scm_t_array_handle handle;
  scm_t_array_dim *s;
  scm_array_get_handle (ra, &handle);
  array_from_pos (&handle, &ndim, &k, &i, &pos, &s, FUNC_NAME, scm_list_2 (ra, indices));
  if (k==ndim)
    o = ra;
  else if (scm_is_null (i))
    {
      array_from_get_o(&handle, k, s, pos, &o);
    }
  else
    {
      scm_array_handle_release (&handle);
      scm_misc_error(FUNC_NAME, "too many indices", scm_list_2 (ra, indices));
    }
  scm_array_handle_release (&handle);
  return o;
}
#undef FUNC_NAME


SCM_DEFINE (scm_array_from, "array-from", 1, 0, 1,
           (SCM ra, SCM indices),
            "Return the element at the @code{(@var{indices} ...)} position\n"
            "in array @var{ra}, or the array slice @var{ra}[@var{indices} ..., ...]\n"
            "if the rank of @var{ra} is larger than the number of indices.\n\n"
            "See also @code{array-ref}, @code{array-from*}, @code{array-amend!}.\n\n"
            "@code{array-from} never returns a rank 0 array. For example:\n"
            "@lisp\n"
            "(array-from #2((1 2 3) (4 5 6)) 1 1) @result{} 5\n"
            "(array-from #2((1 2 3) (4 5 6)) 1) @result{} #(4 5 6)\n"
            "(array-from #2((1 2 3) (4 5 6))) @result{} #2((1 2 3) (4 5 6))\n"
            "(array-from #0(5) @result{} 5.\n"
            "@end lisp")
#define FUNC_NAME s_scm_array_from
{
  SCM o, i = indices;
  size_t ndim, k;
  ssize_t pos = 0;
  scm_t_array_handle handle;
  scm_t_array_dim *s;
  scm_array_get_handle (ra, &handle);
  array_from_pos (&handle, &ndim, &k, &i, &pos, &s, FUNC_NAME, scm_list_2 (ra, indices));
  if (k>0)
    {
      if (k==ndim)
        o = ra;
      else
        array_from_get_o(&handle, k, s, pos, &o);
    }
  else if (scm_is_null(i))
    o = scm_array_handle_ref (&handle, pos);
  else
    {
      scm_array_handle_release (&handle);
      scm_misc_error(FUNC_NAME, "too many indices", scm_list_2 (ra, indices));
    }
  scm_array_handle_release (&handle);
  return o;
}
#undef FUNC_NAME


SCM_DEFINE (scm_array_amend_x, "array-amend!", 2, 0, 1,
            (SCM ra, SCM b, SCM indices),
            "Set the array slice @var{ra}[@var{indices} ..., ...] to @var{b}\n."
            "Equivalent to @code{(array-copy! @var{b} (apply array-from @var{ra} @var{indices}))}\n"
            "if the number of indices is smaller than the rank of @var{ra}; otherwise\n"
            "equivalent to @code{(apply array-set! @var{ra} @var{b} @var{indices})}.\n"
            "This function returns the modified array @var{ra}.\n\n"
            "See also @code{array-ref}, @code{array-from}, @code{array-from*}.\n\n"
            "For example:\n"
            "@lisp\n"
            "(define A (list->array 2 '((1 2 3) (4 5 6))))\n"
            "(array-amend! A #0(99) 1 1) @result{} #2((1 2 3) (4 #0(99) 6))\n"
            "(array-amend! A 99 1 1) @result{} #2((1 2 3) (4 99 6))\n"
            "(array-amend! A #(a b c) 0) @result{} #2((a b c) (4 99 6))\n"
            "(array-amend! A #2((x y z) (9 8 7))) @result{} #2((x y z) (9 8 7))\n\n"
            "(define B (make-array 0))\n"
            "(array-amend! B 15) @result{} #0(15)\n"
            "@end lisp")
#define FUNC_NAME s_scm_array_amend_x
{
  SCM o, i = indices;
  size_t ndim, k;
  ssize_t pos = 0;
  scm_t_array_handle handle;
  scm_t_array_dim *s;
  scm_array_get_handle (ra, &handle);
  array_from_pos (&handle, &ndim, &k, &i, &pos, &s, FUNC_NAME, scm_list_3 (ra, b, indices));
  if (k>0)
    {
      if (k==ndim)
        o = ra;
      else
        array_from_get_o(&handle, k, s, pos, &o);
      scm_array_handle_release(&handle);
      /* an error is still possible here if o and b don't match. */
      /* FIXME copying like this wastes the handle, and the bounds matching
         behavior of array-copy! is not strict. */
      scm_array_copy_x(b, o);
    }
  else if (scm_is_null(i))
    {
      scm_array_handle_set (&handle, pos, b);  /* ra may be non-ARRAYP */
      scm_array_handle_release (&handle);
    }
  else
    {
      scm_array_handle_release (&handle);
      scm_misc_error(FUNC_NAME, "too many indices", scm_list_3 (ra, b, indices));
    }
  return ra;
}
#undef FUNC_NAME


#undef ARRAY_FROM_GET_O


/* args are RA . DIMS */
SCM_DEFINE (scm_transpose_array, "transpose-array", 1, 0, 1,
           (SCM ra, SCM args),
	    "Return an array sharing contents with @var{ra}, but with\n"
	    "dimensions arranged in a different order.  There must be one\n"
	    "@var{dim} argument for each dimension of @var{ra}.\n"
	    "@var{dim0}, @var{dim1}, @dots{} should be integers between 0\n"
	    "and the rank of the array to be returned.  Each integer in that\n"
	    "range must appear at least once in the argument list.\n"
	    "\n"
	    "The values of @var{dim0}, @var{dim1}, @dots{} correspond to\n"
	    "dimensions in the array to be returned, their positions in the\n"
	    "argument list to dimensions of @var{ra}.  Several @var{dim}s\n"
	    "may have the same value, in which case the returned array will\n"
	    "have smaller rank than @var{ra}.\n"
	    "\n"
	    "@lisp\n"
	    "(transpose-array '#2((a b) (c d)) 1 0) @result{} #2((a c) (b d))\n"
	    "(transpose-array '#2((a b) (c d)) 0 0) @result{} #1(a d)\n"
	    "(transpose-array '#3(((a b c) (d e f)) ((1 2 3) (4 5 6))) 1 1 0) @result{}\n"
	    "                #2((a 4) (b 5) (c 6))\n"
	    "@end lisp")
#define FUNC_NAME s_scm_transpose_array
{
  SCM res, vargs;
  scm_t_array_dim *s, *r;
  int ndim, i, k;

  SCM_VALIDATE_REST_ARGUMENT (args);
  SCM_ASSERT (SCM_HEAP_OBJECT_P (ra), ra, SCM_ARG1, FUNC_NAME);

  switch (scm_c_array_rank (ra))
    {
    case 0:
      if (!scm_is_null (args))
	SCM_WRONG_NUM_ARGS ();
      return ra;
    case 1:
      /* Make sure that we are called with a single zero as
	 arguments.
      */
      if (scm_is_null (args) || !scm_is_null (SCM_CDR (args)))
	SCM_WRONG_NUM_ARGS ();
      SCM_VALIDATE_INT_COPY (SCM_ARG2, SCM_CAR (args), i);
      SCM_ASSERT_RANGE (SCM_ARG2, SCM_CAR (args), i == 0);
      return ra;
    default:
      vargs = scm_vector (args);
      if (SCM_SIMPLE_VECTOR_LENGTH (vargs) != SCM_I_ARRAY_NDIM (ra))
	SCM_WRONG_NUM_ARGS ();
      ndim = 0;
      for (k = 0; k < SCM_I_ARRAY_NDIM (ra); k++)
	{
	  i = scm_to_signed_integer (SCM_SIMPLE_VECTOR_REF (vargs, k),
				     0, SCM_I_ARRAY_NDIM(ra));
	  if (ndim < i)
	    ndim = i;
	}
      ndim++;
      res = scm_i_make_array (ndim);
      SCM_I_ARRAY_SET_V (res, SCM_I_ARRAY_V (ra));
      SCM_I_ARRAY_SET_BASE (res, SCM_I_ARRAY_BASE (ra));
      for (k = ndim; k--;)
	{
	  SCM_I_ARRAY_DIMS (res)[k].lbnd = 0;
	  SCM_I_ARRAY_DIMS (res)[k].ubnd = -1;
	}
      for (k = SCM_I_ARRAY_NDIM (ra); k--;)
	{
	  i = scm_to_int (SCM_SIMPLE_VECTOR_REF (vargs, k));
	  s = &(SCM_I_ARRAY_DIMS (ra)[k]);
	  r = &(SCM_I_ARRAY_DIMS (res)[i]);
	  if (r->ubnd < r->lbnd)
	    {
	      r->lbnd = s->lbnd;
	      r->ubnd = s->ubnd;
	      r->inc = s->inc;
	      ndim--;
	    }
	  else
	    {
	      if (r->ubnd > s->ubnd)
		r->ubnd = s->ubnd;
	      if (r->lbnd < s->lbnd)
		{
		  SCM_I_ARRAY_SET_BASE (res, SCM_I_ARRAY_BASE (res) + (s->lbnd - r->lbnd) * r->inc);
		  r->lbnd = s->lbnd;
		}
	      r->inc += s->inc;
	    }
	}
      if (ndim > 0)
	SCM_MISC_ERROR ("bad argument list", SCM_EOL);
      scm_i_ra_set_contp (res);
      return res;
    }
}
#undef FUNC_NAME

/* attempts to unroll an array into a one-dimensional array.
   returns the unrolled array or #f if it can't be done.  */
/* if strict is true, return #f if returned array
   wouldn't have contiguous elements.  */
SCM_DEFINE (scm_array_contents, "array-contents", 1, 1, 0,
           (SCM ra, SCM strict),
	    "If @var{ra} may be @dfn{unrolled} into a one dimensional shared\n"
	    "array without changing their order (last subscript changing\n"
	    "fastest), then @code{array-contents} returns that shared array,\n"
	    "otherwise it returns @code{#f}.  All arrays made by\n"
	    "@code{make-array} and @code{make-uniform-array} may be unrolled,\n"
	    "some arrays made by @code{make-shared-array} may not be.  If\n"
	    "the optional argument @var{strict} is provided, a shared array\n"
	    "will be returned only if its elements are stored contiguously\n"
	    "in memory.")
#define FUNC_NAME s_scm_array_contents
{
  if (SCM_I_ARRAYP (ra))
    {
      SCM v;
      size_t ndim = SCM_I_ARRAY_NDIM (ra);
      scm_t_array_dim *s = SCM_I_ARRAY_DIMS (ra);
      size_t k = ndim;
      size_t len = 1;

      if (k)
        {
          ssize_t last_inc = s[k - 1].inc;
          while (k--)
            {
              if (len*last_inc != s[k].inc)
                return SCM_BOOL_F;
              len *= (s[k].ubnd - s[k].lbnd + 1);
            }
        }

      if (!SCM_UNBNDP (strict) && scm_is_true (strict))
	{
	  if (ndim && (1 != s[ndim - 1].inc))
	    return SCM_BOOL_F;
	  if (scm_is_bitvector (SCM_I_ARRAY_V (ra))
              && (len != scm_c_bitvector_length (SCM_I_ARRAY_V (ra)) ||
                  SCM_I_ARRAY_BASE (ra) % SCM_LONG_BIT ||
                  len % SCM_LONG_BIT))
            return SCM_BOOL_F;
	}

      v = SCM_I_ARRAY_V (ra);
      if ((len == scm_c_array_length (v)) && (0 == SCM_I_ARRAY_BASE (ra)))
          return v;
      else
        {
          SCM sra = scm_i_make_array (1);
          SCM_I_ARRAY_DIMS (sra)->lbnd = 0;
          SCM_I_ARRAY_DIMS (sra)->ubnd = len - 1;
          SCM_I_ARRAY_SET_V (sra, v);
          SCM_I_ARRAY_SET_BASE (sra, SCM_I_ARRAY_BASE (ra));
          SCM_I_ARRAY_DIMS (sra)->inc = (ndim ? SCM_I_ARRAY_DIMS (ra)[ndim - 1].inc : 1);
          return sra;
        }
    }
  else if (scm_is_array (ra))
    return ra;
  else
    scm_wrong_type_arg_msg (NULL, 0, ra, "array");
}
#undef FUNC_NAME


static void
list_to_array (SCM lst, scm_t_array_handle *handle, ssize_t pos, size_t k)
{
  if (k == scm_array_handle_rank (handle))
    scm_array_handle_set (handle, pos, lst);
  else
    {
      scm_t_array_dim *dim = scm_array_handle_dims (handle) + k;
      ssize_t inc = dim->inc;
      size_t len = 1 + dim->ubnd - dim->lbnd, n;
      char *errmsg = NULL;

      n = len;
      while (n > 0 && scm_is_pair (lst))
	{
	  list_to_array (SCM_CAR (lst), handle, pos, k + 1);
	  pos += inc;
	  lst = SCM_CDR (lst);
	  n -= 1;
	}
      if (n != 0)
	errmsg = "too few elements for array dimension ~a, need ~a";
      if (!scm_is_null (lst))
	errmsg = "too many elements for array dimension ~a, want ~a";
      if (errmsg)
	scm_misc_error (NULL, errmsg, scm_list_2 (scm_from_size_t (k),
						  scm_from_size_t (len)));
    }
}


SCM_DEFINE (scm_list_to_typed_array, "list->typed-array", 3, 0, 0,
           (SCM type, SCM shape, SCM lst),
	    "Return an array of the type @var{type}\n"
	    "with elements the same as those of @var{lst}.\n"
	    "\n"
	    "The argument @var{shape} determines the number of dimensions\n"
	    "of the array and their shape.  It is either an exact integer,\n"
	    "giving the\n"
	    "number of dimensions directly, or a list whose length\n"
	    "specifies the number of dimensions and each element specified\n"
	    "the lower and optionally the upper bound of the corresponding\n"
	    "dimension.\n"
	    "When the element is list of two elements, these elements\n"
	    "give the lower and upper bounds.  When it is an exact\n"
	    "integer, it gives only the lower bound.")
#define FUNC_NAME s_scm_list_to_typed_array
{
  SCM row;
  SCM ra;
  scm_t_array_handle handle;

  row = lst;
  if (scm_is_integer (shape))
    {
      size_t k = scm_to_size_t (shape);
      shape = SCM_EOL;
      while (k-- > 0)
	{
	  shape = scm_cons (scm_length (row), shape);
	  if (k > 0 && !scm_is_null (row))
	    row = scm_car (row);
	}
    }
  else
    {
      SCM shape_spec = shape;
      shape = SCM_EOL;
      while (1)
	{
	  SCM spec = scm_car (shape_spec);
	  if (scm_is_pair (spec))
	    shape = scm_cons (spec, shape);
	  else
	    shape = scm_cons (scm_list_2 (spec,
					  scm_sum (scm_sum (spec,
							    scm_length (row)),
						   scm_from_int (-1))),
			      shape);
	  shape_spec = scm_cdr (shape_spec);
	  if (scm_is_pair (shape_spec))
	    {
	      if (!scm_is_null (row))
		row = scm_car (row);
	    }
	  else
	    break;
	}
    }

  ra = scm_make_typed_array (type, SCM_UNSPECIFIED,
			     scm_reverse_x (shape, SCM_EOL));

  scm_array_get_handle (ra, &handle);
  list_to_array (lst, &handle, 0, 0);
  scm_array_handle_release (&handle);

  return ra;
}
#undef FUNC_NAME

SCM_DEFINE (scm_list_to_array, "list->array", 2, 0, 0,
           (SCM ndim, SCM lst),
	    "Return an array with elements the same as those of @var{lst}.")
#define FUNC_NAME s_scm_list_to_array
{
  return scm_list_to_typed_array (SCM_BOOL_T, ndim, lst);
}
#undef FUNC_NAME

/* Print dimension DIM of ARRAY.
 */

static int
scm_i_print_array_dimension (scm_t_array_handle *h, int dim, int pos,
			     SCM port, scm_print_state *pstate)
{
  if (dim == h->ndims)
    scm_iprin1 (scm_array_handle_ref (h, pos), port, pstate);
  else
    {
      ssize_t i;
      scm_putc ('(', port);
      for (i = h->dims[dim].lbnd; i <= h->dims[dim].ubnd;
           i++, pos += h->dims[dim].inc)
        {
          scm_i_print_array_dimension (h, dim+1, pos, port, pstate);
          if (i < h->dims[dim].ubnd)
            scm_putc (' ', port);
        }
      scm_putc (')', port);
    }
  return 1;
}

/* Print an array.
*/

int
scm_i_print_array (SCM array, SCM port, scm_print_state *pstate)
{
  scm_t_array_handle h;
  size_t i;
  int print_lbnds = 0, zero_size = 0, print_lens = 0;

  scm_array_get_handle (array, &h);

  scm_putc ('#', port);
  if (SCM_I_ARRAYP (array))
    scm_intprint (h.ndims, 10, port);
  if (h.element_type != SCM_ARRAY_ELEMENT_TYPE_SCM)
    scm_write (scm_array_handle_element_type (&h), port);

  for (i = 0; i < h.ndims; i++)
    {
      if (h.dims[i].lbnd != 0)
	print_lbnds = 1;
      if (h.dims[i].ubnd - h.dims[i].lbnd + 1 == 0)
	zero_size = 1;
      else if (zero_size)
	print_lens = 1;
    }

  if (print_lbnds || print_lens)
    for (i = 0; i < h.ndims; i++)
      {
	if (print_lbnds)
	  {
	    scm_putc ('@', port);
	    scm_intprint (h.dims[i].lbnd, 10, port);
	  }
	if (print_lens)
	  {
	    scm_putc (':', port);
	    scm_intprint (h.dims[i].ubnd - h.dims[i].lbnd + 1,
			  10, port);
	  }
      }

  if (h.ndims == 0)
    {
      /* Rank zero arrays, which are really just scalars, are printed
	 specially.  The consequent way would be to print them as

            #0 OBJ

         where OBJ is the printed representation of the scalar, but we
         print them instead as

            #0(OBJ)

         to make them look less strange.

	 Just printing them as

            OBJ

         would be correct in a way as well, but zero rank arrays are
         not really the same as Scheme values since they are boxed and
         can be modified with array-set!, say.
      */
      scm_putc ('(', port);
      scm_i_print_array_dimension (&h, 0, 0, port, pstate);
      scm_putc (')', port);
      return 1;
    }
  else
    return scm_i_print_array_dimension (&h, 0, 0, port, pstate);
}

void
scm_init_arrays ()
{
  scm_add_feature ("array");

#include "libguile/arrays.x"

}

/*
  Local Variables:
  c-file-style: "gnu"
  End:
*/
