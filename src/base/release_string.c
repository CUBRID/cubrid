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
 * release_string.c - release related information (at client and server)
 *
 * Note: This file contains some very simple functions related to version and
 *       releases of CUBRID. Among these functions are copyright information
 *       of CUBRID products, name of CUBRID engine, and version.
 */

#ident "$Id$"

#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "porting.h"
#include "release_string.h"
#include "message_catalog.h"
#include "chartype.h"
#include "language_support.h"
#include "environment_variable.h"
#include "log_comm.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

/*
 * COMPATIBILITY_RULE - Structure that encapsulates compatibility rules.
 *                      For two revision levels, both a compatibility type
 *                      and optional fixup function list is defined.
 */

typedef struct version
{
  unsigned char major;
  unsigned char minor;
  unsigned short patch;
} REL_VERSION;

typedef struct compatibility_rule
{
  REL_VERSION base;
  REL_VERSION apply;
  REL_COMPATIBILITY compatibility;
  REL_FIXUP_FUNCTION *fix_function;
} COMPATIBILITY_RULE;

typedef enum
{
  CHECK_LOG_COMPATIBILITY,
  CHECK_NET_PROTOCOL_COMPATIBILITY
} COMPATIBILITY_CHECK_MODE;

/*
 * Copyright Information
 */
static const char *copyright_header = "\
Copyright (C) 2008 Search Solution Corporation.\nCopyright (C) 2016 CUBRID Corporation.\n\
";

static const char *copyright_body = "\
Copyright Information\n\
";

/*
 * CURRENT VERSIONS
 */
#define makestring1(x) #x
#define makestring(x) makestring1(x)

static const char *release_string = makestring (RELEASE_STRING);
static const char *major_release_string = makestring (MAJOR_RELEASE_STRING);
static const char *build_number = makestring (BUILD_NUMBER);
static const char *package_string = PACKAGE_STRING;
static const char *build_os = makestring (BUILD_OS);
static const char *build_type = makestring (BUILD_TYPE);

#if defined (VERSION_STRING)
static const char *version_string = VERSION_STRING;
#endif /* VERSION_STRING */
static int bit_platform = __WORDSIZE;

static REL_COMPATIBILITY rel_get_compatible_internal (const char *base_rel_str, const char *apply_rel_str,
						      COMPATIBILITY_CHECK_MODE check, REL_VERSION rules[]);

/*
 * Disk (database image) Version Compatibility
 */
static float disk_compatibility_level = 11.4f;

/*
 * rel_copy_version_string - version string of the product
 *   return: void
 */
void
rel_copy_version_string (char *buf, size_t len)
{
#if defined (CUBRID_OWFS)
  snprintf (buf, len, "%s (%s) (%dbit owfs %s build for %s) (%s %s)", rel_name (), rel_build_number (),
	    rel_bit_platform (), rel_build_type (), rel_build_os (), __DATE__, __TIME__);
#else /* CUBRID_OWFS */
#if defined (VERSION_STRING)
  snprintf (buf, len, "%s (%s) (%dbit %s build for %s) (%s %s)", rel_name (), rel_version_string (),
	    rel_bit_platform (), rel_build_type (), rel_build_os (), __DATE__, __TIME__);
#else /* VERSION_STRING */
  snprintf (buf, len, "%s (%s) (%dbit %s build for %s) (%s %s)", rel_name (), rel_build_number (),
	    rel_bit_platform (), rel_build_type (), rel_build_os (), __DATE__, __TIME__);
#endif /* VERSION_STRING */
#endif /* CUBRID_OWFS */
}

/*
 * rel_name - Name of the product from the message catalog
 *   return: static character string
 */
const char *
rel_name (void)
{
  return package_string;
}

/*
 * rel_release_string - Release number of the product
 *   return: static char string
 */
const char *
rel_release_string (void)
{
  return release_string;
}

/*
 * rel_major_release_string - Major release portion of the release string
 *   return: static char string
 */
const char *
rel_major_release_string (void)
{
  return major_release_string;
}

/*
 * rel_build_number - Build bumber portion of the release string
 *   return: static char string
 */
const char *
rel_build_number (void)
{
  return build_number;
}

/*
 * rel_build_os - Build OS portion of the release string
 *   return: static char string
 */
const char *
rel_build_os (void)
{
  return build_os;
}

/*
 * rel_build_type - Build type portion of the release string
 *   build, release, coverage, profile
 *   return: static char string
 */
const char *
rel_build_type (void)
{
  return build_type;
}

#if defined (VERSION_STRING)
/*
 * rel_version_string - Full version string of the product
 *   return: static char string
 */
const char *
rel_version_string (void)
{
  return version_string;
}
#endif /* VERSION_STRING */

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * rel_copyright_header - Copyright header from the message catalog
 *   return: static char string
 */
const char *
rel_copyright_header (void)
{
  const char *name;

  lang_init ();
  name = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_GENERAL, MSGCAT_GENERAL_COPYRIGHT_HEADER);
  return (name) ? name : copyright_header;
}

/*
 * rel_copyright_body - Copyright body fromt he message catalog
 *   return: static char string
 */
const char *
rel_copyright_body (void)
{
  const char *name;

  lang_init ();
  name = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_GENERAL, MSGCAT_GENERAL_COPYRIGHT_BODY);
  return (name) ? name : copyright_body;
}
#endif /* ENABLE_UNUSED_FUNCTION */

/*
 * rel_disk_compatible - Disk compatibility level
 *   return:
 */
float
rel_disk_compatible (void)
{
  return disk_compatibility_level;
}


/*
 * rel_set_disk_compatible - Change disk compatibility level
 *   return: none
 *   level(in):
 */
void
rel_set_disk_compatible (float level)
{
  disk_compatibility_level = level;
}

/*
 * rel_platform - built platform
 *   return: none
 *   level(in):
 */
int
rel_bit_platform (void)
{
  return bit_platform;
}

/*
 * compatibility_rules - Static table of compatibility rules.
 *         Each time a change is made to the disk_compatibility_level
 *         a rule needs to be added to this table.
 *         If pair of numbers is absent from this table, the two are considered
 *         to be incompatible.
 * {base_level (of database), apply_level (of system), compatibility, fix_func}
 */
static COMPATIBILITY_RULE disk_compatibility_rules[] = {
  /* a zero indicates the end of the table */
  {{0, 0, 0}, {0, 0, 0}, REL_NOT_COMPATIBLE, NULL}
};

/*
 * rel_get_disk_compatible - Test compatibility of disk (database image)
 *                          Check a disk compatibility number stored in
 *                          a database with the disk compatibility number
 *                          for the system being run
 *   return: One of the three compatibility constants (REL_FULLY_COMPATIBLE,
 *           REL_FORWARD_COMPATIBLE, and REL_BACKWARD_COMPATIBLE) or
 *           REL_NOT_COMPATIBLE if they are not compatible
 *   db_level(in):
 *   REL_FIXUP_FUNCTION(in): function pointer table
 *
 * Note: The rules for compatibility are stored in the compatibility_rules
 *       table.  Whenever the disk_compatibility_level variable is changed
 *       an entry had better be made in the table.
 */
REL_COMPATIBILITY
rel_get_disk_compatible (float db_level, REL_FIXUP_FUNCTION ** fixups)
{
  COMPATIBILITY_RULE *rule;
  REL_COMPATIBILITY compat;
  REL_FIXUP_FUNCTION *func;

  func = NULL;

  if (disk_compatibility_level == db_level)
    {
      compat = REL_FULLY_COMPATIBLE;
    }
  else
    {
      compat = REL_NOT_COMPATIBLE;
      for (rule = &disk_compatibility_rules[0]; rule->base.major != 0 && compat == REL_NOT_COMPATIBLE; rule++)
	{
	  float base_level, apply_level;

	  base_level = (float) (rule->base.major + (rule->base.minor / 10.0));
	  apply_level = (float) (rule->apply.major + (rule->apply.minor / 10.0));

	  if (base_level == db_level && apply_level == disk_compatibility_level)
	    {
	      compat = rule->compatibility;
	      func = rule->fix_function;
	    }
	}
    }

  if (fixups != NULL)
    {
      *fixups = func;
    }

  return compat;
}


/* Compare release strings.
 *
 * Returns:  < 0, if rel_a is earlier than rel_b
 *          == 0, if rel_a is the same as rel_b
 *           > 0, if rel_a is later than rel_b
 */
/*
 * rel_compare - Compare release strings, A and B
 *   return:  < 0, if rel_a is earlier than rel_b
 *           == 0, if rel_a is the same as rel_b
 *            > 0, if rel_a is later than rel_b
 *   rel_a(in): release string A
 *   rel_b(in): release string B
 *
 * Note:
 */
int
rel_compare (const char *rel_a, const char *rel_b)
{
  int a, b, retval = 0;
  char *a_temp, *b_temp, *end_p;

  /*
   * If we get a NULL for one of the values (and we shouldn't), guess that
   * the versions are the same.
   */
  if (!rel_a || !rel_b)
    {
      retval = 0;
    }
  else
    {
      /*
       * Compare strings
       */
      a_temp = (char *) rel_a;
      b_temp = (char *) rel_b;
      /*
       * The following loop terminates if we determine that one string
       * is greater than the other, or we reach the end of one of the
       * strings.
       */
      while (!retval && *a_temp && *b_temp)
	{
	  str_to_int32 (&a, &end_p, a_temp, 10);
	  a_temp = end_p;
	  str_to_int32 (&b, &end_p, b_temp, 10);
	  b_temp = end_p;
	  if (a < b)
	    {
	      retval = -1;
	    }
	  else if (a > b)
	    {
	      retval = 1;
	    }
	  /*
	   * This skips over the '.'.
	   * This means that "?..?" will parse out to "?.?".
	   */
	  while (*a_temp && *a_temp == '.')
	    {
	      a_temp++;
	    }
	  while (*b_temp && *b_temp == '.')
	    {
	      b_temp++;
	    }
	  if (*a_temp && *b_temp && char_isalpha (*a_temp) && char_isalpha (*b_temp))
	    {
	      if (*a_temp != *b_temp)
		retval = -1;
	      a_temp++;
	      b_temp++;
	    }
	}

      if (!retval)
	{
	  /*
	   * Both strings are the same up to this point.  If the rest is zeros,
	   * they're still equal.
	   */
	  while (*a_temp)
	    {
	      if (*a_temp != '.' && *a_temp != '0')
		{
		  retval = 1;
		  break;
		}
	      a_temp++;
	    }
	  while (*b_temp)
	    {
	      if (*b_temp != '.' && *b_temp != '0')
		{
		  retval = -1;
		  break;
		}
	      b_temp++;
	    }
	}
    }
  return retval;
}

/*
 * log compatibility matrix
 */
static REL_VERSION log_incompatible_versions[] = {
  {10, 0, 0},

  /* PLEASE APPEND HERE versions that are incompatible with existing ones. */
  /* NOTE that versions are kept as ascending order. */

  /* zero indicates the end of the table */
  {0, 0, 0}
};

/*
 * rel_is_log_compatible - Test compatiblility of log file format
 *   return: true if compatible
 *   writer_rel_str(in): release string of the log writer (log file)
 *   reader_rel_str(in): release string of the log reader (system being run)
 */
bool
rel_is_log_compatible (const char *writer_rel_str, const char *reader_rel_str)
{
  REL_COMPATIBILITY compat;

  compat =
    rel_get_compatible_internal (writer_rel_str, reader_rel_str, CHECK_LOG_COMPATIBILITY, log_incompatible_versions);
  if (compat == REL_NOT_COMPATIBLE)
    {
      return false;
    }

  return true;
}

/*
 * network compatibility matrix
 */
static REL_VERSION net_incompatible_versions[] = {
  {10, 0, 0},

  /* PLEASE APPEND HERE versions that are incompatible with existing ones. */
  /* NOTE that versions are kept as ascending order. */

  /* zero indicates the end of the table */
  {0, 0, 0}
};

/*
 * rel_get_net_compatible - Compare the release strings from
 *                          the server and client to determine compatibility.
 * return: REL_COMPATIBILITY
 *  REL_NOT_COMPATIBLE if the client and the server are not compatible
 *  REL_FULLY_COMPATIBLE if the client and the server are compatible
 *  REL_FORWARD_COMPATIBLE if the client is forward compatible with the server
 *                         if the server is backward compatible with the client
 *                         the client is older than the server
 *  REL_BACKWARD_COMPATIBLE if the client is backward compatible with the server
 *                          if the server is forward compatible with the client
 *                          the client is newer than the server
 *
 *   client_rel_str(in): client's release string
 *   server_rel_str(in): server's release string
 */
REL_COMPATIBILITY
rel_get_net_compatible (const char *client_rel_str, const char *server_rel_str)
{
  return rel_get_compatible_internal (server_rel_str, client_rel_str, CHECK_NET_PROTOCOL_COMPATIBILITY,
				      net_incompatible_versions);
}

/*
 * rel_get_compatible_internal - Compare the release to determine compatibility.
 *   return: REL_COMPATIBILITY
 *
 *   base_rel_str(in): base release string (of database)
 *   apply_rel_str(in): applier's release string (of system)
 *   rules(in): rules to determine forward/backward compatibility
 */
static REL_COMPATIBILITY
rel_get_compatible_internal (const char *base_rel_str, const char *apply_rel_str, COMPATIBILITY_CHECK_MODE check,
			     REL_VERSION versions[])
{
  REL_VERSION *version, *base_version, *apply_version;
  char *base, *apply, *str_a, *str_b;
  int val;

  unsigned char base_major, base_minor, apply_major, apply_minor;
  unsigned short base_patch, apply_patch;

  if (apply_rel_str == NULL || base_rel_str == NULL)
    {
      return REL_NOT_COMPATIBLE;
    }

  /* release string should be in the form of <major>.<minor>[.<patch>] */

  /* check major number */
  str_to_int32 (&val, &str_a, apply_rel_str, 10);
  apply_major = (unsigned char) val;
  str_to_int32 (&val, &str_b, base_rel_str, 10);
  base_major = (unsigned char) val;
  if (apply_major == 0 || base_major == 0)
    {
      return REL_NOT_COMPATIBLE;
    }

  /* skip '.' */
  while (*str_a && *str_a == '.')
    {
      str_a++;
    }
  while (*str_b && *str_b == '.')
    {
      str_b++;
    }

  /* check minor number */
  apply = str_a;
  base = str_b;
  str_to_int32 (&val, &str_a, apply, 10);
  apply_minor = (unsigned char) val;
  str_to_int32 (&val, &str_b, base, 10);
  base_minor = (unsigned char) val;

  /* skip '.' */
  while (*str_a && *str_a == '.')
    {
      str_a++;
    }
  while (*str_b && *str_b == '.')
    {
      str_b++;
    }

  if (check == CHECK_NET_PROTOCOL_COMPATIBILITY)
    {
      if (apply_major != base_major)
	{
	  return REL_NOT_COMPATIBLE;
	}
      if (apply_minor != base_minor)
	{
	  return REL_NOT_COMPATIBLE;
	}
    }

  /* check patch number */
  apply = str_a;
  base = str_b;
  str_to_int32 (&val, &str_a, apply, 10);
  apply_patch = (unsigned short) val;
  str_to_int32 (&val, &str_b, base, 10);
  base_patch = (unsigned short) val;
  if (apply_major == base_major && apply_minor == base_minor && apply_patch == base_patch)
    {
      return REL_FULLY_COMPATIBLE;
    }

  base_version = NULL;
  apply_version = NULL;
  for (version = &versions[0]; version->major != 0; version++)
    {
      if (base_major >= version->major && base_minor >= version->minor && base_patch >= version->patch)
	{
	  base_version = version;
	}
      if (apply_major >= version->major && apply_minor >= version->minor && apply_patch >= version->patch)
	{
	  apply_version = version;
	}
    }

  if (base_version == NULL || apply_version == NULL || base_version != apply_version)
    {
      return REL_NOT_COMPATIBLE;
    }
  else
    {
      return REL_FULLY_COMPATIBLE;
    }
}
