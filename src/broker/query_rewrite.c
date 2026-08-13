/*
 *
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
 * query_rewrite.c - query rewrite at prepare time (see query_rewrite.h)
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if !defined(WINDOWS)
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#endif

/* used by qr_parse_file, which is compiled outside the !WINDOWS guard above */
#include <errno.h>

#include "porting.h"
#include "query_rewrite.h"
#include "error_manager.h"
/* T_CAS_ERROR_CODE / CAS_ER_DBMS: qr_exec_error_tier must tell a CAS layer error code
 * apart from a server one (the two namespaces are disjoint but share err_info.err_number) */
#include "cas_error.h"
/* for the build-time statement-type validation of rewrite rules (parse only, no
 * DB connection): parser_parse_string / pt_is_ddl_statement / PT_NODE / lang_init */
#include "dbi.h"
#include "parse_tree.h"
#include "parser.h"
#include "language_support.h"

/* free and nullify: this TU does not pull in memory_alloc.h's free_and_init, so
 * provide the same guarded macro locally (matching broker_log_replay.c). */
#ifndef free_and_init
#define free_and_init(ptr) \
        do { \
          if ((ptr)) { \
            free ((ptr)); \
            (ptr) = NULL; \
          } \
        } while (0)
#endif

/* same definition as cas_network.h, which this TU does not include */
#ifndef MIN
#define MIN(X, Y)	((X) < (Y) ? (X) : (Y))
#endif

/* ------------------------------------------------------------------ *
 * common string helpers (used by both broker build and cas lookup)   *
 * ------------------------------------------------------------------ */

/* copy src into dst (size dst_size) upper-casing every character */
static void
qr_upper_copy (char *dst, const char *src, int dst_size)
{
  int i;

  for (i = 0; i < dst_size - 1 && src[i] != '\0'; i++)
    {
      dst[i] = (char) toupper ((unsigned char) src[i]);
    }
  dst[i] = '\0';
}

/* djb2 hash of a plain string */
static unsigned int
qr_hash_str (const char *s)
{
  unsigned int h = 5381;

  for (; *s != '\0'; s++)
    {
      h = ((h << 5) + h) ^ (unsigned char) *s;
    }
  return h;
}

/* character-class flags used by the query normalizer (qr_normalize_query) */
#define QR_CC_WS      0x01	/* whitespace */
#define QR_CC_QUOTE   0x02	/* ' or "  : string-literal delimiter */
#define QR_CC_CMT     0x04	/* / or -  : possible comment or hint opener */

/* qr_norm_char_class[b] = OR of the QR_CC_* flags for byte b, built once at
 * process startup so both the CAS lookup path and the broker rule-build path
 * index it directly with no per-call setup or per-character multi-compare. */
static unsigned char qr_norm_char_class[256];

static bool
qr_build_norm_char_class (void)
{
  qr_norm_char_class[(unsigned char) ' '] = QR_CC_WS;
  qr_norm_char_class[(unsigned char) '\t'] = QR_CC_WS;
  qr_norm_char_class[(unsigned char) '\n'] = QR_CC_WS;
  qr_norm_char_class[(unsigned char) '\r'] = QR_CC_WS;
  qr_norm_char_class[(unsigned char) '\f'] = QR_CC_WS;
  qr_norm_char_class[(unsigned char) '\v'] = QR_CC_WS;
  qr_norm_char_class[(unsigned char) '\''] = QR_CC_QUOTE;
  qr_norm_char_class[(unsigned char) '"'] = QR_CC_QUOTE;
  qr_norm_char_class[(unsigned char) '/'] = QR_CC_CMT;
  qr_norm_char_class[(unsigned char) '-'] = QR_CC_CMT;

  return true;
}

static bool qr_norm_char_class_ready = qr_build_norm_char_class ();

static int
qr_normalize_query (char *dst, int dst_size, const char *src, unsigned int *dst_hash, bool * unterminated)
{
  int len = 0;
  int prev_space = 1;		/* drop leading whitespace */
  char quote = 0;		/* current open quote char, 0 = outside literal */
  const char *s;
  unsigned int h = 5381;

  /* set when a hint or block comment is not closed: the normalizer then truncates (or
   * fabricates a closer).  rule loading rejects such text; lookup passes NULL and
   * tolerates the fabricated closer. */
  if (unterminated != NULL)
    {
      *unterminated = false;
    }

  /* emit one char to dst with bounds check + rolling hash */
#define QR_PUT(ch) \
  do { \
    if (len >= dst_size - 1) \
      { \
        return -1; \
      } \
    dst[len++] = (char) (ch); \
    h = ((h << 5) + h) ^ (unsigned char) (ch); \
  } while (0)

  for (s = src; *s != '\0'; s++)
    {
      char c = *s;

      if (quote != 0)
	{
	  /* inside a string literal: copy verbatim AND hash, so the returned
	   * hash equals qr_hash_str(dst) even when the query contains a literal. */
	  QR_PUT (c);
	  if (c == quote)
	    {
	      if (*(s + 1) == quote)
		{
		  /* doubled quote = escaped quote, stay inside the literal */
		  QR_PUT (quote);
		  s++;
		}
	      else
		{
		  quote = 0;
		}
	    }
	  prev_space = 0;
	  continue;
	}

      unsigned char cls = qr_norm_char_class[(unsigned char) c];

      if (cls & QR_CC_QUOTE)
	{
	  /* entering a literal: emit a pending separator space first */
	  if (prev_space && len > 0)
	    {
	      QR_PUT (' ');
	    }
	  QR_PUT (c);
	  quote = c;
	  prev_space = 0;
	  continue;
	}

      if (cls & QR_CC_CMT)
	{
	  /* c is '/' or '-'.  a comment opens with slash-star, slash-slash or
	   * dash-dash; a lone '/' or '-' is an operator (falls through to the
	   * ordinary handling below).  a regular comment is stripped (acts as a
	   * separator); a HINT comment (opener followed by '+') is preserved with
	   * its inner whitespace collapsed, because it can change the plan. */
	  int block = (c == '/' && *(s + 1) == '*');
	  int line = ((c == '/' && *(s + 1) == '/') || (c == '-' && *(s + 1) == '-'));

	  if (block || line)
	    {
	      if (*(s + 2) == '+')
		{
		  /* HINT comment: keep opener and '+', collapse inner whitespace */
		  int inner_prev_space = 1;

		  if (prev_space && len > 0)
		    {
		      QR_PUT (' ');
		    }
		  QR_PUT (c);
		  QR_PUT (*(s + 1));
		  QR_PUT ('+');
		  s += 3;	/* skip opener and the '+' */

		  if (block)
		    {
		      for (; *s != '\0' && !(*s == '*' && *(s + 1) == '/'); s++)
			{
			  if (qr_norm_char_class[(unsigned char) *s] & QR_CC_WS)
			    {
			      inner_prev_space = 1;
			    }
			  else
			    {
			      if (inner_prev_space)
				{
				  QR_PUT (' ');
				}
			      QR_PUT (*s);
			      inner_prev_space = 0;
			    }
			}
		      QR_PUT ('*');
		      QR_PUT ('/');
		      if (*s != '\0')
			{
			  s++;	/* at the closing slash; loop s++ steps past it */
			}
		      else
			{
			  if (unterminated != NULL)
			    {
			      *unterminated = true;
			    }
			  s--;	/* unterminated; loop s++ re-reads the terminator */
			}
		    }
		  else
		    {
		      for (; *s != '\0' && *s != '\n'; s++)
			{
			  if (qr_norm_char_class[(unsigned char) *s] & QR_CC_WS)
			    {
			      inner_prev_space = 1;
			    }
			  else
			    {
			      if (inner_prev_space)
				{
				  QR_PUT (' ');
				}
			      QR_PUT (*s);
			      inner_prev_space = 0;
			    }
			}
		      /* the newline terminates a line hint, so it must survive: folded into a
		       * space it would let the hint swallow the rest of the statement, and
		       * qr_check_rewrite_policy parses this text.  the key stays consistent
		       * because the broker and the CAS both normalize with this function. */
		      QR_PUT ('\n');
		      s--;	/* loop s++ re-reads the newline or terminator */
		    }
		  prev_space = 1;	/* separate the hint from the next token */
		  continue;
		}
	      else
		{
		  /* regular comment: skip it, leaving a separator */
		  if (block)
		    {
		      s += 2;	/* skip the block-comment opener */
		      while (*s != '\0' && !(*s == '*' && *(s + 1) == '/'))
			{
			  s++;
			}
		      if (*s != '\0')
			{
			  s++;	/* at the closing slash; loop s++ steps past it */
			}
		      else
			{
			  if (unterminated != NULL)
			    {
			      *unterminated = true;
			    }
			  s--;	/* unterminated; loop s++ re-reads the terminator */
			}
		    }
		  else
		    {
		      s += 2;	/* skip the line-comment opener */
		      while (*s != '\0' && *s != '\n')
			{
			  s++;
			}
		      s--;	/* loop s++ re-reads the newline or terminator */
		    }
		  prev_space = 1;
		  continue;
		}
	    }
	  /* lone '/' or '-' operator: fall through to ordinary handling */
	}

      if (cls & QR_CC_WS)
	{
	  prev_space = 1;
	  continue;
	}

      if (prev_space && len > 0)
	{
	  QR_PUT (' ');
	}
      prev_space = 0;
      QR_PUT (c);
    }
#undef QR_PUT

  dst[len] = '\0';
  *dst_hash = h;

  return len;
}

/* true if a file name ends with QR_RULE_SUFFIX (".rule").  only such files are
 * loaded as rules; renaming to any other suffix excludes a file without deleting
 * it.  the bare name ".rule" (no base name) is not accepted as a rule. */
static int
qr_has_rule_suffix (const char *name)
{
  int n = (int) strlen (name);
  int s = (int) strlen (QR_RULE_SUFFIX);

  return (n > s && strcmp (name + n - s, QR_RULE_SUFFIX) == 0);
}

/* NOTE: the authoritative marker count for a prepare comes from CUBRID's
 * get_num_markers() (cas_common_execute.c) at CAS prepare time.  the broker stores query
 * strings + BIND_MAP and counts markers from its load-time parse only to validate a rule
 * (qr_check_rewrite_policy). */

#if !defined(WINDOWS)
/* full memory barrier so a CAS reader never sees a hash-bucket head pointing at a
 * rule slot whose content has not been fully written yet. */
#define QR_BARRIER()	__sync_synchronize ()

/* per-rule string pool slot layout (fixed offsets within slot idx):
 *   [0]                         normalized original query (<= cfg_max_query_len)
 *   [cfg_max_query_len + 1]     replacement query        (<= cfg_max_query_len)
 *   [2*(cfg_max_query_len + 1)] source rulepath "user@dbname/file" (< QR_RELPATH_LEN) */
static void
qr_slot_offsets (const T_QR_SHM_HEADER * h, int idx, int *orig_off, int *rewrite_off, int *name_off)
{
  int base = idx * h->pool_slot;

  *orig_off = base;
  *rewrite_off = base + (h->cfg_max_query_len + 1);
  *name_off = base + 2 * (h->cfg_max_query_len + 1);
}

/* readers compute the slot addresses instead of dereferencing the stored offsets: the
 * values are always the functions above, so a corrupt entry cannot steer a pointer. */
static char *
qr_slot_orig (char *pool, const T_QR_SHM_HEADER * h, int idx)
{
  return pool + idx * h->pool_slot;
}

static char *
qr_slot_name (char *pool, const T_QR_SHM_HEADER * h, int idx)
{
  return pool + idx * h->pool_slot + 2 * (h->cfg_max_query_len + 1);
}

/* structural check every attach must pass before dereferencing anything: readers and admin
 * mutators alike derive all their pointers and bounds from this header, and a process of
 * the same uid could have corrupted it.  the layout is a pure function of the capacity
 * scalars, so it is recomputed and required to match exactly -- per-field range checks
 * would still let two arrays overlap and never bound the last array's end.  the owner
 * check stays with the caller: admin knows the owning id, CAS only knows the key. */
static bool
qr_shm_header_sane (const T_QR_SHM_HEADER * h, int mid)
{
  struct shmid_ds ds;
  int exp_slot, exp_bucket, exp_rule, exp_dbucket, exp_dbuser, exp_pool, exp_total;

  if (h->magic != QR_SHM_MAGIC
      || h->max_rules <= 0 || h->max_rules > QR_MAX_RULE_COUNT
      || h->rule_count < 0 || h->rule_count > h->max_rules
      || h->hash_size != QR_DEFAULT_HASH_SIZE || h->dbuser_hash_size != QR_DEFAULT_HASH_SIZE
      || h->dbuser_count < 0 || h->dbuser_count > h->max_rules
      || h->cfg_max_query_len <= 0 || h->cfg_max_query_len > QR_MAX_QUERY_LEN
      || h->min_query_len < 0 || h->max_query_len < 0 || h->max_query_len > h->cfg_max_query_len)
    {
      return false;
    }

  exp_slot = 2 * (h->cfg_max_query_len + 1) + QR_RELPATH_LEN;
  exp_bucket = (int) sizeof (T_QR_SHM_HEADER);
  exp_rule = exp_bucket + (int) sizeof (int) * h->hash_size;
  exp_dbucket = exp_rule + (int) sizeof (T_QR_RULE) * h->max_rules;
  exp_dbuser = exp_dbucket + (int) sizeof (int) * h->dbuser_hash_size;
  exp_pool = exp_dbuser + (int) sizeof (T_QR_DBUSER) * h->max_rules;
  exp_total = exp_pool + h->max_rules * exp_slot;

  if (h->pool_slot != exp_slot || h->bucket_off != exp_bucket || h->rule_off != exp_rule
      || h->dbuser_bucket_off != exp_dbucket || h->dbuser_off != exp_dbuser
      || h->pool_off != exp_pool || h->total_size != exp_total)
    {
      return false;
    }

  /* the header is self-consistent; make sure the kernel actually mapped that much */
  return (shmctl (mid, IPC_STAT, &ds) == 0 && (size_t) exp_total <= ds.shm_segsz);
}

#endif

/* ================================================================== *
 *  BROKER SIDE : build the shared memory segment                     *
 * ================================================================== */

/* a single rule parsed from a file, kept in process memory before
 * being serialized into shared memory */
typedef struct t_qr_parsed T_QR_PARSED;
struct t_qr_parsed
{
  char db_name[QR_NAME_LEN];
  char user_name[QR_NAME_LEN];
  char *orig_norm;		/* normalized original query */
  unsigned int orig_hash;	/* hash value of normalized original query */
  char *rewrite_query;		/* replacement query (block lines joined with '\n') */
  char rulepath[QR_RELPATH_LEN];	/* source file relative path "user@dbname/file" */
  int num_map_entries;		/* number of BIND_MAP entries, 0 = NONE, -1 = identity */
  int bind_map_given;		/* 1 = the rule file has a BIND_MAP line, 0 = omitted.  identity is
				 * stored the same either way, but only an omitted line is restricted
				 * to a markerless ORIG (qr_check_rewrite_policy) */
  short src_orig_pos[QR_MAX_BINDS];
};

/*
 * parse the BIND_MAP value into the parsed rule and validate its form.  accepted (keywords
 * are case-insensitive):
 *   MATCH        the replacement takes the original's markers in order -> -1 entries
 *   NONE         the replacement takes none of them                    ->  0 entries
 *   new:orig,... explicit mapping                                      ->  n entries
 * returns 0 on success, -1 on error.  NULL and "" are NOT the same: NULL means the rule has
 * no BIND_MAP line, "" means the line is there without a value ("BIND_MAP ="), which is
 * rejected -- guessing a mapping there would silently run a rule the author did not describe.
 * the counts are only checked against the real K_orig / K_rewrite in
 * qr_check_rewrite_policy, which is where the queries are parsed.
 */
static int
qr_parse_bind_map (T_QR_PARSED * p, const char *map_text)
{
  int covered[QR_MAX_BINDS];
  int i, entries = 0;
  const char *s;

  for (i = 0; i < QR_MAX_BINDS; i++)
    {
      covered[i] = 0;
    }

  /* the whole array is copied into the segment either way, so leave no uninitialized stack
   * bytes in the positions this map does not name */
  memset (p->src_orig_pos, 0, sizeof (p->src_orig_pos));

  if (map_text == NULL)
    {
      /* BIND_MAP omitted: identity mapping, resolved at CAS prepare time when the
       * authoritative K_orig / K_rewrite are known (must be equal). */
      p->num_map_entries = -1;
      return 0;
    }

  if (map_text[0] == '\0')
    {
      return -1;		/* "BIND_MAP =" with no value */
    }

  if (strcasecmp (map_text, "MATCH") == 0)
    {
      p->num_map_entries = -1;	/* same as an omitted line, but written down */
      return 0;
    }

  if (strcasecmp (map_text, "NONE") == 0)
    {
      p->num_map_entries = 0;
      return 0;
    }

  s = map_text;
  while (*s != '\0')
    {
      int rewrite_pos = 0, orig_pos = 0;

      while (*s == ' ' || *s == ',' || *s == '\t')
	{
	  s++;
	}

      if (*s == '\0')
	{
	  break;
	}

      /* strtol reports an out-of-range integer literal through ERANGE */
      {
	char *endp;
	long lv;

	errno = 0;
	lv = strtol (s, &endp, 10);
	if (endp == s || errno == ERANGE || lv < 1 || lv > QR_MAX_BINDS)
	  {
	    return -1;
	  }
	rewrite_pos = (int) lv;
	s = endp;

	while (*s == ' ' || *s == '\t')
	  {
	    s++;
	  }
	if (*s != ':')
	  {
	    return -1;
	  }
	s++;

	errno = 0;
	lv = strtol (s, &endp, 10);
	if (endp == s || errno == ERANGE || lv < 1 || lv > QR_MAX_BINDS)
	  {
	    return -1;
	  }
	orig_pos = (int) lv;
	s = endp;
      }

      /* only whitespace may sit between a pair and the next ',' or the end:
       * "1:2xyz" is rejected */
      while (*s == ' ' || *s == '\t')
	{
	  s++;
	}
      if (*s != '\0' && *s != ',')
	{
	  return -1;
	}

      /* range against K_rewrite / K_orig is checked at resolve time; here only the
       * storage bounds and the rewrite_pos uniqueness can be validated. */
      if (rewrite_pos < 1 || rewrite_pos > QR_MAX_BINDS || orig_pos < 1 || orig_pos > QR_MAX_BINDS)
	{
	  return -1;
	}

      if (covered[rewrite_pos - 1] != 0)
	{
	  return -1;		/* duplicated new position */
	}

      covered[rewrite_pos - 1] = 1;
      p->src_orig_pos[rewrite_pos - 1] = (short) orig_pos;
      entries++;
    }

  p->num_map_entries = entries;

  return 0;
}

/*
 * parse one rule file into *p. the target db/user are taken from the enclosing
 * "user@dbname" directory name (already upper-cased) and passed in by the caller.
 * returns 0 on success, -1 to skip the file (with *errmsg set to the reason
 * when errmsg != NULL, so the caller can record it in the rule error log).
 */
static int
qr_parse_file (const char *path, const char *up_db, const char *up_user, T_QR_PARSED * p, char *errmsg, int errsz)
{
  FILE *fp;
  char line[QR_MAX_QUERY_LEN];
  char orig_raw[QR_MAX_QUERY_LEN];
  char rewrite_raw[QR_MAX_QUERY_LEN];
  char bind_map[QR_MAX_QUERY_LEN];
  char norm[QR_MAX_QUERY_LEN];
  int has_orig = 0, has_rewrite = 0, has_bind_map = 0;
  int saw_end;			/* the block was terminated by END, not by EOF */
  bool unterminated = false;	/* ORIG had an unclosed hint / block comment */
  int orig_len = 0, rewrite_len = 0, norm_len;
  unsigned int orig_h;

  if (errmsg != NULL && errsz > 0)
    {
      errmsg[0] = '\0';
    }

  fp = fopen (path, "r");
  if (fp == NULL)
    {
      if (errmsg != NULL)
	{
	  snprintf (errmsg, errsz, "cannot open file (%s)", strerror (errno));
	}
      return -1;
    }

  memset (p, 0, sizeof (T_QR_PARSED));
  snprintf (p->db_name, QR_NAME_LEN, "%s", up_db);
  snprintf (p->user_name, QR_NAME_LEN, "%s", up_user);
  orig_raw[0] = rewrite_raw[0] = bind_map[0] = '\0';

  while (fgets (line, sizeof (line), fp) != NULL)
    {
      char *t = trim (line);

      if (t[0] == '#' || t[0] == '\0')
	{
	  continue;
	}

      /* exact key match: "BIND_MAPPING = ..." is a different key, not a BIND_MAP line */
      if (strncasecmp (t, "BIND_MAP", 8) == 0 && (t[8] == '\0' || t[8] == '=' || isspace ((unsigned char) t[8])))
	{
	  char *v = strchr (t, '=');
	  if (v == NULL)
	    {
	      if (errmsg != NULL)
		{
		  snprintf (errmsg, errsz, "BIND_MAP line has no '='");
		}
	      fclose (fp);
	      return -1;
	    }
	  snprintf (bind_map, sizeof (bind_map), "%s", trim (v + 1));
	  has_bind_map = 1;	/* bind_map alone cannot tell "no line" from an empty value */
	}
      else if (strcasecmp (t, "ORIG") == 0)
	{
	  has_orig = 1;
	  saw_end = 0;
	  while (fgets (line, sizeof (line), fp) != NULL)
	    {
	      char *q = trim (line);
	      if (strcasecmp (q, "END") == 0)
		{
		  saw_end = 1;
		  break;
		}
	      if (q[0] == '#' || q[0] == '\0')
		{
		  /* '#' comments and blank lines are dropped inside a block: the collected
		   * text is the query itself (REWRITE is stored verbatim). */
		  continue;
		}
	      /* join block lines with '\n' so an SQL line comment ("--", "//") ends at its own
	       * line; '\n' is whitespace to qr_normalize_query, so the matching key is
	       * unchanged. */
	      orig_len += snprintf (orig_raw + orig_len, sizeof (orig_raw) - orig_len, "%s%s",
				    (orig_len > 0) ? "\n" : "", q);
	      if (orig_len >= (int) sizeof (orig_raw) - 1)
		{
		  if (errmsg != NULL)
		    {
		      snprintf (errmsg, errsz, "ORIG block too long");
		    }
		  fclose (fp);
		  return -1;
		}
	    }
	  if (!saw_end)
	    {
	      /* a block without END means a truncated file: skip the whole rule */
	      if (errmsg != NULL)
		{
		  snprintf (errmsg, errsz, "ORIG block is not terminated by END");
		}
	      fclose (fp);
	      return -1;
	    }
	}
      else if (strcasecmp (t, "REWRITE") == 0)
	{
	  has_rewrite = 1;
	  saw_end = 0;
	  while (fgets (line, sizeof (line), fp) != NULL)
	    {
	      char *q = trim (line);
	      if (strcasecmp (q, "END") == 0)
		{
		  saw_end = 1;
		  break;
		}
	      if (q[0] == '#' || q[0] == '\0')
		{
		  continue;
		}
	      rewrite_len += snprintf (rewrite_raw + rewrite_len, sizeof (rewrite_raw) - rewrite_len, "%s%s",
				       (rewrite_len > 0) ? "\n" : "", q);
	      if (rewrite_len >= (int) sizeof (rewrite_raw) - 1)
		{
		  if (errmsg != NULL)
		    {
		      snprintf (errmsg, errsz, "REWRITE block too long");
		    }
		  fclose (fp);
		  return -1;
		}
	    }
	  if (!saw_end)
	    {
	      /* see the ORIG block above */
	      if (errmsg != NULL)
		{
		  snprintf (errmsg, errsz, "REWRITE block is not terminated by END");
		}
	      fclose (fp);
	      return -1;
	    }
	}
    }
  fclose (fp);

  if (!has_orig || !has_rewrite || p->db_name[0] == '\0' || p->user_name[0] == '\0')
    {
      if (errmsg != NULL)
	{
	  snprintf (errmsg, errsz, "missing %s",
		    !has_orig ? "ORIG block" : (!has_rewrite ? "REWRITE block" : "db/user (directory name)"));
	}
      return -1;
    }

  /* normalize original query (used for matching) */
  norm_len = qr_normalize_query (norm, sizeof (norm), orig_raw, &orig_h, &unterminated);
  if (unterminated)
    {
      if (errmsg != NULL)
	{
	  snprintf (errmsg, errsz, "unterminated comment in ORIG query");
	}
      return -1;
    }
  if (norm_len < 0)
    {
      /* -1 = the normalized text did not fit; the empty case is handled below */
      if (errmsg != NULL)
	{
	  snprintf (errmsg, errsz, "ORIG query too long after normalization");
	}
      return -1;
    }
  if (norm_len == 0)
    {
      if (errmsg != NULL)
	{
	  snprintf (errmsg, errsz, "empty ORIG query after normalization");
	}
      return -1;
    }
  p->orig_norm = strdup (norm);
  p->orig_hash = orig_h;

  if (rewrite_raw[0] == '\0')
    {
      if (errmsg != NULL)
	{
	  snprintf (errmsg, errsz, "empty REWRITE query");
	}
      free_and_init (p->orig_norm);
      return -1;
    }
  p->rewrite_query = strdup (rewrite_raw);

  if (p->orig_norm == NULL || p->rewrite_query == NULL)
    {
      if (errmsg != NULL)
	{
	  snprintf (errmsg, errsz, "out of memory");
	}
      free_and_init (p->orig_norm);
      free_and_init (p->rewrite_query);
      return -1;
    }

  /* record the BIND_MAP entries; the counts are validated at load time by
   * qr_check_rewrite_policy and resolved at CAS prepare time (get_num_markers). */
  p->bind_map_given = has_bind_map;
  if (qr_parse_bind_map (p, has_bind_map ? bind_map : NULL) < 0)
    {
      if (errmsg != NULL)
	{
	  /* the map text can be as long as a whole query line while errmsg is a small
	   * diagnostic buffer, so bound the echo rather than let snprintf truncate it. */
	  snprintf (errmsg, errsz, "invalid BIND_MAP (%.128s)",
		    (bind_map[0] != '\0') ? bind_map : "empty; use MATCH, NONE or an explicit map");
	}
      free_and_init (p->orig_norm);
      free_and_init (p->rewrite_query);
      return -1;
    }

  return 0;
}

#if !defined(WINDOWS)
/*
 * build the per-broker rule error log path <err_log_dir>/<broker>_qr_rule.err
 * into buf.  returns buf, or NULL when br_info_p is NULL.
 */
static const char *
qr_rule_log_path (const T_BROKER_INFO * br_info_p, char *buf, size_t bufsz)
{
  if (br_info_p == NULL)
    {
      return NULL;
    }

  snprintf (buf, bufsz, "%s/%s_qr_rule.err", br_info_p->err_log_dir, br_info_p->name);
  return buf;
}

/*
 * append (or truncate) a diagnostic line to the per-broker rule error log
 * <err_log_dir>/<broker_name>_qr_rule.err.  used by the build (qr_shm_create)
 * and by the runtime admin path (qr_admin_*) to record why a rule was skipped.
 * do_truncate == true opens with "w" (the first write of a build), otherwise appends.
 * the file is created lazily on the first error only, so a clean load leaves no
 * file behind.
 */
void
qr_rule_log_write (const T_BROKER_INFO * br_info_p, bool do_truncate, const char *rulepath, const char *reason)
{
  char path[BROKER_PATH_MAX];
  FILE *fp;
  time_t now;
  struct tm tm;
  char ts[32];

  if (qr_rule_log_path (br_info_p, path, sizeof (path)) == NULL)
    {
      return;
    }

  fp = fopen (path, do_truncate ? "w" : "a");
  if (fp == NULL)
    {
      return;
    }

  now = time (NULL);
  if (localtime_r (&now, &tm) == NULL || strftime (ts, sizeof (ts), "%Y-%m-%d %H:%M:%S", &tm) == 0)
    {
      ts[0] = '\0';
    }

  fprintf (fp, "%s %s: %s\n", ts, (rulepath != NULL) ? rulepath : "unknown rule",
	   (reason != NULL) ? reason : "unknown reason");

  fclose (fp);
}

/*
 * parse a "user@dbname" rule directory name into upper-cased user/db.
 * exactly one '@' is required and both sides must be non-empty and fit in
 * QR_NAME_LEN.  returns 0 on success, -1 when the name is not a valid
 * user@dbname directory (the caller skips it).
 */
static int
qr_parse_dir_name (const char *name, char *up_user, char *up_db)
{
  const char *at = strchr (name, '@');
  char user_raw[QR_NAME_LEN];
  int ulen;

  if (at == NULL || at == name || at[1] == '\0')
    {
      return -1;		/* missing '@', empty user, or empty db */
    }

  if (strchr (at + 1, '@') != NULL)
    {
      return -1;		/* more than one '@' */
    }

  ulen = (int) (at - name);
  if (ulen >= QR_NAME_LEN || (int) strlen (at + 1) >= QR_NAME_LEN)
    {
      return -1;		/* user or db name too long */
    }

  memcpy (user_raw, name, ulen);
  user_raw[ulen] = '\0';

  qr_upper_copy (up_user, user_raw, QR_NAME_LEN);
  qr_upper_copy (up_db, at + 1, QR_NAME_LEN);

  return 0;
}
#endif

/* derive a distinct shm key for the rewrite segment from the appl server
 * shm id, keeping per-broker uniqueness in the low bits. */
int
qr_make_shm_key (int appl_server_shm_id)
{
#if defined(WINDOWS)
  return 0;
#else
  return (int) (((unsigned int) appl_server_shm_id & 0x00FFFFFFu) | 0x51000000u);
#endif
}

/* admin side: print the loaded-rule information for one broker (cubrid broker
 * info -r).  read-only attach to the paired segment; prints OFF when absent. */
void
qr_admin_dump_info (FILE * fp, const T_BROKER_INFO * br_info_p)
{
#if defined(WINDOWS)
  fprintf (fp, "  QUERY_REWRITE = N/A (windows)\n");
#else
  int key, mid, i, j;
  char *base;
  T_QR_SHM_HEADER *hdr;
  T_QR_RULE *rule;
  char *pool;
  char *done;
  int loaded = 0, disabled = 0;

  if (br_info_p->query_rewrite_rule[0] == '\0')
    {
      fprintf (fp, "  query rewrite disabled (QUERY_REWRITE_RULE not set)\n");
      return;
    }

  key = qr_make_shm_key (br_info_p->appl_server_shm_id);
  mid = shmget (key, 0, 0);
  if (mid == -1)
    {
      fprintf (fp, "  QUERY_REWRITE = OFF\n");
      return;
    }

  base = (char *) shmat (mid, (char *) 0, SHM_RDONLY);
  if (base == (char *) -1)
    {
      fprintf (fp, "  QUERY_REWRITE = OFF\n");
      return;
    }

  hdr = (T_QR_SHM_HEADER *) base;
  /* owner_shm_id guards against the key collision qr_make_shm_key's 24-bit mask allows */
  if (hdr->magic != QR_SHM_MAGIC || hdr->owner_shm_id != br_info_p->appl_server_shm_id)
    {
      fprintf (fp, "  QUERY_REWRITE = OFF\n");
      shmdt (base);
      return;
    }

  /* the listing walks rule[] and the pool through the header offsets: same check as the
   * CAS attach, reported apart from OFF because it is not a normal state. */
  if (!qr_shm_header_sane (hdr, mid))
    {
      fprintf (fp, "  QUERY_REWRITE = OFF (segment corrupted, restart the broker)\n");
      shmdt (base);
      return;
    }

  rule = (T_QR_RULE *) (base + hdr->rule_off);
  pool = base + hdr->pool_off;

  for (i = 0; i < hdr->rule_count; i++)
    {
      if (rule[i].disabled)
	{
	  disabled++;
	}
      else
	{
	  loaded++;
	}
    }

  fprintf (fp, "  RULE DIR      : %s\n", br_info_p->query_rewrite_rule);
  fprintf (fp, "  MAX RULES     : %-3d      MAX QUERY LEN : %-d\n", hdr->max_rules, hdr->cfg_max_query_len);
  fprintf (fp, "  LOADED        : %-3d      DISABLED      : %-3d       SLOTS : %d/%d\n",
	   loaded, disabled, hdr->rule_count, hdr->max_rules);

  /* rules that failed to load have no slot at all, so the listing below cannot show them.
   * point at the rule error log instead: qr_shm_create() unlinks it on entry and recreates
   * it lazily, so its mere existence means "this build skipped something". */
  {
    char errlog[BROKER_PATH_MAX];
    FILE *efp;

    if (qr_rule_log_path (br_info_p, errlog, sizeof (errlog)) != NULL && (efp = fopen (errlog, "r")) != NULL)
      {
	char lbuf[1024];
	int entries = 0, warns = 0;
	bool split = false;	/* the previous read stopped mid-line, not at its end */

	/* a rule that loaded with a warning writes here too, so the two are counted apart:
	 * "entries" must keep meaning "rules this build skipped". */
	while (fgets (lbuf, sizeof (lbuf), efp) != NULL)
	  {
	    size_t n = strlen (lbuf);

	    if (!split)
	      {
		if (strstr (lbuf, ": warning: ") != NULL)
		  {
		    warns++;
		  }
		else
		  {
		    entries++;
		  }
	      }
	    split = (n > 0 && lbuf[n - 1] != '\n');
	  }
	fclose (efp);

	if (warns > 0)
	  {
	    fprintf (fp, "  RULE ERRORS   : %s (%d %s, %d warning%s)\n", errlog, entries,
		     (entries == 1) ? "entry" : "entries", warns, (warns == 1) ? "" : "s");
	  }
	else
	  {
	    fprintf (fp, "  RULE ERRORS   : %s (%d %s)\n", errlog, entries, (entries == 1) ? "entry" : "entries");
	  }
      }
  }

  /* a segment holding only disabled rules is still listed: `qr enable` takes a relpath and
   * `qr status` is the documented way to look it up, so gate on the slot count, not on the
   * active count. */
  if (hdr->rule_count == 0)
    {
      shmdt (base);
      return;
    }

  /* group the rules by (db, user) and list each with its state and source */
  done = (char *) calloc (hdr->rule_count > 0 ? hdr->rule_count : 1, 1);
  if (done == NULL)
    {
      shmdt (base);
      return;
    }

  for (i = 0; i < hdr->rule_count; i++)
    {
      if (done[i])
	{
	  continue;
	}
      fprintf (fp, "\n  [%s / %s]\n", rule[i].db_name, rule[i].user_name);
      fprintf (fp, "    %-30s %-9s %s\n", "NAME", "STATE", "SOURCE");
      fprintf (fp, "    %s\n", "--------------------------------------------------");
      for (j = i; j < hdr->rule_count; j++)
	{
	  if (!done[j] && strcmp (rule[j].db_name, rule[i].db_name) == 0
	      && strcmp (rule[j].user_name, rule[i].user_name) == 0)
	    {
	      fprintf (fp, "    %-30s %-9s %s\n", qr_slot_name (pool, hdr, j), rule[j].disabled ? "DISABLED" : "ON",
		       (rule[j].origin == QR_ORIGIN_ADDED) ? "ADDED" : "STARTUP");
	      done[j] = 1;
	    }
	}
    }

  free_and_init (done);
  shmdt (base);
#endif
}

/* statement-type kind used by the build-time rewrite policy.
 * the INSERT "replace" modifier is folded in so INSERT and REPLACE are distinct. */
typedef enum
{
  QR_K_OTHER = 0,		/* DDL / DCL / CALL / method / unsupported */
  QR_K_SELECT,			/* SELECT and set queries (UNION/DIFFERENCE/INTERSECTION) */
  QR_K_INSERT,
  QR_K_REPLACE,
  QR_K_UPDATE,
  QR_K_DELETE,
  QR_K_MERGE
} QR_STMT_KIND;

/*
 * qr_node_kind () - classify a parsed statement into a rewrite-policy kind.
 *   return: QR_STMT_KIND
 *   stmt(in): first parsed statement node, or NULL
 */
static QR_STMT_KIND
qr_node_kind (PT_NODE * stmt)
{
  if (stmt == NULL)
    {
      return QR_K_OTHER;
    }

  switch (stmt->node_type)
    {
    case PT_SELECT:
    case PT_UNION:
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
      /* set queries are read-only: same category as SELECT */
      return QR_K_SELECT;
    case PT_INSERT:
      return stmt->info.insert.do_replace ? QR_K_REPLACE : QR_K_INSERT;
    case PT_UPDATE:
      return QR_K_UPDATE;
    case PT_DELETE:
      return QR_K_DELETE;
    case PT_MERGE:
      return QR_K_MERGE;
    default:
      /* DDL / DCL (GRANT/REVOKE) / CALL / method / everything else */
      return QR_K_OTHER;
    }
}

/* first parser error, appended to the rule error log reason.  "syntax error" alone
 * forces the rule author to bisect the query by hand; the parser already knows where. */
static void
qr_parse_error_detail (PARSER_CONTEXT * parser, char *buf, int bufsz)
{
  PT_NODE *errs;
  int stmt_no, line_no, col_no;
  const char *msg = NULL;

  buf[0] = '\0';
  errs = pt_get_errors (parser);
  if (errs == NULL)
    {
      return;
    }

  (void) pt_get_next_error (errs, &stmt_no, &line_no, &col_no, &msg);
  if (msg != NULL && msg[0] != '\0')
    {
      snprintf (buf, bufsz, ": %s", msg);
    }
}

/* locale for the build-time parser.  fixed on purpose: taking it from the environment
 * would let the same rule file pass on one machine and fail on another. */
#define QR_PARSER_LOCALE "en_US.utf8"

static bool qr_parser_lang_ready = false;

/*
 * qr_check_rewrite_policy () - build-time statement-type security check for one
 *   rule.  parses ORIG and REWRITE (syntax only, no DB connection) and enforces:
 *   single statement, no DDL/DCL/CALL, SELECT (or DML when allowed), ORIG/REWRITE kind
 *   preservation, and marker/BIND_MAP consistency.
 *   return: 0 if the rule passes, -1 otherwise (reason filled)
 *   p(in): the parsed rule; the queries are read from it along with the BIND_MAP form
 *   allow_non_select(in): QUERY_REWRITE_ALLOW_NON_SELECT (0 = SELECT-only)
 *   reason(out), reasonsz(in): rejection reason for the rule error log
 *   warn(out), warnsz(in): set when the rule passes but deserves a note; "" otherwise
 */
static int
qr_check_rewrite_policy (const T_QR_PARSED * p, int allow_non_select, char *reason, int reasonsz, char *warn,
			 int warnsz)
{
  const char *orig = p->orig_norm;
  const char *rewrite = p->rewrite_query;
  PARSER_CONTEXT *parser = NULL;
  PT_NODE **tree;
  PT_NODE *node;
  QR_STMT_KIND rw_kind, or_kind;
  int k_orig = 0, k_rewrite = 0;
  int rc = -1;

  if (warn != NULL)
    {
      warn[0] = '\0';
    }

  /* the SQL parser needs the locale/charset subsystem; no DB connection is required.
   * lang_init alone is not enough: it leaves the charset uninitialized, and the first
   * lang_charset() the parser reaches then asserts (debug) or returns INTL_CODESET_NONE
   * (release). */
  if (!qr_parser_lang_ready)
    {
      (void) lang_init ();
      if (lang_set_charset_lang (QR_PARSER_LOCALE) != NO_ERROR)
	{
	  snprintf (reason, reasonsz, "locale initialization failed (%s)", QR_PARSER_LOCALE);
	  return -1;
	}
      qr_parser_lang_ready = true;
    }

  /* --- REWRITE: the query that actually executes (primary control target) --- */
  parser = parser_create_parser ();
  if (parser == NULL)
    {
      snprintf (reason, reasonsz, "parser initialization failed");
      return -1;
    }
  tree = parser_parse_string (parser, rewrite);
  node = (tree != NULL) ? tree[0] : NULL;
  if (node == NULL || pt_has_error (parser))
    {
      char detail[192];

      qr_parse_error_detail (parser, detail, sizeof (detail));
      snprintf (reason, reasonsz, "REWRITE query has a syntax error%s", detail);
      goto done;
    }
  if (parser->statement_number != 1)
    {
      snprintf (reason, reasonsz, "REWRITE must be a single statement");
      goto done;
    }
  if (pt_is_ddl_statement (node))
    {
      snprintf (reason, reasonsz, "DDL replacement is not allowed");
      goto done;
    }
  rw_kind = qr_node_kind (node);
  if (rw_kind == QR_K_OTHER)
    {
      snprintf (reason, reasonsz, "CALL/method/unsupported replacement is not allowed");
      goto done;
    }
  if (rw_kind != QR_K_SELECT && allow_non_select == 0)
    {
      snprintf (reason, reasonsz, "non-SELECT replacement disallowed (QUERY_REWRITE_ALLOW_NON_SELECT=OFF)");
      goto done;
    }
  /* parser_parse_string alone fills host_var_count with the explicit '?' count: constants
   * stay PT_VALUE and auto-parameterization only happens later, in the prepare path. */
  k_rewrite = parser->host_var_count;
  parser_free_parser (parser);
  parser = NULL;

  /* --- ORIG: matching key only; parse for the kind-preservation comparison --- */
  parser = parser_create_parser ();
  if (parser == NULL)
    {
      snprintf (reason, reasonsz, "parser initialization failed");
      return -1;
    }
  tree = parser_parse_string (parser, orig);
  node = (tree != NULL) ? tree[0] : NULL;
  if (node == NULL || pt_has_error (parser))
    {
      char detail[192];

      qr_parse_error_detail (parser, detail, sizeof (detail));
      snprintf (reason, reasonsz, "ORIG query has a syntax error%s", detail);
      goto done;
    }
  if (parser->statement_number != 1)
    {
      snprintf (reason, reasonsz, "ORIG must be a single statement");
      goto done;
    }
  k_orig = parser->host_var_count;
  or_kind = qr_node_kind (node);

  /* kind preservation: a rewrite must not change the statement kind (this also
   * blocks INSERT<->REPLACE since do_replace is folded into the kind). */
  if (rw_kind != or_kind)
    {
      snprintf (reason, reasonsz, "statement kind mismatch between ORIG and REWRITE");
      goto done;
    }

  /* marker / BIND_MAP consistency, checked at load time so a misconfigured rule never reaches
   * a CAS.  stricter than the CAS-side qr_validate_markers, which sees only the stored entry
   * count and so cannot tell an omitted BIND_MAP from MATCH; that one stays as the second
   * safety net for a rule that somehow reaches prepare. */

  /* qr_validate_markers rejects anything above QR_MAX_BINDS.  without the same bound here
   * the rule loads, and then every prepare of it fails validation in the CAS and falls back
   * to the original query -- a per-request cost with no diagnostic at load time. */
  if (k_orig > QR_MAX_BINDS || k_rewrite > QR_MAX_BINDS)
    {
      snprintf (reason, reasonsz, "marker count exceeds %d (ORIG %d, REWRITE %d)", QR_MAX_BINDS, k_orig, k_rewrite);
      goto done;
    }

  if (!p->bind_map_given)
    {
      /* an omitted line may only mean "this rule has no binds at all".  with markers in ORIG
       * it is indistinguishable from a forgotten map, and either guess (identity or drop)
       * silently misplaces the caller's values; MATCH / NONE say which one was meant. */
      if (k_orig > 0)
	{
	  snprintf (reason, reasonsz, "ORIG has %d marker(s); BIND_MAP is required (MATCH, NONE or an explicit map)",
		    k_orig);
	  goto done;
	}
      if (k_rewrite > 0)
	{
	  snprintf (reason, reasonsz, "REWRITE has %d marker(s) but ORIG has none", k_rewrite);
	  goto done;
	}
    }
  else if (p->num_map_entries < 0)	/* MATCH */
    {
      if (k_rewrite != k_orig)
	{
	  snprintf (reason, reasonsz, "BIND_MAP = MATCH requires equal marker counts (ORIG %d, REWRITE %d)", k_orig,
		    k_rewrite);
	  goto done;
	}
    }
  else if (p->num_map_entries == 0)	/* NONE */
    {
      if (k_rewrite > 0)
	{
	  snprintf (reason, reasonsz, "BIND_MAP = NONE but REWRITE has %d marker(s)", k_rewrite);
	  goto done;
	}
    }
  else
    {
      const short *src_orig_pos = p->src_orig_pos;
      bool used[QR_MAX_BINDS];
      char list[128];		/* leading positions only; n_unused carries the full count */
      int j, n_unused = 0, w = 0;

      if (p->num_map_entries != k_rewrite)
	{
	  snprintf (reason, reasonsz, "BIND_MAP has %d entries but REWRITE has %d marker(s)", p->num_map_entries,
		    k_rewrite);
	  goto done;
	}

      memset (used, 0, sizeof (used));
      for (j = 0; j < k_rewrite; j++)
	{
	  if (src_orig_pos[j] < 1 || src_orig_pos[j] > k_orig)
	    {
	      snprintf (reason, reasonsz,
			"BIND_MAP entry for REWRITE marker %d maps to ORIG position %d (valid: 1..%d)", j + 1,
			(int) src_orig_pos[j], k_orig);
	      goto done;
	    }
	  used[src_orig_pos[j] - 1] = true;
	}

      /* an ORIG position the map never names is a value the driver keeps sending and the
       * replacement never uses.  the author may well have meant it, so warn instead of
       * rejecting -- but it is also exactly what a mistyped map looks like. */
      list[0] = '\0';
      for (j = 0; j < k_orig; j++)
	{
	  if (used[j])
	    {
	      continue;
	    }
	  n_unused++;
	  if (w < (int) sizeof (list) - 1)
	    {
	      /* a truncating snprintf still leaves list terminated; w only guards this loop */
	      w += snprintf (list + w, sizeof (list) - w, "%s%d", (w == 0) ? "" : ",", j + 1);
	    }
	}
      if (n_unused > 0 && warn != NULL)
	{
	  /* the "warning: " tag is part of the text: it lands in the per-broker rule log, which
	   * is otherwise a list of skipped rules, and qr status counts the two apart. */
	  snprintf (warn, warnsz,
		    "warning: BIND_MAP does not use %d ORIG marker(s) (%.100s); those bind values are ignored",
		    n_unused, list);
	}
    }

  rc = 0;

done:
  if (parser != NULL)
    {
      parser_free_parser (parser);
    }
  return rc;
}

int
qr_shm_create (T_BROKER_INFO * br_info_p, T_SHM_APPL_SERVER * shm_as_p)
{
/* record one skipped rule in the per-broker rule error log.  the log is unlink()ed at entry
 * below, but the first write of this build still opens with "w" as a belt for a failed
 * unlink (permissions); every later one appends.  br_info_p and need_truncate are captured
 * from the enclosing function.  #undef'd at the end of the function. */
#define QR_RULE_LOG_WRITE(relpath, reason) \
        do { \
          qr_rule_log_write (br_info_p, need_truncate, (relpath), (reason)); \
          need_truncate = false; \
        } while (0)

#if defined(WINDOWS)
  /* not supported on windows yet */
  shm_as_p->query_rewrite_shm_key = 0;
  return 0;
#else
  const char *dir = br_info_p->query_rewrite_rule;
  DIR *dp;
  struct dirent *ent;
  T_QR_PARSED *parsed = NULL;
  int parsed_count = 0, parsed_cap = 0;
  int i, hash_size, pool_size, total_size, pool_slot;
  int bucket_off, rule_off, pool_off;
  int dbuser_count = 0, dbuser_hash_size;
  int dbuser_bucket_off, dbuser_off;
  int max_rules, cfg_qlen;
  T_QR_DBUSER *du = NULL;	/* distinct (db,user) seeded from directory names */
  int shm_key, mid;
  char *base;
  T_QR_SHM_HEADER *hdr;
  int *bucket;
  T_QR_RULE *rule;
  int *dbuser_bucket;
  T_QR_DBUSER *dbuser;
  char *pool;
  char fullpath[BROKER_PATH_MAX];
  bool need_truncate = true;	/* the next rule error log write must truncate, not append */

  shm_as_p->query_rewrite_shm_key = 0;

  /* clear the previous build's rule error log so a clean load leaves no file
   * behind; it is recreated lazily below only if this build hits an error. */
  {
    char errlog[BROKER_PATH_MAX];

    if (qr_rule_log_path (br_info_p, errlog, sizeof (errlog)) != NULL)
      {
	unlink (errlog);
      }
  }

  /* drop a stale segment from a previous (possibly crashed) run before any early return.
   * so the key-based qr status / qr disable never find an orphan (they locate the segment
   * by key, not by shm_as_p->query_rewrite_shm_key).  qr_shm_destroy validates
   * magic/owner_shm_id, so only this broker's segment is removed. */
  qr_shm_destroy (br_info_p->appl_server_shm_id, QR_SHMODE);

  if (dir == NULL || dir[0] == '\0')
    {
      return 0;
    }

  max_rules = br_info_p->query_rewrite_max_rules;
  cfg_qlen = br_info_p->query_rewrite_max_query_len;

  dp = opendir (dir);
  if (dp == NULL)
    {
      char why[BROKER_PATH_MAX + 64];
      snprintf (why, sizeof (why), "cannot open rule directory '%s' (%s), feature disabled", dir, strerror (errno));
      QR_RULE_LOG_WRITE ("(directory)", why);
      return 0;
    }

  /* distinct (db,user) presence list, seeded from every valid user@dbname
   * sub-directory (even empty ones) so that runtime `qr add` can target them. */
  du = (T_QR_DBUSER *) calloc (max_rules, sizeof (T_QR_DBUSER));
  if (du == NULL)
    {
      closedir (dp);
      return -1;
    }

  /* pass 1 : walk each "user@dbname" sub-directory and parse its rule files.
   * the db/user are taken from the sub-directory name, not from the files. */
  while ((ent = readdir (dp)) != NULL)
    {
      char up_db[QR_NAME_LEN];
      char up_user[QR_NAME_LEN];
      char subdir[BROKER_PATH_MAX];
      int pathlen;
      struct stat st;
      DIR *sub_dp;
      struct dirent *sub_ent;

      if (ent->d_name[0] == '.')
	{
	  continue;
	}
      /* a path that does not fit BROKER_PATH_MAX could not be opened either, so drop the
       * entry here instead of walking on with a silently truncated path. */
      pathlen = snprintf (subdir, sizeof (subdir), "%s/%s", dir, ent->d_name);
      if (pathlen < 0 || pathlen >= (int) sizeof (subdir))
	{
	  continue;
	}
      if (stat (subdir, &st) != 0 || !S_ISDIR (st.st_mode) || strchr (ent->d_name, ' ') != NULL)
	{
	  continue;		/* ignore non-directory entries */
	}
      if (qr_parse_dir_name (ent->d_name, up_user, up_db) < 0)
	{
	  /* not a valid "user@dbname" directory, skip it */
	  continue;
	}

      /* seed this (db,user) into the presence list (even with no rule files) */
      {
	int k, found = -1;
	for (k = 0; k < dbuser_count; k++)
	  {
	    if (strcmp (du[k].db_name, up_db) == 0 && strcmp (du[k].user_name, up_user) == 0)
	      {
		found = k;
		break;
	      }
	  }
	if (found < 0)
	  {
	    if (dbuser_count >= max_rules)
	      {
		char why[64];
		snprintf (why, sizeof (why), "distinct (db,user) exceeds %d, directory skipped", max_rules);
		QR_RULE_LOG_WRITE (ent->d_name, why);
		continue;
	      }
	    snprintf (du[dbuser_count].db_name, QR_NAME_LEN, "%s", up_db);
	    snprintf (du[dbuser_count].user_name, QR_NAME_LEN, "%s", up_user);
	    dbuser_count++;
	  }
      }

      sub_dp = opendir (subdir);
      if (sub_dp == NULL)
	{
	  continue;
	}
      while ((sub_ent = readdir (sub_dp)) != NULL)
	{
	  T_QR_PARSED tmp;
	  char rulepath[QR_RELPATH_LEN];
	  char errbuf[256];
	  char warnbuf[256];

	  if (sub_ent->d_name[0] == '.')
	    {
	      continue;
	    }

	  /* only files ending in ".rule" are loaded */
	  if (!qr_has_rule_suffix (sub_ent->d_name))
	    {
	      continue;
	    }

	  pathlen = snprintf (fullpath, sizeof (fullpath), "%s/%s/%s", dir, ent->d_name, sub_ent->d_name);
	  if (pathlen < 0 || pathlen >= (int) sizeof (fullpath))
	    {
	      continue;		/* same as above: it would not be openable anyway */
	    }
	  if (stat (fullpath, &st) != 0 || !S_ISREG (st.st_mode) || strchr (sub_ent->d_name, ' ') != NULL)
	    {
	      continue;
	    }

	  /* source file relative path (literal dir name + file), used both for the
	   * loaded-rule listing and for the rule error log. */
	  snprintf (rulepath, sizeof (rulepath), "%s/%s", ent->d_name, sub_ent->d_name);

	  if (qr_parse_file (fullpath, up_db, up_user, &tmp, errbuf, sizeof (errbuf)) < 0)
	    {
	      /* skip a malformed file, keep building the rest */
	      QR_RULE_LOG_WRITE (rulepath, errbuf[0] != '\0' ? errbuf : "parse error");
	      continue;
	    }

	  snprintf (tmp.rulepath, sizeof (tmp.rulepath), "%s", rulepath);
	  /* enforce the per-query length cap (slot size is sized for cfg_qlen) */
	  if ((int) strlen (tmp.orig_norm) > cfg_qlen || (int) strlen (tmp.rewrite_query) > cfg_qlen)
	    {
	      snprintf (errbuf, sizeof (errbuf), "query exceeds %d bytes", cfg_qlen);
	      QR_RULE_LOG_WRITE (rulepath, errbuf);
	      free_and_init (tmp.orig_norm);
	      free_and_init (tmp.rewrite_query);
	      continue;
	    }
	  /* build-time statement-type policy: parse ORIG/REWRITE and
	   * reject DDL/CALL/non-SELECT/kind-mismatch/multi-statement replacements. */
	  if (qr_check_rewrite_policy (&tmp, br_info_p->query_rewrite_allow_non_select, errbuf, sizeof (errbuf),
				       warnbuf, sizeof (warnbuf)) < 0)
	    {
	      QR_RULE_LOG_WRITE (rulepath, errbuf);
	      free_and_init (tmp.orig_norm);
	      free_and_init (tmp.rewrite_query);
	      continue;
	    }
	  if (warnbuf[0] != '\0')
	    {
	      /* the rule did load; the rule log is the only per-broker channel at startup */
	      QR_RULE_LOG_WRITE (rulepath, warnbuf);
	    }
	  /* skip a duplicate (db, user, normalized orig): keep the first one so that
	   * lookup stays deterministic regardless of directory read order. */
	  {
	    int d, dup = 0;
	    for (d = 0; d < parsed_count; d++)
	      {
		if (strcmp (parsed[d].db_name, tmp.db_name) == 0
		    && strcmp (parsed[d].user_name, tmp.user_name) == 0
		    && strcmp (parsed[d].orig_norm, tmp.orig_norm) == 0)
		  {
		    dup = 1;
		    break;
		  }
	      }
	    if (dup)
	      {
		QR_RULE_LOG_WRITE (rulepath, "duplicate ORIG (already loaded for this db/user)");
		free_and_init (tmp.orig_norm);
		free_and_init (tmp.rewrite_query);
		continue;
	      }
	  }
	  if (parsed_count >= max_rules)
	    {
	      snprintf (errbuf, sizeof (errbuf), "rule count exceeds %d", max_rules);
	      QR_RULE_LOG_WRITE (rulepath, errbuf);
	      free_and_init (tmp.orig_norm);
	      free_and_init (tmp.rewrite_query);
	      continue;
	    }
	  if (parsed_count >= parsed_cap)
	    {
	      int new_cap = (parsed_cap == 0) ? 64 : parsed_cap * 2;
	      T_QR_PARSED *np;
	      if (new_cap > max_rules)
		{
		  new_cap = max_rules;
		}
	      np = (T_QR_PARSED *) realloc (parsed, sizeof (T_QR_PARSED) * new_cap);
	      if (np == NULL)
		{
		  free_and_init (tmp.orig_norm);
		  free_and_init (tmp.rewrite_query);
		  break;
		}
	      parsed = np;
	      parsed_cap = new_cap;
	    }
	  parsed[parsed_count++] = tmp;
	}
      closedir (sub_dp);
    }
  closedir (dp);

  if (dbuser_count == 0)
    {
      /* no valid user@dbname directory: nothing to serve / target, feature off */
      for (i = 0; i < parsed_count; i++)
	{
	  free_and_init (parsed[i].orig_norm);
	  free_and_init (parsed[i].rewrite_query);
	}
      free_and_init (parsed);
      free_and_init (du);
      return 0;
    }

  /* fixed reservation: rule[] and dbuser[] to max_rules, slot-based string pool */
  pool_slot = 2 * (cfg_qlen + 1) + QR_RELPATH_LEN;
  pool_size = max_rules * pool_slot;
  hash_size = QR_DEFAULT_HASH_SIZE;
  dbuser_hash_size = QR_DEFAULT_HASH_SIZE;
  bucket_off = (int) sizeof (T_QR_SHM_HEADER);
  rule_off = bucket_off + (int) sizeof (int) * hash_size;
  dbuser_bucket_off = rule_off + (int) sizeof (T_QR_RULE) * max_rules;
  dbuser_off = dbuser_bucket_off + (int) sizeof (int) * dbuser_hash_size;
  pool_off = dbuser_off + (int) sizeof (T_QR_DBUSER) * max_rules;
  total_size = pool_off + pool_size;

  shm_key = qr_make_shm_key (br_info_p->appl_server_shm_id);


  mid = shmget (shm_key, total_size, IPC_CREAT | IPC_EXCL | QR_SHMODE);
  if (mid == -1)
    {
      /* the caller ignores the return value, so without this the feature would go off with
       * no diagnostic.  the expected cause is a live segment on the same key: qr_make_shm_key
       * keeps only 24 bits of appl_server_shm_id, and the stale-segment removal at entry
       * drops only segments this broker owns. */
      char why[256];

      snprintf (why, sizeof (why), "cannot create rewrite segment for key 0x%x (%s); "
		"check APPL_SERVER_SHM_ID collisions", shm_key, strerror (errno));
      QR_RULE_LOG_WRITE ("(segment)", why);

      for (i = 0; i < parsed_count; i++)
	{
	  free_and_init (parsed[i].orig_norm);
	  free_and_init (parsed[i].rewrite_query);
	}
      free_and_init (parsed);
      free_and_init (du);
      return -1;
    }
  base = (char *) shmat (mid, (char *) 0, 0);
  if (base == (char *) -1)
    {
      shmctl (mid, IPC_RMID, NULL);
      for (i = 0; i < parsed_count; i++)
	{
	  free_and_init (parsed[i].orig_norm);
	  free_and_init (parsed[i].rewrite_query);
	}
      free_and_init (parsed);
      free_and_init (du);
      return -1;
    }

  memset (base, 0, total_size);
  hdr = (T_QR_SHM_HEADER *) base;
  bucket = (int *) (base + bucket_off);
  rule = (T_QR_RULE *) (base + rule_off);
  dbuser_bucket = (int *) (base + dbuser_bucket_off);
  dbuser = (T_QR_DBUSER *) (base + dbuser_off);
  pool = base + pool_off;

  hdr->magic = QR_SHM_MAGIC;
  hdr->owner_shm_id = br_info_p->appl_server_shm_id;
  hdr->generation = shm_as_p->query_rewrite_generation + 1;
  hdr->rule_count = parsed_count;
  hdr->hash_size = hash_size;
  hdr->max_rules = max_rules;
  hdr->cfg_max_query_len = cfg_qlen;
  hdr->pool_slot = pool_slot;
  hdr->bucket_off = bucket_off;
  hdr->rule_off = rule_off;
  hdr->pool_off = pool_off;
  hdr->total_size = total_size;
  hdr->min_query_len = 0x7fffffff;
  hdr->max_query_len = 0;
  hdr->dbuser_count = dbuser_count;
  hdr->dbuser_hash_size = dbuser_hash_size;
  hdr->dbuser_bucket_off = dbuser_bucket_off;
  hdr->dbuser_off = dbuser_off;

  for (i = 0; i < hash_size; i++)
    {
      bucket[i] = -1;
    }

  for (i = 0; i < dbuser_hash_size; i++)
    {
      dbuser_bucket[i] = -1;
    }

  /* seed the (db, user) presence index from the directory list */
  for (i = 0; i < dbuser_count; i++)
    {
      unsigned int dh;
      int b;

      memcpy (dbuser[i].db_name, du[i].db_name, QR_NAME_LEN);
      memcpy (dbuser[i].user_name, du[i].user_name, QR_NAME_LEN);
      dh = qr_hash_str (du[i].db_name) ^ qr_hash_str (du[i].user_name);
      b = (int) (dh % (unsigned int) dbuser_hash_size);
      dbuser[i].next_idx = dbuser_bucket[b];
      dbuser_bucket[b] = i;
    }

  /* serialize rules into fixed pool slots and build the hash chains */
  for (i = 0; i < parsed_count; i++)
    {
      T_QR_PARSED *src = &parsed[i];
      int orig_len = (int) strlen (src->orig_norm);
      int orig_off, rewrite_off, name_off;
      unsigned int h;
      int b;

      memcpy (rule[i].db_name, src->db_name, QR_NAME_LEN);
      memcpy (rule[i].user_name, src->user_name, QR_NAME_LEN);
      rule[i].num_map_entries = src->num_map_entries;
      rule[i].disabled = 0;
      rule[i].origin = QR_ORIGIN_STARTUP;
      rule[i].admin_seq = 0;
      memcpy (rule[i].src_orig_pos, src->src_orig_pos, sizeof (rule[i].src_orig_pos));

      qr_slot_offsets (hdr, i, &orig_off, &rewrite_off, &name_off);
      rule[i].orig_off = orig_off;
      rule[i].rewrite_off = rewrite_off;
      rule[i].name_off = name_off;
      memcpy (pool + orig_off, src->orig_norm, orig_len + 1);
      memcpy (pool + rewrite_off, src->rewrite_query, strlen (src->rewrite_query) + 1);
      memcpy (pool + name_off, src->rulepath, strlen (src->rulepath) + 1);

      if (orig_len < hdr->min_query_len)
	{
	  hdr->min_query_len = orig_len;
	}
      if (orig_len > hdr->max_query_len)
	{
	  hdr->max_query_len = orig_len;
	}

      h = src->orig_hash ^ qr_hash_str (src->db_name) ^ qr_hash_str (src->user_name);
      rule[i].hash = h;
      rule[i].orig_len = orig_len;
      b = (int) (h % (unsigned int) hash_size);
      rule[i].next_idx = bucket[b];
      bucket[b] = i;

      free_and_init (src->orig_norm);
      free_and_init (src->rewrite_query);
    }
  free_and_init (parsed);
  free_and_init (du);

  shmdt (base);

  shm_as_p->query_rewrite_shm_key = shm_key;
  shm_as_p->query_rewrite_generation++;

  return shm_key;
#endif
#undef QR_RULE_LOG_WRITE
}

void
qr_shm_destroy (int shm_key, int mode)
{
#if defined(WINDOWS)
  /* not supported on windows yet: no segment is ever created (qr_shm_create) */
  return;
#else
  int qr_mid;
  char *base;
  T_QR_SHM_HEADER *hdr;

  qr_mid = shmget (qr_make_shm_key (shm_key), 0, mode);
  if (qr_mid == -1)
    {
      return;
    }

  base = (char *) shmat (qr_mid, (char *) 0, SHM_RDONLY);
  if (base == (char *) -1)
    {
      return;
    }

  hdr = (T_QR_SHM_HEADER *) base;
  if (hdr->magic == QR_SHM_MAGIC && hdr->owner_shm_id == shm_key)
    {
      shmctl (qr_mid, IPC_RMID, 0);
    }
  shmdt (base);
#endif
}

/* ================================================================== *
 *  ADMIN SIDE : runtime mutation (cubrid broker qr add/reload/...)   *
 * ================================================================== */

#if !defined(WINDOWS)
/* acquire the per-rule-directory write lock (<dir>/.qr.lock).  the lock file is
 * created once and reused; flock is released by the kernel if the holder dies.
 * returns the fd (>= 0) or -1 (msg filled). */
static int
qr_admin_lock (const char *dir, char *msg, int msgsz)
{
  char path[BROKER_PATH_MAX];
  int fd;

  snprintf (path, sizeof (path), "%s/.qr.lock", dir);
  fd = open (path, O_CREAT | O_RDWR, 0644);
  if (fd < 0)
    {
      snprintf (msg, msgsz, "cannot open lock file '%s' (%s)", path, strerror (errno));
      return -1;
    }

  if (flock (fd, LOCK_EX | LOCK_NB) != 0)
    {
      snprintf (msg, msgsz, "another query rewrite command is in progress for this broker");
      close (fd);
      return -1;
    }

  return fd;
}

static void
qr_admin_unlock (int fd)
{
  if (fd >= 0)
    {
      flock (fd, LOCK_UN);
      close (fd);
    }
}

/*
 * attach the rewrite segment RW for mutation.  returns base or NULL (msg filled).
 * a missing segment / bad magic (broker OFF or being rebuilt) fails cleanly.
 */
static char *
qr_admin_attach (const T_BROKER_INFO * br, char *msg, int msgsz)
{
  int key, mid;
  char *base;

  key = qr_make_shm_key (br->appl_server_shm_id);
  mid = shmget (key, 0, 0);
  if (mid == -1)
    {
      snprintf (msg, msgsz, "query rewrite is not enabled for this broker");
      return NULL;
    }

  base = (char *) shmat (mid, (char *) 0, 0);
  if (base == (char *) -1)
    {
      snprintf (msg, msgsz, "cannot attach rewrite segment (%s)", strerror (errno));
      return NULL;
    }

  /* the QR key keeps only the low 24 bits of appl_server_shm_id, so two brokers can map
   * onto one key; owner_shm_id tells whose segment this really is. */
  if (((T_QR_SHM_HEADER *) base)->magic != QR_SHM_MAGIC
      || ((T_QR_SHM_HEADER *) base)->owner_shm_id != br->appl_server_shm_id)
    {
      snprintf (msg, msgsz, "rewrite segment is unavailable (broker stopping/rebuilding), retry");
      shmdt (base);
      return NULL;
    }

  /* the admin paths derive every pointer and loop bound from the header exactly like the
   * CAS side does, and they do it on a read-write mapping.  distinct from the transient
   * failure above: a corrupt segment does not heal by retrying. */
  if (!qr_shm_header_sane ((T_QR_SHM_HEADER *) base, mid))
    {
      snprintf (msg, msgsz, "rewrite segment is corrupted; restart the broker to rebuild it");
      shmdt (base);
      return NULL;
    }

  return base;
}

/* acquire the rule-dir lock and attach the segment RW for a runtime mutation.
 * returns base with *lock_fd set (>= 0) on success; returns NULL (msg filled,
 * lock already released) on failure.  paired with qr_admin_end(). */
static char *
qr_admin_begin (const T_BROKER_INFO * br, char *msg, int msgsz, int *lock_fd)
{
  char *base;

  *lock_fd = qr_admin_lock (br->query_rewrite_rule, msg, msgsz);
  if (*lock_fd < 0)
    {
      return NULL;
    }

  base = qr_admin_attach (br, msg, msgsz);
  if (base == NULL)
    {
      qr_admin_unlock (*lock_fd);
      *lock_fd = -1;
      return NULL;
    }

  return base;
}

/* release what qr_admin_begin() acquired: detach the segment and drop the lock. */
static void
qr_admin_end (char *base, int lock_fd)
{
  if (base != NULL)
    {
      shmdt (base);
    }
  qr_admin_unlock (lock_fd);
}

/* is (db,user) in the startup-seeded presence index?  base is RW-mapped. */
static int
qr_admin_dbuser_present (char *base, const char *up_db, const char *up_user)
{
  T_QR_SHM_HEADER *hdr = (T_QR_SHM_HEADER *) base;
  int *dbuser_bucket = (int *) (base + hdr->dbuser_bucket_off);
  T_QR_DBUSER *dbuser = (T_QR_DBUSER *) (base + hdr->dbuser_off);
  int b, hops;

  if (hdr->dbuser_hash_size == 0)
    {
      return 0;
    }

  b = (int) ((qr_hash_str (up_db) ^ qr_hash_str (up_user)) % (unsigned int) hdr->dbuser_hash_size);
  /* bounded like the CAS-side walks: this one runs under the admin lock, so a corrupt
   * next_idx cycle would hang every later `qr` command, not just this one. */
  for (b = dbuser_bucket[b], hops = 0; b != -1 && hops <= hdr->dbuser_count; b = dbuser[b].next_idx, hops++)
    {
      if (b < 0 || b >= hdr->max_rules)
	{
	  break;
	}
      if (strncmp (dbuser[b].db_name, up_db, QR_NAME_LEN) == 0
	  && strncmp (dbuser[b].user_name, up_user, QR_NAME_LEN) == 0)
	{
	  return 1;
	}
    }

  return 0;
}

/* index of the active rule with the given rulepath, or -1. */
static int
qr_admin_find_active (char *base, const char *rulepath)
{
  T_QR_SHM_HEADER *hdr = (T_QR_SHM_HEADER *) base;
  T_QR_RULE *rule = (T_QR_RULE *) (base + hdr->rule_off);
  char *pool = base + hdr->pool_off;
  int i;

  for (i = 0; i < hdr->rule_count; i++)
    {
      if (!rule[i].disabled && strcmp (qr_slot_name (pool, hdr, i), rulepath) == 0)
	{
	  return i;
	}
    }

  return -1;
}

/*
 * index of the active rule with the same (db, user, normalized ORIG) as *p, ignoring every
 * entry whose rulepath is skip_path (NULL = ignore nothing), or -1.
 * qr_shm_create rejects such duplicates so a bucket never holds two rules with the same
 * key and lookup stays deterministic; qr add / qr reload must enforce the same invariant.
 * reload passes its own rulepath because the entries it is about to replace legitimately
 * carry the same ORIG.
 */
static int
qr_admin_find_active_orig (char *base, const T_QR_PARSED * p, const char *skip_path)
{
  T_QR_SHM_HEADER *hdr = (T_QR_SHM_HEADER *) base;
  T_QR_RULE *rule = (T_QR_RULE *) (base + hdr->rule_off);
  char *pool = base + hdr->pool_off;
  int i;

  for (i = 0; i < hdr->rule_count; i++)
    {
      if (rule[i].disabled)
	{
	  continue;
	}
      if (skip_path != NULL && strcmp (qr_slot_name (pool, hdr, i), skip_path) == 0)
	{
	  continue;
	}
      if (strncmp (rule[i].db_name, p->db_name, QR_NAME_LEN) == 0
	  && strncmp (rule[i].user_name, p->user_name, QR_NAME_LEN) == 0
	  && strcmp (qr_slot_orig (pool, hdr, i), p->orig_norm) == 0)
	{
	  return i;
	}
    }

  return -1;
}

/* rulepath of a rule slot, for diagnostics under the admin lock. */
static const char *
qr_admin_rule_name (char *base, int idx)
{
  T_QR_SHM_HEADER *hdr = (T_QR_SHM_HEADER *) base;

  return qr_slot_name (base + hdr->pool_off, hdr, idx);
}

/*
 * append a parsed rule into a fresh slot and publish (single writer, under flock).
 * returns the new index or -1 (msg filled).  origin = QR_ORIGIN_ADDED.
 */
static int
qr_admin_append (char *base, T_QR_PARSED * src, char *msg, int msgsz)
{
  T_QR_SHM_HEADER *hdr = (T_QR_SHM_HEADER *) base;
  int *bucket = (int *) (base + hdr->bucket_off);
  T_QR_RULE *rule = (T_QR_RULE *) (base + hdr->rule_off);
  char *pool = base + hdr->pool_off;
  int idx = hdr->rule_count;
  int orig_off, rewrite_off, name_off, orig_len;
  unsigned int h;
  int b;

  if (idx >= hdr->max_rules)
    {
      snprintf (msg, msgsz, "rule capacity full (%d); to add a rule, restart the broker with changing the settings",
		hdr->max_rules);
      return -1;
    }

  if ((int) strlen (src->orig_norm) > hdr->cfg_max_query_len
      || (int) strlen (src->rewrite_query) > hdr->cfg_max_query_len)
    {
      snprintf (msg, msgsz, "query exceeds MAX QUERY LEN (%d)", hdr->cfg_max_query_len);
      return -1;
    }

  orig_len = (int) strlen (src->orig_norm);
  qr_slot_offsets (hdr, idx, &orig_off, &rewrite_off, &name_off);

  /* write the slot content completely before publishing the bucket head */
  memcpy (rule[idx].db_name, src->db_name, QR_NAME_LEN);
  memcpy (rule[idx].user_name, src->user_name, QR_NAME_LEN);
  rule[idx].num_map_entries = src->num_map_entries;
  rule[idx].disabled = 0;
  rule[idx].origin = QR_ORIGIN_ADDED;
  rule[idx].admin_seq = 0;
  memcpy (rule[idx].src_orig_pos, src->src_orig_pos, sizeof (rule[idx].src_orig_pos));
  rule[idx].orig_off = orig_off;
  rule[idx].rewrite_off = rewrite_off;
  rule[idx].name_off = name_off;
  memcpy (pool + orig_off, src->orig_norm, orig_len + 1);
  memcpy (pool + rewrite_off, src->rewrite_query, strlen (src->rewrite_query) + 1);
  memcpy (pool + name_off, src->rulepath, strlen (src->rulepath) + 1);

  h = src->orig_hash ^ qr_hash_str (src->db_name) ^ qr_hash_str (src->user_name);
  rule[idx].hash = h;
  rule[idx].orig_len = orig_len;
  b = (int) (h % (unsigned int) hdr->hash_size);
  rule[idx].next_idx = bucket[b];

  /* widen the length pre-filter window before publishing so a reader that sees
   * the new bucket head also sees an admitting min/max. */
  if (orig_len < hdr->min_query_len)
    {
      hdr->min_query_len = orig_len;
    }
  if (orig_len > hdr->max_query_len)
    {
      hdr->max_query_len = orig_len;
    }

  /* publish to the admin scans first, to the readers second.  the reverse order lets a
   * crash in between leave a slot qr_lookup already matches while rule_count still points
   * at it: the next `qr add` would overwrite a live rule (self-linking the chain when the
   * hash lands on the same bucket), and a prepared handle holding that index would remap
   * its binds through another rule's BIND_MAP.  this way an interrupted add leaves an
   * unreachable slot instead, which `qr status` shows and `qr disable` / `qr reload` clean
   * up.  raising rule_count early is safe: no reader indexes rule[] by it, qr_lookup only
   * walks the buckets and uses it as the hop cap. */
  QR_BARRIER ();
  hdr->rule_count = idx + 1;

  QR_BARRIER ();
  bucket[b] = idx;		/* single visibility commit */

  return idx;
}

/* parse <dir>/rulepath into *p, deriving (db,user) from the rulepath directory
 * component.  returns 0 / -1 (msg filled).
 */
static int
qr_admin_parse_rulepath (const T_BROKER_INFO * br, const char *rulepath, T_QR_PARSED * p, char *up_db,
			 char *up_user, char *msg, int msgsz)
{
  char dirname[QR_NAME_LEN * 2 + 8];
  const char *slash = strchr (rulepath, '/');
  char fullpath[BROKER_PATH_MAX];
  int dlen;

  if (slash == NULL || slash == rulepath || slash[1] == '\0' || strchr (slash + 1, '/') != NULL)
    {
      snprintf (msg, msgsz, "invalid rule name, expected \"user@dbname/file\"");
      return -1;
    }

  /* the file part must carry the ".rule" suffix, consistent with the startup
   * scan (files with any other suffix are not treated as rules). */
  if (!qr_has_rule_suffix (slash + 1))
    {
      snprintf (msg, msgsz, "rule file must end with \"%s\"", QR_RULE_SUFFIX);
      return -1;
    }

  dlen = (int) (slash - rulepath);
  if (dlen >= (int) sizeof (dirname))
    {
      snprintf (msg, msgsz, "rule name too long");
      return -1;
    }

  memcpy (dirname, rulepath, dlen);
  dirname[dlen] = '\0';
  if (qr_parse_dir_name (dirname, up_user, up_db) < 0)
    {
      snprintf (msg, msgsz, "invalid \"user@dbname\" directory in rule name");
      return -1;
    }

  if (snprintf (fullpath, sizeof (fullpath), "%s/%s", br->query_rewrite_rule, rulepath) >= (int) sizeof (fullpath))
    {
      snprintf (msg, msgsz, "rule path too long");
      return -1;
    }
  {
    char errbuf[256];
    if (qr_parse_file (fullpath, up_db, up_user, p, errbuf, sizeof (errbuf)) < 0)
      {
	snprintf (msg, msgsz, "cannot parse rule file '%s': %s", fullpath, errbuf[0] != '\0' ? errbuf : "parse error");
	return -1;
      }
  }
  snprintf (p->rulepath, sizeof (p->rulepath), "%s", rulepath);

  return 0;
}
#endif /* !WINDOWS */

int
qr_admin_add (const T_BROKER_INFO * br_info_p, const char *rulepath, char *msg, int msgsz)
{
#if defined(WINDOWS)
  snprintf (msg, msgsz, "query rewrite runtime commands are not supported on windows");
  return -1;
#else
  T_QR_PARSED p;
  char up_db[QR_NAME_LEN], up_user[QR_NAME_LEN];
  char warn[256];
  char *base;
  int lock_fd, rc = -1, dupidx;

  msg[0] = '\0';
  if (qr_admin_parse_rulepath (br_info_p, rulepath, &p, up_db, up_user, msg, msgsz) < 0)
    {
      return -1;
    }

  /* build-time statement-type policy: reject DDL/CALL/
   * non-SELECT/kind-mismatch/multi-statement replacements before touching shm. */
  if (qr_check_rewrite_policy (&p, br_info_p->query_rewrite_allow_non_select, msg, msgsz, warn, sizeof (warn)) < 0)
    {
      qr_rule_log_write (br_info_p, false, rulepath, msg);
      free_and_init (p.orig_norm);
      free_and_init (p.rewrite_query);
      return -1;
    }

  base = qr_admin_begin (br_info_p, msg, msgsz, &lock_fd);
  if (base == NULL)
    {
      free_and_init (p.orig_norm);
      free_and_init (p.rewrite_query);
      return -1;
    }

  if (!qr_admin_dbuser_present (base, up_db, up_user))
    {
      snprintf (msg, msgsz, "(db,user) '%s@%s' not present at startup; create the directory and restart the broker",
		up_user, up_db);
    }
  else if (qr_admin_find_active (base, rulepath) >= 0)
    {
      snprintf (msg, msgsz, "rule '%s' is already loaded; use 'qr reload'", rulepath);
    }
  else if ((dupidx = qr_admin_find_active_orig (base, &p, NULL)) >= 0)
    {
      snprintf (msg, msgsz, "same ORIG is already rewritten by active rule '%s'", qr_admin_rule_name (base, dupidx));
    }
  else if (qr_admin_append (base, &p, msg, msgsz) >= 0)
    {
      ((T_QR_SHM_HEADER *) base)->generation++;
      /* the rule is in; msg is free to carry the policy warning back to the caller */
      snprintf (msg, msgsz, "%s", warn);
      rc = 0;
    }

  qr_admin_end (base, lock_fd);
  free_and_init (p.orig_norm);
  free_and_init (p.rewrite_query);

  return rc;
#endif
}

int
qr_admin_reload (const T_BROKER_INFO * br_info_p, const char *rulepath, char *msg, int msgsz)
{
#if defined(WINDOWS)
  snprintf (msg, msgsz, "query rewrite runtime commands are not supported on windows");
  return -1;
#else
  T_QR_PARSED p;
  char up_db[QR_NAME_LEN], up_user[QR_NAME_LEN];
  char warn[256];
  char *base;
  int lock_fd, rc = -1, newidx;

  msg[0] = '\0';
  if (qr_admin_parse_rulepath (br_info_p, rulepath, &p, up_db, up_user, msg, msgsz) < 0)
    {
      return -1;
    }

  /* build-time statement-type policy: reject DDL/CALL/
   * non-SELECT/kind-mismatch/multi-statement replacements before touching shm. */
  if (qr_check_rewrite_policy (&p, br_info_p->query_rewrite_allow_non_select, msg, msgsz, warn, sizeof (warn)) < 0)
    {
      qr_rule_log_write (br_info_p, false, rulepath, msg);
      free_and_init (p.orig_norm);
      free_and_init (p.rewrite_query);
      return -1;
    }

  base = qr_admin_begin (br_info_p, msg, msgsz, &lock_fd);
  if (base == NULL)
    {
      free_and_init (p.orig_norm);
      free_and_init (p.rewrite_query);
      return -1;
    }

  if (!qr_admin_dbuser_present (base, up_db, up_user))
    {
      snprintf (msg, msgsz, "(db,user) '%s@%s' not present at startup; create the directory and restart the broker",
		up_user, up_db);
    }
  else if (qr_admin_find_active (base, rulepath) < 0)
    {
      snprintf (msg, msgsz, "rule '%s' is not loaded; use 'qr add'", rulepath);
    }
  else if ((newidx = qr_admin_find_active_orig (base, &p, rulepath)) >= 0)
    {
      /* the file's new ORIG collides with a *different* active rule */
      snprintf (msg, msgsz, "same ORIG is already rewritten by active rule '%s'", qr_admin_rule_name (base, newidx));
    }
  else if ((newidx = qr_admin_append (base, &p, msg, msgsz)) >= 0)
    {
      /* disable every prior active entry with this rulepath (append-first keeps the
       * rule live throughout; leftover duplicates from a crash also get cleaned). */
      T_QR_SHM_HEADER *hdr = (T_QR_SHM_HEADER *) base;
      T_QR_RULE *rule = (T_QR_RULE *) (base + hdr->rule_off);
      char *pool = base + hdr->pool_off;
      int i;

      for (i = 0; i < hdr->rule_count; i++)
	{
	  if (i != newidx && !rule[i].disabled && strcmp (qr_slot_name (pool, hdr, i), rulepath) == 0)
	    {
	      rule[i].disabled = 1;
	    }
	}
      hdr->generation++;
      /* the rule is in; msg is free to carry the policy warning back to the caller */
      snprintf (msg, msgsz, "%s", warn);
      rc = 0;
    }

  qr_admin_end (base, lock_fd);
  free_and_init (p.orig_norm);
  free_and_init (p.rewrite_query);

  return rc;
#endif
}

int
qr_admin_disable (const T_BROKER_INFO * br_info_p, const char *rulepath, char *msg, int msgsz)
{
#if defined(WINDOWS)
  snprintf (msg, msgsz, "query rewrite runtime commands are not supported on windows");
  return -1;
#else
  char *base;
  int lock_fd, rc = -1, n = 0, i;
  T_QR_SHM_HEADER *hdr;
  T_QR_RULE *rule;
  char *pool;

  msg[0] = '\0';
  base = qr_admin_begin (br_info_p, msg, msgsz, &lock_fd);
  if (base == NULL)
    {
      return -1;
    }

  hdr = (T_QR_SHM_HEADER *) base;
  rule = (T_QR_RULE *) (base + hdr->rule_off);
  pool = base + hdr->pool_off;

  for (i = 0; i < hdr->rule_count; i++)
    {
      if (!rule[i].disabled && strcmp (qr_slot_name (pool, hdr, i), rulepath) == 0)
	{
	  rule[i].disabled = 1;
	  n++;
	}
    }

  if (n == 0)
    {
      snprintf (msg, msgsz, "no active rule named '%s'", rulepath);
    }
  else
    {
      hdr->generation++;
      rc = 0;
    }

  qr_admin_end (base, lock_fd);

  return rc;
#endif
}

int
qr_admin_enable (const T_BROKER_INFO * br_info_p, const char *rulepath, char *msg, int msgsz)
{
#if defined(WINDOWS)
  snprintf (msg, msgsz, "query rewrite runtime commands are not supported on windows");
  return -1;
#else
  char *base;
  int lock_fd, rc = -1, i, target = -1;
  T_QR_SHM_HEADER *hdr;
  T_QR_RULE *rule;
  char *pool;

  msg[0] = '\0';
  base = qr_admin_begin (br_info_p, msg, msgsz, &lock_fd);
  if (base == NULL)
    {
      return -1;
    }

  hdr = (T_QR_SHM_HEADER *) base;
  rule = (T_QR_RULE *) (base + hdr->rule_off);
  pool = base + hdr->pool_off;

  if (qr_admin_find_active (base, rulepath) >= 0)
    {
      snprintf (msg, msgsz, "rule '%s' is already active", rulepath);
    }
  else
    {
      /* re-activate the most recently added disabled entry of this rulepath */
      for (i = hdr->rule_count - 1; i >= 0; i--)
	{
	  if (rule[i].disabled && strcmp (qr_slot_name (pool, hdr, i), rulepath) == 0)
	    {
	      target = i;
	      break;
	    }
	}

      if (target < 0)
	{
	  snprintf (msg, msgsz, "no disabled rule named '%s'", rulepath);
	}
      else
	{
	  rule[target].admin_seq++;	/* invalidate any CAS process-local failure-disable */
	  QR_BARRIER ();
	  rule[target].disabled = 0;
	  hdr->generation++;
	  rc = 0;
	}
    }

  qr_admin_end (base, lock_fd);

  return rc;
#endif
}

/* ================================================================== *
 *  CAS SIDE : attach and lookup                                      *
 * ================================================================== */

static T_QR_SHM_HEADER *qr_shm = NULL;
static int *qr_bucket = NULL;
static T_QR_RULE *qr_rule = NULL;
static const char *qr_pool = NULL;
static int *qr_dbuser_bucket = NULL;
static T_QR_DBUSER *qr_dbuser = NULL;

/* process-local failure-disable, sized to the segment's max_rules (slots are
 * never reclaimed at runtime so rule_idx may exceed the startup rule_count).
 * a local disable is effective only while it matches the rule's admin_seq, so
 * an admin `qr enable` (which bumps admin_seq) clears it without writing the CAS. */
static unsigned char *qr_local_disabled = NULL;
static int *qr_local_seq = NULL;
static int qr_local_cap = 0;

/* per-process normalization buffer for qr_lookup, allocated once in qr_init()
 * sized to the segment's cfg_max_query_len (QUERY_REWRITE_MAX_QUERY_LEN) instead
 * of the QR_MAX_QUERY_LEN hard ceiling.  CAS is single-threaded: one buffer. */
static char *qr_norm_buf = NULL;
static int qr_norm_buf_size = 0;

/* lazily-filled per-process cache of each rule's validated marker counts.
 * a rule's original/replacement texts and BIND_MAP are immutable once the slot
 * is published (slots are append-only), so a (k_orig, k_rewrite) pair that
 * passed qr_validate_markers once stays valid for the CAS lifetime; -1 = not
 * yet validated.  the authoritative k_orig count comes from get_num_markers here,
 * not from the broker's load-time parse. */
static int *qr_local_k_orig = NULL;
static int *qr_local_k_rewrite = NULL;

/* per-rule consecutive AMBIGUOUS-failure counter (process-local, sized qr_local_cap).
 * a rule is demoted after QR_FAIL_STRIKE_MAX consecutive ambiguous execute failures. */
static int *qr_local_fail_streak = NULL;

/* connection-level early-exit gate: cache whether the current connection's
 * (db, user) has any rule at all */
static char qr_conn_db[QR_NAME_LEN] = { 0 };
static char qr_conn_user[QR_NAME_LEN] = { 0 };

static unsigned int qr_hash_db_user;	/* hash value of conn_db and conn_user */
static int qr_conn_has_rules = -1;	/* -1 unknown, 0 none, 1 some */

int
qr_init (T_SHM_APPL_SERVER * shm_as_p)
{
#if defined(WINDOWS)
  return 0;
#else
  int mid;
  char *base;
  char qr_msg[QR_RELPATH_LEN + 128];

  qr_shm = NULL;
  qr_bucket = NULL;
  qr_rule = NULL;
  qr_pool = NULL;
  qr_dbuser_bucket = NULL;
  qr_dbuser = NULL;
  free_and_init (qr_local_disabled);
  free_and_init (qr_local_seq);
  qr_local_cap = 0;
  free_and_init (qr_norm_buf);
  qr_norm_buf_size = 0;
  free_and_init (qr_local_k_orig);
  free_and_init (qr_local_k_rewrite);
  free_and_init (qr_local_fail_streak);
  qr_conn_db[0] = '\0';
  qr_conn_user[0] = '\0';
  qr_conn_has_rules = -1;

  if (shm_as_p->query_rewrite_shm_key == 0)
    {
      return 0;			/* feature disabled */
    }

  mid = shmget (shm_as_p->query_rewrite_shm_key, 0, 0);
  if (mid == -1)
    {
#if !defined(NDEBUG)
      _er_log_debug (ARG_FILE_LINE, "qr_init: shmget failed for key 0x%x (%s)\n",
		     shm_as_p->query_rewrite_shm_key, strerror (errno));
#endif
      goto error;
    }

  base = (char *) shmat (mid, (char *) 0, SHM_RDONLY);
  if (base == (char *) -1)
    {
#if !defined(NDEBUG)
      _er_log_debug (ARG_FILE_LINE, "qr_init: shmat failed for key 0x%x (%s)\n",
		     shm_as_p->query_rewrite_shm_key, strerror (errno));
#endif
      goto error;
    }

  qr_shm = (T_QR_SHM_HEADER *) base;
  if (qr_shm->magic != QR_SHM_MAGIC)
    {
#if !defined(NDEBUG)
      _er_log_debug (ARG_FILE_LINE, "qr_init: bad magic 0x%x for key 0x%x\n", qr_shm->magic,
		     shm_as_p->query_rewrite_shm_key);
#endif
      shmdt (base);
      qr_shm = NULL;

      goto error;
    }

  /* T_SHM_APPL_SERVER does not carry the owning appl_server_shm_id, so verify the recorded
   * owner still maps to the key we opened (a full owner match is only possible in the admin
   * paths, which know that id).  the structural half is shared with them. */
  if (qr_make_shm_key (qr_shm->owner_shm_id) != shm_as_p->query_rewrite_shm_key
      || !qr_shm_header_sane (qr_shm, mid))
    {
#if !defined(NDEBUG)
      _er_log_debug (ARG_FILE_LINE, "qr_init: inconsistent header for key 0x%x\n", shm_as_p->query_rewrite_shm_key);
#endif
      shmdt (base);
      qr_shm = NULL;

      goto error;
    }

  qr_bucket = (int *) (base + qr_shm->bucket_off);
  qr_rule = (T_QR_RULE *) (base + qr_shm->rule_off);
  qr_dbuser_bucket = (int *) (base + qr_shm->dbuser_bucket_off);
  qr_dbuser = (T_QR_DBUSER *) (base + qr_shm->dbuser_off);
  qr_pool = base + qr_shm->pool_off;

  /* sized to the reserved capacity so runtime-added rule indexes stay in range */
  qr_local_cap = (qr_shm->max_rules > 0) ? qr_shm->max_rules : qr_shm->rule_count;
  qr_local_disabled = (unsigned char *) calloc (qr_local_cap > 0 ? qr_local_cap : 1, sizeof (char));
  qr_local_seq = (int *) calloc (qr_local_cap > 0 ? qr_local_cap : 1, sizeof (int));
  qr_local_k_orig = (int *) malloc (sizeof (int) * (qr_local_cap > 0 ? qr_local_cap : 1));
  qr_local_k_rewrite = (int *) malloc (sizeof (int) * (qr_local_cap > 0 ? qr_local_cap : 1));
  qr_local_fail_streak = (int *) calloc (qr_local_cap > 0 ? qr_local_cap : 1, sizeof (int));

  /* +2: one byte for the over-length sentinel the length filter rejects, one
   * for the terminating NUL (see qr_lookup's early-abort cap) */
  qr_norm_buf_size = qr_shm->cfg_max_query_len + 2;
  qr_norm_buf = (char *) malloc (qr_norm_buf_size);

  if (qr_local_disabled == NULL || qr_local_seq == NULL || qr_norm_buf == NULL
      || qr_local_k_orig == NULL || qr_local_k_rewrite == NULL || qr_local_fail_streak == NULL)
    {
#if !defined(NDEBUG)
      _er_log_debug (ARG_FILE_LINE, "qr_init: out of memory for local arrays (cap %d)\n", qr_local_cap);
#endif
      free_and_init (qr_local_disabled);
      free_and_init (qr_local_seq);
      free_and_init (qr_norm_buf);
      qr_norm_buf_size = 0;
      free_and_init (qr_local_k_orig);
      free_and_init (qr_local_k_rewrite);
      free_and_init (qr_local_fail_streak);
      qr_local_cap = 0;		/* it bounds the arrays just freed */

      shmdt (base);
      qr_shm = NULL;

      goto error;
    }

  for (int i = 0; i < qr_local_cap; i++)
    {
      qr_local_disabled[i] = qr_rule[i].disabled;
      qr_local_seq[i] = qr_rule[i].admin_seq;
      qr_local_k_orig[i] = -1;
      qr_local_k_rewrite[i] = -1;
    }

  snprintf (qr_msg, sizeof (qr_msg), "query rewrite enabled: %d rules loaded (max_rules %d, max_query_len %d)",
	    qr_shm->rule_count, qr_shm->max_rules, qr_shm->cfg_max_query_len);
  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 1, qr_msg);

  return 0;

error:
  snprintf (qr_msg, sizeof (qr_msg), "query rewrite disabled: cannot attach rule segment (shm key 0x%x)",
	    shm_as_p->query_rewrite_shm_key);
  er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 1, qr_msg);

  return -1;
#endif
}

void
qr_final ()
{
  free_and_init (qr_local_disabled);
  free_and_init (qr_local_seq);
  free_and_init (qr_norm_buf);
  qr_norm_buf_size = 0;
  free_and_init (qr_local_k_orig);
  free_and_init (qr_local_k_rewrite);
  free_and_init (qr_local_fail_streak);
  qr_local_cap = 0;		/* it bounds the arrays just freed */

#if !defined(WINDOWS)
  /* detach the read-only rule segment and clear segment pointers,
   * mirroring qr_init's entry-time reset */
  if (qr_shm != NULL)
    {
      shmdt ((void *) qr_shm);
    }
  qr_shm = NULL;
  qr_bucket = NULL;
  qr_rule = NULL;
  qr_pool = NULL;
  qr_dbuser_bucket = NULL;
  qr_dbuser = NULL;
#endif
}

void
qr_set_disabled (int rule_idx)
{
  assert (qr_shm != NULL && rule_idx >= 0 && rule_idx < qr_local_cap);
  assert (qr_local_disabled != NULL);

  /* stamp the admin_seq this disable is based on: qr_is_disabled honours a local
   * disable only while qr_local_seq == admin_seq. */
  qr_local_seq[rule_idx] = qr_rule[rule_idx].admin_seq;
  qr_local_disabled[rule_idx] = 1;
}

int
qr_is_disabled (int rule_idx)
{
  assert (qr_shm != NULL && rule_idx >= 0 && rule_idx < qr_local_cap);
  assert (qr_local_disabled != NULL);

  /* shared admin disable */
  if (qr_rule[rule_idx].disabled)
    {
      return 1;
    }

  /* process-local failure-disable, still valid only if admin_seq is unchanged */
  if (qr_local_disabled[rule_idx])
    {
      if (qr_local_seq[rule_idx] < qr_rule[rule_idx].admin_seq)
	{
	  qr_local_seq[rule_idx] = qr_rule[rule_idx].admin_seq;
	  qr_local_disabled[rule_idx] = 0;
	  qr_local_fail_streak[rule_idx] = 0;	/* re-enabled rule gets a fresh N-strike budget */

	  return 0;
	}
      else if (qr_local_seq[rule_idx] == qr_rule[rule_idx].admin_seq)
	{
	  return 1;
	}
      else
	{
	  assert (false);
	  qr_local_seq[rule_idx] = qr_rule[rule_idx].admin_seq;
	  return 1;
	}
    }

  return 0;
}

/*
 * qr_exec_error_tier () - classify an execute-time error for the demote decision.
 *   return: QR_ERR_TRANSIENT / QR_ERR_DATA / QR_ERR_AMBIGUOUS / QR_ERR_RULE_DEFECT
 *   err(in): the error code returned by the failed execute (server or CAS layer)
 */
QR_ERR_TIER
qr_exec_error_tier (int err)
{
  /* err_info.err_number carries two disjoint namespaces: server codes (0 .. ER_LAST_ERROR
   * = -1374) and CAS layer codes (T_CAS_ERROR_CODE, CAS_ER_DBMS = -10000 and below).
   * only the server ones say anything about the replacement query; a CAS layer failure
   * (bind count, statement pooling, query cancel, out of memory, server disconnected) is
   * infrastructure.  classify those TRANSIENT so they leave the N-strike streak alone --
   * falling through to the DATA default would *reset* it and let an interleaved CAS error
   * postpone the demote of a genuinely broken rule indefinitely. */
  if (err <= CAS_ER_DBMS)
    {
      return QR_ERR_TRANSIENT;
    }

  /* TRANSIENT: infra / transaction -- never a rule defect (streak kept as-is) */
  if (ER_IS_SERVER_DOWN_ERROR (err) || ER_IS_ABORTED_DUE_TO_DEADLOCK (err) || ER_IS_LOCK_TIMEOUT_ERROR (err)
      || err == ER_LK_DEADLOCK_CYCLE_DETECTED || err == ER_MVCC_SERIALIZABLE_CONFLICT || err == ER_INTERRUPTED)
    {
      return QR_ERR_TRANSIENT;
    }

  switch (err)
    {
      /* RULE_DEFECT: the replacement query is unusable regardless of bind values */
    case ER_AU_AUTHORIZATION_FAILURE:
    case ER_AU_SELECT_FAILURE:
    case ER_LC_UNKNOWN_CLASSNAME:
      return QR_ERR_RULE_DEFECT;

      /* AMBIGUOUS: plan built, execute failed; may be data- or rule-caused.
       * ER_PT_EXECUTE includes the executor "Query execution failure #N" catch-all. */
    case ER_PT_EXECUTE:
    case ER_SP_EXECUTE_ERROR:
      return QR_ERR_AMBIGUOUS;

      /* everything else (unique / FK / NOT NULL / overflow / zero-divide / string /
       * resource ...) is treated as bind/data-dependent: the rule executed, not a defect. */
    default:
      return QR_ERR_DATA;
    }
}

/*
 * qr_record_exec_result () - record one execute outcome of a rewritten statement
 *   and decide whether the rule must be demoted (caller then disables it and
 *   self-heals the handle to the original query).  Handles the per-rule failure
 *   streak inline (reset on success/DATA, ++ on AMBIGUOUS, unchanged on TRANSIENT).
 *   return: 1 to demote now, 0 otherwise
 */
int
qr_record_exec_result (int rule_idx, bool succeeded, QR_ERR_TIER tier, bool all_rows_failed)
{
  assert (qr_shm != NULL && rule_idx >= 0 && rule_idx < qr_local_cap);
  assert (qr_local_fail_streak != NULL);

  if (succeeded)
    {
      qr_local_fail_streak[rule_idx] = 0;
      return 0;
    }

  switch (tier)
    {
    case QR_ERR_RULE_DEFECT:
      return 1;			/* bind-independent -> demote at once */

    case QR_ERR_AMBIGUOUS:
      if (all_rows_failed || ++qr_local_fail_streak[rule_idx] >= QR_FAIL_STRIKE_MAX)
	{
	  return 1;		/* whole batch failed, or K consecutive failures */
	}
      return 0;

    case QR_ERR_DATA:
      qr_local_fail_streak[rule_idx] = 0;	/* rule executed -> not a defect */
      return 0;

    case QR_ERR_TRANSIENT:
    default:
      return 0;			/* leave the streak unchanged */
    }
}

/* slot base of a rule the caller has already range-checked.  the stored offsets are not
 * read: their value is always this function of the index, so a corrupt entry cannot
 * point outside the pool. */
static const char *
qr_cas_slot (int rule_idx)
{
  return qr_pool + rule_idx * qr_shm->pool_slot;
}

const char *
qr_get_rewrite_query (int rule_idx)
{
  assert (qr_shm != NULL && rule_idx >= 0 && rule_idx < qr_local_cap);

  return qr_cas_slot (rule_idx) + (qr_shm->cfg_max_query_len + 1);
}

const char *
qr_get_orig_query (int rule_idx)
{
  assert (qr_shm != NULL && rule_idx >= 0 && rule_idx < qr_local_cap);

  return qr_cas_slot (rule_idx);
}

const char *
qr_get_rulepath (int rule_idx)
{
  assert (qr_shm != NULL && rule_idx >= 0 && rule_idx < qr_local_cap);

  return qr_cas_slot (rule_idx) + 2 * (qr_shm->cfg_max_query_len + 1);
}

int
qr_validate_markers (int rule_idx, int k_orig, int k_rewrite)
{
  T_QR_RULE *r;
  int j, valid;

  assert (qr_shm != NULL && rule_idx >= 0 && rule_idx < qr_local_cap);

  r = &qr_rule[rule_idx];
  valid = 1;

  if (k_orig < 0 || k_rewrite < 0 || k_rewrite > QR_MAX_BINDS || k_orig > QR_MAX_BINDS)
    {
      valid = 0;
    }
  else if (k_rewrite == 0)
    {
      /* replacement query uses no markers */
      valid = (r->num_map_entries <= 0) ? 1 : 0;
    }
  else if (r->num_map_entries <= 0)
    {
      /* BIND_MAP omitted, identity mapping requires K_rewrite == K_orig */
      valid = (k_rewrite == k_orig) ? 1 : 0;
    }
  else
    {
      /* explicit BIND_MAP: every replacement marker 1..k_rewrite must be covered
       * exactly once (src_orig_pos[j] set, in 1..k_orig) and there must be no
       * extra entries. */
      if (r->num_map_entries != k_rewrite)
	{
	  valid = 0;
	}

      for (j = 0; valid && j < k_rewrite; j++)
	{
	  if (r->src_orig_pos[j] < 1 || r->src_orig_pos[j] > k_orig)
	    {
	      valid = 0;
	    }
	}
    }

#if !defined(NDEBUG)
  if (!valid)
    {
      _er_log_debug (ARG_FILE_LINE, "qr_validate_markers: rule %d invalid BIND_MAP "
		     "(k_orig %d, k_rewrite %d, num_map_entries %d)\n", rule_idx, k_orig, k_rewrite,
		     r->num_map_entries);
    }
#endif

  return valid;
}

short *
qr_get_bind_src (int rule_idx)
{
  assert (qr_shm != NULL && rule_idx >= 0 && rule_idx < qr_local_cap);

  return ((qr_rule[rule_idx].num_map_entries <= 0) ? NULL : qr_rule[rule_idx].src_orig_pos);
}

/* returns the cached K_orig when this rule already passed marker validation
 * with the same K_rewrite; -1 = caller must compute K_orig and validate. */
int
qr_get_valid_k_orig (int rule_idx, int k_rewrite)
{
  assert (qr_shm != NULL && rule_idx >= 0 && rule_idx < qr_local_cap);
  assert (qr_local_k_orig != NULL && qr_local_k_rewrite != NULL);

  return (qr_local_k_rewrite[rule_idx] == k_rewrite) ? qr_local_k_orig[rule_idx] : -1;
}

/* cache a (k_orig, k_rewrite) pair that just passed qr_validate_markers */
void
qr_set_valid_k_orig (int rule_idx, int k_orig, int k_rewrite)
{
  assert (qr_shm != NULL && rule_idx >= 0 && rule_idx < qr_local_cap);
  assert (qr_local_k_orig != NULL && qr_local_k_rewrite != NULL);

  if (k_orig >= 0)
    {
      qr_local_k_orig[rule_idx] = k_orig;
      qr_local_k_rewrite[rule_idx] = k_rewrite;
    }
}

void
qr_load_dbuser_has_rules (const char *db_name, const char *user_name)
{
  int b, hops;

  if (qr_shm == NULL || qr_shm->dbuser_count == 0 || qr_shm->dbuser_hash_size == 0)
    {
      return;
    }

  qr_upper_copy (qr_conn_db, db_name ? db_name : "", QR_NAME_LEN);
  qr_upper_copy (qr_conn_user, user_name && strlen (user_name) > 0 ? user_name : "PUBLIC", QR_NAME_LEN);
  qr_hash_db_user = qr_hash_str (qr_conn_db) ^ qr_hash_str (qr_conn_user);

  qr_conn_has_rules = 0;
  b = (int) (qr_hash_db_user % (unsigned int) qr_shm->dbuser_hash_size);
  /* bounded like the rule chain in qr_lookup: head-prepend means a legitimate chain never
   * exceeds dbuser_count, and the cap also stops a corrupt next_idx cycle. */
  for (b = qr_dbuser_bucket[b], hops = 0; b != -1 && hops <= qr_shm->dbuser_count; b = qr_dbuser[b].next_idx, hops++)
    {
      if (b < 0 || b >= qr_local_cap)
	{
	  break;
	}
      if (strncmp (qr_dbuser[b].db_name, qr_conn_db, QR_NAME_LEN) == 0
	  && strncmp (qr_dbuser[b].user_name, qr_conn_user, QR_NAME_LEN) == 0)
	{
	  qr_conn_has_rules = 1;
	}
    }
}

int
qr_lookup (const char *sql_stmt, int sql_len)
{
  int norm_len;
  int norm_cap;
  unsigned int h, norm_h;
  int b, idx, hops;

  if (qr_shm == NULL || qr_norm_buf == NULL || qr_conn_has_rules <= 0 || qr_shm->rule_count == 0 || sql_stmt == NULL)
    {
      return -1;
    }

  /* cheap pre-filter before the O(len) normalization.  normalization can *grow* the text:
   * a hint keeps one separator space on each side, which canonicalizes the whitespace
   * around it so that writing the hint with or without inner spaces yields one key.  that
   * is at most 2 bytes per hint token of >= 3 input bytes, so norm_len always stays below
   * 2 * sql_len + 2; a query shorter than that bound cannot reach the shortest rule. */
  if (2 * sql_len + 2 < qr_shm->min_query_len)
    {
      return -1;
    }

  /* normalize original query and get hash.  cap the output at the longest
   * registered rule length: a query that normalizes past max_query_len can
   * never match any rule, so qr_normalize_query aborts early with -1 instead
   * of scanning/hashing the rest of a large statement. */
  norm_cap = MIN (qr_shm->max_query_len + 2, qr_norm_buf_size);
  norm_len = qr_normalize_query (qr_norm_buf, norm_cap, sql_stmt, &norm_h, NULL);
  if (norm_len <= 0)
    {
      return -1;
    }
  else if (norm_len < qr_shm->min_query_len || norm_len > qr_shm->max_query_len)
    {
      return -1;
    }

  h = norm_h ^ qr_hash_db_user;

  /* hash bucket walk: the stored per-rule hash/length kill almost every
   * chain miss with int compares before any string work. */
  b = (int) (h % (unsigned int) qr_shm->hash_size);
  idx = -1;
  /* cap the walk: chains are built by head-prepend so they can never legitimately hold
   * more than rule_count entries, and the cap bounds the walk even for a corrupt
   * next_idx cycle. */
  for (b = qr_bucket[b], hops = 0; b != -1 && hops <= qr_shm->rule_count; b = qr_rule[b].next_idx, hops++)
    {
      if (b < 0 || b >= qr_local_cap)
	{
	  break;		/* corrupt index: stop while every access below is still in range */
	}
      if (qr_rule[b].hash != h || qr_rule[b].orig_len != norm_len)
	{
	  continue;
	}

      if (qr_is_disabled (b))
	{
	  continue;		/* reload leaves a disabled old version with the same key */
	}

      if (strncmp (qr_rule[b].db_name, qr_conn_db, QR_NAME_LEN) == 0
	  && strncmp (qr_rule[b].user_name, qr_conn_user, QR_NAME_LEN) == 0
	  && memcmp (qr_cas_slot (b), qr_norm_buf, norm_len) == 0)
	{
	  idx = b;
	  break;
	}
    }

  return idx;
}
