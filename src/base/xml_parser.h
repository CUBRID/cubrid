/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */


/*
 * xml_parser.h : XML Parser for CUBRID
 *
 */

#ifndef _XML_PARSER_H_
#define _XML_PARSER_H_

#ident "$Id$"

#include "config.h"
/* static linking for expat */
#define XML_STATIC
#include "expat.h"

#define XML_CUB_NO_ERROR		  0
#define XML_CUB_ERR_BASE		  1
#define XML_CUB_SCHEMA_BROKEN		  (XML_CUB_ERR_BASE)
#define XML_CUB_OUT_OF_MEMORY		  (XML_CUB_ERR_BASE + 1)
#define XML_CUB_ERR_HEADER_ENCODING	  (XML_CUB_ERR_BASE + 2)
#define XML_CUB_ERR_FILE_READ		  (XML_CUB_ERR_BASE + 3)
#define XML_CUB_ERR_PARSER		  (XML_CUB_ERR_BASE + 4)


/* XML parser schema */
#define MAX_ELEMENT_NAME  30
#define MAX_ELEMENT_FULL_NAME  256

/* 
 * start_xxxxxxxxx - XML element start function
 *		       function be called when an element starts 
 *
 * return: 0 validation OK enter this element , 1 validation NOK do not enter
 *         -1 error abort parsing
 * (data): user data
 * (attr): attribute/value pair array
 */
typedef int (*ELEM_START_FUNC) (void *, const char **);

/*
 * end_xxxxxxxxx - XML element end function
 *		     function be called when an element starts 
 *
 * return: 0 parser OK, non-zero value if parser NOK and stop parsing
 * (data): user data
 * (el_name): element name
 */
typedef int (*ELEM_END_FUNC) (void *, const char *);

/*
 * handle_xxxxxxxxx - XML element data content handle function
 *			function be called for element data content 
 *
 * return: 0 handling OK, non-zero if handling NOK and stop parsing
 * (data): user data
 * (s): content buffer
 * (len): length of buffer
 */
typedef int (*ELEM_DATA_FUNC) (void *, const char *, int);

typedef struct xml_element_def XML_ELEMENT_DEF;
struct xml_element_def
{
  const char *full_name;	/* fullname of element with spaces between parents */
  const int depth;		/* first level has depth 1 */
  ELEM_START_FUNC start_func;	/* element start function */
  ELEM_END_FUNC end_func;	/* element end function */
  ELEM_DATA_FUNC data_func;	/* element content handling function */
};

typedef struct xml_element XML_ELEMENT;
struct xml_element
{
  XML_ELEMENT_DEF *def;
  char short_name[MAX_ELEMENT_NAME];
  XML_ELEMENT *parent;		/* parent element */
  XML_ELEMENT *child;		/* first child element */
  XML_ELEMENT *next;		/* next element (same level) */
  XML_ELEMENT *prev;		/* next element (same level) */
  bool match;
};

#define START_FUNC(el) (el->def->start_func)
#define END_FUNC(el) (el->def->end_func)
#define DATA_FUNC(el) (el->def->data_func)

#define XML_USER_DATA(xml) (xml->ud)
#define XML_SET_USER_DATA(xml,u_data) (xml->ud = udata)

typedef struct xml_parser_data XML_PARSER_DATA;
struct xml_parser_data
{
  XML_Parser xml_parser;	/* XML parser (expat) */
  int depth;			/* current depth of parser */
  XML_ELEMENT *sc;		/* parse tree schema */
  XML_ELEMENT *ce;		/* current element (in schema) */
  int xml_error;		/* XML error code */
  int xml_error_line;		/* line with error */
  int xml_error_column;		/* column with error */
  char *buf;			/* file read parser buffer */
  bool verbose;			/* to print debug info */
  void *ud;			/* user data */
};

#ifdef __cplusplus
extern "C"
{
#endif

  XML_Parser xml_init_parser (void *data, const char *encoding,
			      XML_ELEMENT_DEF ** element_array,
			      const int count);
  void xml_destroy_parser (void *data);
  int xml_parse (void *data, FILE * fp, bool * is_finished);
  int xml_check_att_value (const char **attrs, const char *att_name,
			   const char *att_value);
  int xml_get_att_value (const char **attrs, const char *att_name,
			 char **p_att_value);

#ifdef __cplusplus
}

#endif				/* _XML_PARSER_H_ */

#endif				/* _LANGUAGE_SUPPORT_H_ */
