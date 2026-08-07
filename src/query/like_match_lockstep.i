/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * like_match_lockstep.i - codeset-parameterized byte-lockstep LIKE matcher body,
 *                         following PostgreSQL's like_match.c template pattern
 *
 * This file is included by string_opfunc.c once per eligible codeset. Before
 * each inclusion the following macros must be defined; all of them are
 * undefined again at the end of this file.
 *
 * LOCKSTEP_FN_NAME
 *   Name of the generated static matcher function.
 *
 * LOCKSTEP_NEXT_CHAR(p, remain)
 *   Statement. Advance `p` (const unsigned char *) by exactly one character of
 *   the instance codeset and decrease `remain` (int) by the same byte count.
 *   Precondition: remain >= 1 and p is on a character boundary. Postcondition:
 *   a character truncated by the buffer end must leave remain < 0 with p at
 *   the buffer end (callers treat remain < 0 as an exhausted invalid tail).
 *
 * LOCKSTEP_BYTE_EQ(b1, b2)
 *   Expression. 1:1 byte equivalence used for the literal lockstep walk.
 *   Must be reflexive and symmetric, and every non-trivial equivalence class
 *   must consist of single bytes only (variable-length classes are handled by
 *   LOCKSTEP_MISMATCH_RESYNC instead). The {0x00, 0x20} space-NUL class of
 *   identity-weight collations belongs here; BINARY uses plain equality.
 *
 * LOCKSTEP_CAND_EQ(t, tlen, pat, patlen)
 *   Expression. '%'-scan candidate predicate for the per-character walk.
 *   `t`/`tlen` is a target position on a character boundary (tlen >= 1) and
 *   `pat`/`patlen` is the first pattern character after '%'-run collapsing and
 *   escape skipping (patlen >= 1). Must return nonzero iff `t` can start a
 *   collation-equivalent occurrence of the pattern character. False positives
 *   are filtered by the recursive lockstep walk and are therefore harmless;
 *   false negatives lose matches and are forbidden. Must not advance pointers.
 *
 * LOCKSTEP_MISMATCH_RESYNC(t, tlen, p, plen)
 *   Statement. Runs when LOCKSTEP_BYTE_EQ failed on a literal byte pair.
 *   The default (all fixed-width instances) is `return LOCKSTEP_FALSE`.
 *   A codeset with variable-length equivalence classes (EUC-KR space
 *   0xA1 0xA1 vs ASCII space/NUL) may instead consume one equivalent
 *   character from BOTH sides, each by its own element length, and fall
 *   through to continue the walk; when the pair is not equivalent it must
 *   `return QSTR_LIKE_LOCKSTEP_FALSE`.
 *
 * LOCKSTEP_MEMCHR_OK(firstpat)
 *   Expression. Nonzero when a memchr () scan on `firstpat` enumerates a
 *   superset of the character-boundary candidate positions (i.e. a raw byte
 *   hit can never fall inside a multi-byte character of the codeset, or a
 *   false candidate is otherwise impossible). When zero, the matcher falls
 *   back to the per-character LOCKSTEP_CAND_EQ walk.
 *
 * LOCKSTEP_TAIL_IS_PAD(t, tlen)
 *   Expression. Once the pattern is exhausted, returns the byte length of a
 *   pad/space element at `t` (tlen >= 1) that may be ignored at the end of
 *   the target, or 0 when `t` does not start such an element. Mirrors the
 *   trailing-pad rule of the generic qstr_eval_like () loop for the codeset.
 *
 * The function body itself is codeset-agnostic: every UTF-8 assumption of the
 * original matcher is expressed through the macros above. The UTF8 instance
 * reproduces the pre-template code literally (same candidate sets, same
 * early-abort behavior, same trailing-space handling).
 */

static int
LOCKSTEP_FN_NAME (const unsigned char *t, int tlen, const unsigned char *p, int plen, int escape_byte, int depth)
{
  if (depth > QSTR_LIKE_LOCKSTEP_MAX_DEPTH)
    {
      return QSTR_LIKE_LOCKSTEP_FALLBACK;
    }

  /* fast path for a match-everything pattern */
  if (plen == 1 && *p == LIKE_WILDCARD_MATCH_MANY)
    {
      return QSTR_LIKE_LOCKSTEP_TRUE;
    }

  while (tlen > 0 && plen > 0)
    {
      if (*p == LIKE_WILDCARD_MATCH_MANY)
	{
	  const unsigned char *pat;
	  int patlen;
	  unsigned char firstpat;

	  /* collapse the wildcard run after '%' (N '_' plus '%'s == N '_' plus one '%'),
	   * so the recursive scan below always starts at a regular or escaped literal */
	  p++;
	  plen--;
	  while (plen > 0)
	    {
	      if (*p == LIKE_WILDCARD_MATCH_MANY)
		{
		  p++;
		  plen--;
		}
	      else if (*p == LIKE_WILDCARD_MATCH_ONE)
		{
		  if (tlen <= 0)
		    {
		      return QSTR_LIKE_LOCKSTEP_ABORT;
		    }
		  LOCKSTEP_NEXT_CHAR (t, tlen);
		  if (tlen < 0)
		    {
		      /* truncated last character (invalid byte sequence) */
		      return QSTR_LIKE_LOCKSTEP_FALSE;
		    }
		  p++;
		  plen--;
		}
	      else
		{
		  break;
		}
	    }

	  if (plen <= 0)
	    {
	      /* a trailing '%' matches all remaining target */
	      return QSTR_LIKE_LOCKSTEP_TRUE;
	    }

	  /* scan for a target position at which the rest of the pattern can
	   * match; candidate positions are pre-filtered with the first
	   * literal pattern character to avoid recursing more than necessary */
	  if (escape_byte >= 0 && *p == escape_byte && plen >= 2)
	    {
	      pat = p + 1;
	      patlen = plen - 1;
	    }
	  else
	    {
	      pat = p;
	      patlen = plen;
	    }
	  firstpat = pat[0];

	  if (LOCKSTEP_MEMCHR_OK (firstpat))
	    {
	      /* every memchr () hit is a character start under the instance's
	       * MEMCHR_OK condition : same candidate set as the per-character walk */
	      while (tlen > 0)
		{
		  const unsigned char *hit = (const unsigned char *) memchr (t, firstpat, tlen);
		  /* the only single-byte equivalence partner is the {0x00, 0x20} class;
		   * instances whose BYTE_EQ lacks that class fold `partner` to -1 at compile time */
		  const int partner =
		    (LOCKSTEP_BYTE_EQ (0x00, 0x20) && (firstpat == 0x00 || firstpat == 0x20))
		    ? (int) (firstpat ^ 0x20) : -1;
		  int matched;

		  if (hit == NULL && partner < 0)
		    {
		      break;
		    }
		  if (hit != NULL && partner >= 0)
		    {
		      /* space and NUL match each other : take the earlier candidate */
		      const unsigned char *hit2 =
			(const unsigned char *) memchr (t, partner, CAST_BUFLEN (hit - t));
		      if (hit2 != NULL)
			{
			  hit = hit2;
			}
		    }
		  else if (hit == NULL)
		    {
		      /* firstpat is space or NUL : its class partner may still match */
		      hit = (const unsigned char *) memchr (t, partner, tlen);
		      if (hit == NULL)
			{
			  break;
			}
		    }

		  tlen -= CAST_BUFLEN (hit - t);
		  t = hit;

		  matched = LOCKSTEP_FN_NAME (t, tlen, p, plen, escape_byte, depth + 1);
		  if (matched != QSTR_LIKE_LOCKSTEP_FALSE)
		    {
		      return matched;	/* _TRUE, _ABORT or _FALLBACK */
		    }
		  t++;
		  tlen--;
		}
	    }
	  else
	    {
	      /* per-character candidate walk */
	      while (tlen > 0)
		{
		  if (LOCKSTEP_CAND_EQ (t, tlen, pat, patlen))
		    {
		      int matched = LOCKSTEP_FN_NAME (t, tlen, p, plen, escape_byte, depth + 1);

		      if (matched != QSTR_LIKE_LOCKSTEP_FALSE)
			{
			  return matched;	/* _TRUE, _ABORT or _FALLBACK */
			}
		    }
		  LOCKSTEP_NEXT_CHAR (t, tlen);
		}
	    }

	  /* end of target with no match : no later position can match */
	  return QSTR_LIKE_LOCKSTEP_ABORT;
	}
      else if (*p == LIKE_WILDCARD_MATCH_ONE)
	{
	  LOCKSTEP_NEXT_CHAR (t, tlen);
	  if (tlen < 0)
	    {
	      /* truncated last character (invalid byte sequence) */
	      return QSTR_LIKE_LOCKSTEP_FALSE;
	    }
	  p++;
	  plen--;
	  continue;
	}
      else
	{
	  if (escape_byte >= 0 && *p == escape_byte && plen >= 2)
	    {
	      /* escaped pattern byte matches literally; a trailing escape stays a normal character */
	      p++;
	      plen--;
	    }
	  if (!LOCKSTEP_BYTE_EQ (*p, *t))
	    {
	      /* variable-length equivalence rescue; the default is `return _FALSE` */
	      LOCKSTEP_MISMATCH_RESYNC (t, tlen, p, plen);
	    }
	  else
	    {
	      t++;
	      tlen--;
	      p++;
	      plen--;
	    }
	}
    }

  if (tlen > 0)
    {
      /* pattern exhausted : the target still matches if only trailing pad
       * elements remain (same rule as the generic loop) */
      int pad_adv;

      assert (plen <= 0);
      while (tlen > 0 && (pad_adv = LOCKSTEP_TAIL_IS_PAD (t, tlen)) > 0)
	{
	  t += pad_adv;
	  tlen -= pad_adv;
	}
      return (tlen == 0) ? QSTR_LIKE_LOCKSTEP_TRUE : QSTR_LIKE_LOCKSTEP_FALSE;
    }

  /* target exhausted : match iff the remaining pattern is zero or more '%' */
  while (plen > 0 && *p == LIKE_WILDCARD_MATCH_MANY)
    {
      p++;
      plen--;
    }
  return (plen <= 0) ? QSTR_LIKE_LOCKSTEP_TRUE : QSTR_LIKE_LOCKSTEP_ABORT;
}

#undef LOCKSTEP_FN_NAME
#undef LOCKSTEP_NEXT_CHAR
#undef LOCKSTEP_BYTE_EQ
#undef LOCKSTEP_CAND_EQ
#undef LOCKSTEP_MISMATCH_RESYNC
#undef LOCKSTEP_MEMCHR_OK
#undef LOCKSTEP_TAIL_IS_PAD
