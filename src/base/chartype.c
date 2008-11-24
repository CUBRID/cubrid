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
 * chartype.c : character type checking functions
 */

#ident "$Id$"

#include "config.h"

#include "chartype.h"

/*
 * char_islower() - test for a lower case character
 *   return: non-zero if c is a lower case character,
 *           0 otherwise.
 *   c (in): the character to be tested
 */
int
char_islower (int c)
{
  return ((c) >= 'a' && (c) <= 'z');
}

/*
 * char_isupper() - test for a upper case character
 *   return: non-zero if c is a upper case character,
 *           0 otherwise.
 *   c (in): the character to be tested
 */
int
char_isupper (int c)
{
  return ((c) >= 'A' && (c) <= 'Z');
}

/*
 * char_isalpha() - test for a alphabetic character
 *   return: non-zero if c is a alphabetic character,
 *           0 otherwise.
 *   c (in): the character to be tested
 */
int
char_isalpha (int c)
{
  return (char_islower ((c)) || char_isupper ((c)));
}

/*
 * char_isdigit() - test for a decimal digit character
 *   return: non-zero if c is a decimal digit character,
 *           0 otherwise.
 *   c (in): the character to be tested
 */
int
char_isdigit (int c)
{
  return ((c) >= '0' && (c) <= '9');
}

/*
 * char_isxdigit() - test for a hexa decimal digit character
 *   return: non-zero if c is a hexa decimal digit character,
 *           0 otherwise.
 *   c (in): the character to be tested
 */
int
char_isxdigit (int c)
{
  return (char_isdigit ((c)) || ((c) >= 'a' && (c) <= 'f')
	  || ((c) >= 'A' && (c) <= 'F'));
}

/*
 * char_isalnum() - test for a alphanumeric character
 *   return: non-zero if c is a alphanumeric character,
 *           0 otherwise.
 *   c (in): the character to be tested
 */
int
char_isalnum (int c)
{
  return (char_isalpha ((c)) || char_isdigit ((c)));
}

/*
 * char_isspace() - test for a white space character
 *   return: non-zero if c is a white space character,
 *           0 otherwise.
 *   c (in): the character to be tested
 */
int
char_isspace (int c)
{
  return ((c) == ' ' || (c) == '\t' || (c) == '\r' || (c) == '\n');
}

/*
 * char_iseol() - test for a end-of-line character
 *   return: non-zero if c is a end-of-line character,
 *           0 otherwise.
 *   c (in): the character to be tested
 */
int
char_iseol (int c)
{
  return ((c) == '\r' || (c) == '\n');
}

/*
 * char_isascii() - test for a US-ASCII character
 *   return: non-zero if c is a US-ASCII character,
 *           0 otherwise.
 *   c (in): the character to be tested
 */
int
char_isascii (int c)
{
  return ((c) >= 1 && (c) <= 127);
}

/*
 * char_tolower() - convert uppercase character to lowercase
 *   return: lowercase character corresponding to the argument
 *   c (in): the character to be converted
 */
int
char_tolower (int c)
{
  return (char_isupper ((c)) ? ((c) - ('A' - 'a')) : (c));
}

/*
 * char_toupper() - convert lowercase character to uppercase
 *   return: uppercase character corresponding to the argument
 *   c (in): the character to be converted
 */
int
char_toupper (int c)
{
  return (char_islower ((c)) ? ((c) + ('A' - 'a')) : (c));
}

/* Specialized for ISO 8859-1 */
static const int A_GRAVE_ACCENT = 192;
static const int MULT_ISO8859 = 215;
static const int CAPITAL_THORN = 222;

static const int a_GRAVE_ACCENT = 224;
static const int DIV_ISO8859 = 247;
static const int SMALL_THORN = 254;
/*
 * char_isupper_iso8859() - test for a upper case character for iso-8859
 *   return: non-zero if c is a upper case character,
 *           0 otherwise.
 *   c (in): the character to be tested
 */
int
char_isupper_iso8859 (int c)
{
  return ((c) >= A_GRAVE_ACCENT && (c) <= CAPITAL_THORN
	  && (c) != MULT_ISO8859);
}

/*
 * char_islower_iso8859() - test for a lower case character for iso-8859
 *   return: non-zero if c is a lower case character,
 *           0 otherwise.
 *   c (in): the character to be tested
 */
int
char_islower_iso8859 (int c)
{
  return ((c) >= a_GRAVE_ACCENT && (c) <= SMALL_THORN && (c) != DIV_ISO8859);
}

/*
 * char_tolower_iso8859() - convert uppercase iso-8859 character to lowercase
 *   return: lowercase character corresponding to the argument
 *   c (in): the character to be converted
 */
int
char_tolower_iso8859 (int c)
{
  return (char_isupper_iso8859 ((c)) ? ((c) - ('A' - 'a')) : (c));
}

/*
 * char_toupper_iso8859() - convert lowercase iso-8859 character to uppercase
 *   return: uppercase character corresponding to the argument
 *   c (in): the character to be converted
 */
int
char_toupper_iso8859 (int c)
{
  return (char_islower ((c)) ? ((c) + ('A' - 'a')) : (c));
}
