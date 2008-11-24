/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
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
#include "release_string.h"
#include "message_catalog.h"
#include "chartype.h"
#include "language_support.h"
#include "environment_variable.h"
#include "log_comm.h"
#include "log_manager.h"


#define CSQL_NAME_MAX_LENGTH 100
#define MAXPATCHLEN  256

/*
 * COMPATIBILITY_RULE - Structure that encapsulates compatibility rules.
 *                      For two revision levels, both a compatibility type
 *                      and optional fixup function list is defined.
 */
typedef struct compatibility_rule
{
  float database_level;
  float system_level;
  REL_COMPATIBILITY compatibility;
  REL_FIXUP_FUNCTION *functions;
} COMPATIBILITY_RULE;

/*
 * Copyright Information
 */
static const char *copyright_header = "\
Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.\n\
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

/*
 * Disk (database image) Version Compatibility
 */
static float disk_compatibility_level = 8.0;

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
 * rel_copyright_header - Copyright header from the message catalog
 *   return: static char string
 */
const char *
rel_copyright_header (void)
{
  const char *name;

  lang_init ();
  name = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_GENERAL,
			 MSGCAT_GENERAL_COPYRIGHT_HEADER);
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
  name = msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_GENERAL,
			 MSGCAT_GENERAL_COPYRIGHT_BODY);
  return (name) ? name : copyright_body;
}

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
 * compatibility_rules - Static table of compatibility rules.                                    *
 *         Each time a change is made to the disk_compatibility_level
 *         a rule needs to be added to this table.
 *         If pair of numbers is absent from this table, the two are considered
 *         to be incompatible.
 */
static COMPATIBILITY_RULE compatibility_rules[] = {
  /* a database_level of zero indicates the end of the table */
  {0.0, 0.0, REL_NOT_COMPATIBLE, NULL}
};

/*
 * rel_is_disk_compatible - Test compatibility of disk (database image)
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
rel_is_disk_compatible (float db_level, REL_FIXUP_FUNCTION ** fixups)
{
  COMPATIBILITY_RULE *rule;
  REL_COMPATIBILITY compat;
  REL_FIXUP_FUNCTION *functions;

  functions = NULL;

  if (disk_compatibility_level == db_level)
    {
      compat = REL_FULLY_COMPATIBLE;
    }
  else
    {
      compat = REL_NOT_COMPATIBLE;
      for (rule = &compatibility_rules[0];
	   rule->database_level != 0 && compat == REL_NOT_COMPATIBLE; rule++)
	{

	  if (rule->database_level == db_level
	      && rule->system_level == disk_compatibility_level)
	    {
	      compat = rule->compatibility;
	      functions = rule->functions;
	    }
	}
    }

  if (fixups != NULL)
    *fixups = functions;

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
  char *a_temp, *b_temp;

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
	  a = strtol (a_temp, &a_temp, 10);
	  b = strtol (b_temp, &b_temp, 10);
	  if (a < b)
	    retval = -1;
	  else if (a > b)
	    retval = 1;
	  /*
	   * This skips over the '.'.
	   * This means that "?..?" will parse out to "?.?".
	   */
	  while (*a_temp && *a_temp == '.')
	    a_temp++;
	  while (*b_temp && *b_temp == '.')
	    b_temp++;
	  if (*a_temp && *b_temp
	      && char_isalpha (*a_temp) && char_isalpha (*b_temp))
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
 * rel_is_log_compatible - Test compatiblility of log file format
 *   return: true if compatible
 *   writer_rel_str(in): release string of the log writer (log file)
 *   reader_rel_str(in): release string of the log reader (system being run)
 */
bool
rel_is_log_compatible (const char *writer_rel_str, const char *reader_rel_str)
{
  char temp_a[REL_MAX_RELEASE_LENGTH];
  char temp_b[REL_MAX_RELEASE_LENGTH];
  int i;

  /* remove not_a_number in release_string */
  for (i = 0; *writer_rel_str; writer_rel_str++)
    {
      if (*writer_rel_str == '.' || char_isdigit (*writer_rel_str))
	{
	  temp_a[i++] = *writer_rel_str;
	}
    }
  temp_a[i] = '\0';
  for (i = 0; *reader_rel_str; reader_rel_str++)
    {
      if (*reader_rel_str == '.' || char_isdigit (*reader_rel_str))
	{
	  temp_b[i++] = *reader_rel_str;
	}
    }
  temp_b[i] = '\0';

  /* if the same version, OK */
  if (strcmp (temp_a, temp_b) == 0)
    return true;

  return false;
}
