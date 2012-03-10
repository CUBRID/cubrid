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
 * xml_parser.c : XML Parser for CUBRID
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>


#include "xml_parser.h"

#if defined(WINDOWS)
#define strtok_r	strtok_s
#endif


/* size of XML parsing input buffer */
#define XML_READ_BUFFSIZE 8192

/* XML parse tree data */
#define XML_ROOT_DEPTH 0

#define IS_XML_ROOT(el) (el->def->depth == XML_ROOT_DEPTH)

typedef enum
{
  XML_INS_POS_UNDEF = 0,
  XML_INS_POS_AFTER,
  XML_INS_POS_BEFORE
} XML_INS_POS;

static XML_ELEMENT *xml_init_schema_tree (XML_ELEMENT_DEF ** element_array,
					  const int count);
static void xml_destroy_schema_tree (XML_ELEMENT * pt);

static XML_ELEMENT *create_xml_node (XML_ELEMENT_DEF * new_elem);
static int add_xml_element (XML_ELEMENT * xml_node,
			    XML_ELEMENT_DEF * new_elem_def);

static XML_ELEMENT *select_xml_branch_node (XML_ELEMENT * xml_node,
					    const char *sel_name);
static XML_ELEMENT *select_xml_node_for_ins (XML_ELEMENT * xml_node,
					     const char *sel_name,
					     XML_INS_POS * insert_pos);
static char *get_short_elem_name (const XML_ELEMENT_DEF * el_def,
				  const int level, char *short_name);

static int check_xml_elem_name (XML_ELEMENT * el, const char *check_el_name);

static void XMLCALL xml_header_validation_utf8 (void *userData,
						const XML_Char * version,
						const XML_Char * encoding,
						int standalone);
static void XMLCALL xml_elem_start (void *data, const char *parsed_el_name,
				    const char **attr);
static void XMLCALL xml_elem_end (void *data, const char *parsed_el_name);
static void XMLCALL xml_data_handler (void *data, const XML_Char * s,
				      int len);

/* XML root element */
XML_ELEMENT_DEF xml_elem_XML = { "XML", XML_ROOT_DEPTH, NULL, NULL, NULL };


/*
 * xml_init_schema_tree() - initializes the XML parsing schema 
 *   return: tree parse schema (root node)
 *   element_array(in): array of definition nodes (XML elements)
 *   count(in): count of nodes in the array
 *
 */
static XML_ELEMENT *
xml_init_schema_tree (XML_ELEMENT_DEF ** element_array, const int count)
{
  int i;
  XML_ELEMENT *xml_parse_tree = NULL;

  xml_parse_tree = malloc (sizeof (XML_ELEMENT));
  if (xml_parse_tree == NULL)
    {
      return NULL;
    }

  xml_parse_tree->def = &xml_elem_XML;
  xml_parse_tree->child = NULL;
  xml_parse_tree->next = NULL;
  xml_parse_tree->prev = NULL;
  xml_parse_tree->parent = NULL;
  strcpy (xml_parse_tree->short_name, xml_elem_XML.full_name);

  for (i = 0; i < count; i++)
    {
      if (add_xml_element (xml_parse_tree, element_array[i]) !=
	  XML_CUB_NO_ERROR)
	{
	  return NULL;
	}
    }

  return xml_parse_tree;
}

/*
 * xml_destroy_schema_tree() - frees the XML parsing schema 
 *   return:
 *   pt(in): tree parse schema (root node)
 */
static void
xml_destroy_schema_tree (XML_ELEMENT * pt)
{
  if (pt->next != NULL)
    {
      xml_destroy_schema_tree (pt->next);
      pt->next = NULL;
    }

  if (pt->child != NULL)
    {
      xml_destroy_schema_tree (pt->child);
      pt->child = NULL;
    }

  pt->def = NULL;
  pt->parent = NULL;
  pt->prev = NULL;
  assert (pt->short_name != '\0');
  free (pt);
}

/*
 * create_xml_node() - creates a new XML schema element based on definition
 *
 *   return: new element node
 *   el_def(in): element node definition
 */
static XML_ELEMENT *
create_xml_node (XML_ELEMENT_DEF * new_elem)
{
  XML_ELEMENT *xml_node = malloc (sizeof (XML_ELEMENT));

  if (xml_node == NULL)
    {
      return NULL;
    }

  xml_node->def = new_elem;
  xml_node->child = NULL;
  xml_node->next = NULL;
  xml_node->prev = NULL;
  xml_node->parent = NULL;
  xml_node->match = false;
  *(xml_node->short_name) = '\0';

  return xml_node;
}

/*
 * add_xml_element() - creates and element node based on its definition and
 *		       inserts it into the XML schema parser 
 *
 *   return: XML error code
 *   xml_node(in): tree parse schema (root node)
 *   new_elem_def(in): element definition
 */
static int
add_xml_element (XML_ELEMENT * xml_node, XML_ELEMENT_DEF * new_elem_def)
{
  char new_elem_short_name[MAX_ELEMENT_NAME];
  char new_elem_branch_name[MAX_ELEMENT_NAME];

  assert (xml_node != NULL);
  assert (new_elem_def != NULL);

  assert (xml_node->def != NULL);
  assert (new_elem_def->depth >= xml_node->def->depth);

  if (get_short_elem_name (new_elem_def, new_elem_def->depth,
			   new_elem_short_name) == NULL)
    {
      return XML_CUB_SCHEMA_BROKEN;
    }

  if (new_elem_def->depth == xml_node->def->depth)
    {
      /* same level */
      XML_ELEMENT *xml_new_node = NULL;
      XML_INS_POS insert_pos = XML_INS_POS_UNDEF;

      xml_node = select_xml_node_for_ins (xml_node, new_elem_short_name,
					  &insert_pos);

      assert (xml_node != NULL);

      /* insert new node here */
      xml_new_node = create_xml_node (new_elem_def);
      if (xml_new_node == NULL)
	{
	  return XML_CUB_OUT_OF_MEMORY;
	}

      assert (insert_pos == XML_INS_POS_AFTER ||
	      insert_pos == XML_INS_POS_BEFORE);

      strcpy (xml_new_node->short_name, new_elem_short_name);
      xml_new_node->parent = xml_node->parent;

      if (insert_pos == XML_INS_POS_AFTER)
	{
	  xml_new_node->next = xml_node->next;
	  if (xml_node->next != NULL)
	    {
	      xml_node->next->prev = xml_new_node;
	    }
	  xml_node->next = xml_new_node;
	  xml_new_node->prev = xml_node;
	}
      else
	{
	  if (xml_node->prev == NULL)
	    {
	      /* becomes new first child */
	      assert (xml_node->parent != NULL);
	      xml_node->parent->child = xml_new_node;
	      xml_new_node->next = xml_node;
	      xml_node->prev = xml_new_node;
	    }
	  else
	    {
	      /* insert before */
	      xml_new_node->prev = xml_node->prev;
	      xml_node->prev->next = xml_new_node;
	      xml_new_node->next = xml_node;
	      xml_node->prev = xml_new_node;
	    }
	}

      return XML_CUB_NO_ERROR;
    }

  assert (new_elem_def->depth > xml_node->def->depth);

  if (xml_node->child == NULL)
    {
      XML_ELEMENT *xml_new_node = NULL;

      /* cannot create children without parents */
      assert (new_elem_def->depth == xml_node->def->depth + 1);

      /* first child of this node, insert new node here */
      xml_new_node = create_xml_node (new_elem_def);

      if (xml_new_node == NULL)
	{
	  return XML_CUB_OUT_OF_MEMORY;
	}

      xml_new_node->parent = xml_node;
      xml_node->child = xml_new_node;
      strcpy (xml_new_node->short_name, new_elem_short_name);

      return XML_CUB_NO_ERROR;
    }

  assert (xml_node->child != NULL);

  /* check that select branch is ok up to this point before next selection */
  if (!IS_XML_ROOT (xml_node))
    {
      int el_order = 0;

      if (get_short_elem_name (new_elem_def, xml_node->def->depth,
			       new_elem_branch_name) == NULL)
	{
	  return XML_CUB_SCHEMA_BROKEN;
	}

      el_order = check_xml_elem_name (xml_node, new_elem_branch_name);

      assert (el_order == 0);

      if (el_order != 0)
	{
	  return XML_CUB_SCHEMA_BROKEN;
	}
    }

  /* move to child */
  xml_node = xml_node->child;

  if (xml_node->def->depth == new_elem_def->depth)
    {
      /* add on same level as child */
      return add_xml_element (xml_node, new_elem_def);
    }

  /* go down the branches */
  while (xml_node->def->depth < new_elem_def->depth)
    {
      /* select branch at this level */
      if (get_short_elem_name (new_elem_def, xml_node->def->depth,
			       new_elem_branch_name) == NULL)
	{
	  return XML_CUB_SCHEMA_BROKEN;
	}

      xml_node = select_xml_branch_node (xml_node, new_elem_branch_name);

      if (xml_node->child == NULL)
	{
	  break;
	}

      xml_node = xml_node->child;
    }
  assert (xml_node != NULL);

  return add_xml_element (xml_node, new_elem_def);

}

/*
 * select_xml_branch_node() - selects the node branch coresponding to the name
 *			      within the same level
 *
 *   return: XML node or NULL if branch not found
 *   xml_node(in): first node of level to start from
 *   sel_name(in): branch name
 */
static XML_ELEMENT *
select_xml_branch_node (XML_ELEMENT * xml_node, const char *sel_name)
{
  assert (xml_node != NULL);

  for (;;)
    {
      int el_order = check_xml_elem_name (xml_node, sel_name);

      if (el_order < 0)
	{
	  if (xml_node->next != NULL)
	    {
	      /* position after : continue search */
	      xml_node = xml_node->next;
	      continue;
	    }
	  else
	    {
	      /* no more branches */
	      assert (false);
	      return NULL;
	    }
	}
      else if (el_order > 0)
	{
	  /* branch non existent */
	  assert (false);
	  return NULL;
	}
      else
	{
	  /* found */
	  assert (el_order == 0);
	  return xml_node;
	}
    }

  assert (false);
  return NULL;
}

/*
 * select_xml_node_for_ins() - selects the node from which to insert a new
 *			       element node
 *
 *   return: XML node or NULL if a node with the required name already exists
 *   xml_node(in): first node of level to start from
 *   sel_name(in): short name of new node
 *   insert_pos(out): where to insert
 */
static XML_ELEMENT *
select_xml_node_for_ins (XML_ELEMENT * xml_node, const char *sel_name,
			 XML_INS_POS * insert_pos)
{
  assert (xml_node != NULL);

  for (;;)
    {
      int el_order = check_xml_elem_name (xml_node, sel_name);

      if (el_order < 0)
	{
	  if (xml_node->next != NULL)
	    {
	      /* position after : continue search */
	      xml_node = xml_node->next;
	      continue;
	    }
	  else
	    {
	      *insert_pos = XML_INS_POS_AFTER;
	      return xml_node;
	    }
	}
      else if (el_order > 0)
	{
	  *insert_pos = XML_INS_POS_BEFORE;
	  return xml_node;
	}
      else
	{
	  assert (el_order == 0);
	  /* node alreay existing */
	  return NULL;
	}
    }

  assert (false);
  return NULL;
}

/*
 * get_short_elem_name() - returns the short name of a element definition
 *
 *   return: short name or NULL if required level exceeds the node depth
 *   el_def(in): element node definition
 *   level(in): level for which the short name is requested
 *   short_name(out): short name
 */
static char *
get_short_elem_name (const XML_ELEMENT_DEF * el_def, const int level,
		     char *short_name)
{
  char *t = NULL;
  char *s = NULL;
  char *q = NULL;
  char tokenized[MAX_ELEMENT_FULL_NAME];
  int l = 1;

  assert (short_name != NULL);
  assert (el_def != NULL);
  assert (el_def->full_name != NULL);


  assert (strlen (el_def->full_name) < MAX_ELEMENT_FULL_NAME);

  strcpy (tokenized, el_def->full_name);
  t = strtok_r (tokenized, " ", &q);
  while (l < level && t != NULL)
    {
      t = strtok_r (NULL, " ", &q);
      l++;
    }

  return (t != NULL) ? strcpy (short_name, t) : NULL;
}

/*
 * check_xml_elem_name() - checks the order between an XML element node name
 *			   and a a string
 *
 *   return: 0 if same name, < 0 if requested name is after element name,
 *	     > 0 if requested name is before element name
 *   el(in): element node
 *   check_el_name(in): name to be checked
 */
static int
check_xml_elem_name (XML_ELEMENT * el, const char *check_el_name)
{
  assert (el != NULL);
  assert (check_el_name != NULL);

  assert (el->short_name != NULL);

  return strcmp (el->short_name, check_el_name);
}

/*
 * xml_header_validation_utf8() - XML header validation function;
 *				  expat callback function
 *
 *   return: 
 *   userData(in): user data
 *   version(in):
 *   encoding(in):
 *   standalone(in):
 */
static void XMLCALL
xml_header_validation_utf8 (void *userData, const XML_Char * version,
			    const XML_Char * encoding, int standalone)
{
  if (encoding == NULL || strcmp ((char *) encoding, "UTF-8"))
    {
      XML_PARSER_DATA *pd;

      assert (userData != NULL);

      pd = (XML_PARSER_DATA *) userData;

      pd->xml_error = XML_CUB_ERR_HEADER_ENCODING;
      pd->xml_error_line = XML_GetCurrentLineNumber (pd->xml_parser);
      pd->xml_error_column = XML_GetCurrentColumnNumber (pd->xml_parser);

      XML_StopParser (pd->xml_parser, XML_FALSE);
    }
}

/*
 * xml_elem_start() - XML element start function;
 *		      expat callback function
 *
 *   return: 
 *   data(in): user data
 *   parsed_el_name(in): element name
 *   attr(in): array of pairs for XML attribute and value (strings) of current
 *	       element
 */
static void XMLCALL
xml_elem_start (void *data, const char *parsed_el_name, const char **attr)
{
  int el_order;
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  XML_ELEMENT *el = NULL;
  bool found = false;

  assert (pd != NULL);
  el = pd->ce;

  if (IS_XML_ROOT (el))
    {
      el = el->child;
    }

  assert (el != NULL);
  assert (el->def != NULL);

  pd->depth++;
  assert (pd->depth >= el->def->depth);

  if (pd->depth - el->def->depth > 1)
    {
      /* parser is too deep for our schema, ignore element */
      return;
    }
  else if (pd->depth - el->def->depth == 1)
    {
      /* search elements of next level */
      el = el->child;
    }
  else
    {
      /* same level */
      assert (pd->depth == el->def->depth);
    }

  for (; el != NULL; el = el->next)
    {
      el_order = check_xml_elem_name (el, parsed_el_name);

      if (el_order == 0)
	{
	  /* found element name */
	  found = true;
	  break;
	}

      if (el_order > 0)
	{
	  /* currently parsed element sorts after last schema element */
	  break;
	}
    }

  if (found)
    {
      int elem_start_res = 0;
      int handle_res = 0;
      ELEM_START_FUNC start_func = START_FUNC (el);
      ELEM_DATA_FUNC data_func = DATA_FUNC (el);
      XML_ELEMENT *saved_el = NULL;

      assert (el != NULL);
      assert (el->def != NULL);

      /* set current element (required for verbose option) */
      saved_el = pd->ce;
      pd->ce = el;

      /* found element */
      if (start_func != NULL)
	{
	  elem_start_res = (*start_func) (pd, attr);
	}

      if (elem_start_res > 0)
	{
	  /* ignore element */
	  assert (el->match == false);
	  /* restore initial current element */
	  pd->ce = saved_el;
	  return;
	}
      else if (elem_start_res < 0)
	{
	  if (pd->xml_error == XML_CUB_NO_ERROR)
	    {
	      pd->xml_error = XML_CUB_ERR_PARSER;
	    }
	  pd->xml_error_line = XML_GetCurrentLineNumber (pd->xml_parser);
	  pd->xml_error_column = XML_GetCurrentColumnNumber (pd->xml_parser);

	  XML_StopParser (pd->xml_parser, XML_FALSE);
	  /* restore initial current element */
	  pd->ce = saved_el;
	  return;
	}

      assert (elem_start_res == 0);

      /* new schema element is checked */
      assert (pd->ce == el);
      el->match = true;

      /* enable data handler */
      if (data_func != NULL)
	{
	  XML_SetCharacterDataHandler (pd->xml_parser, xml_data_handler);
	}
    }
  else
    {
      /* only first level unknown elements are ignored */
      if (!IS_XML_ROOT (pd->ce))
	{
	  assert (pd->ce->match == true);
	}
    }
}

/*
 * xml_elem_end() - XML element end function; expat callback function
 *
 *   return: 
 *   data(in): user data
 *   parsed_el_name(in): element name
 */
static void XMLCALL
xml_elem_end (void *data, const char *parsed_el_name)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  ELEM_END_FUNC end_func;
  int end_res = 0;

  assert (pd != NULL);

  /* disable data handler */
  XML_SetCharacterDataHandler (pd->xml_parser, NULL);

  if (pd->depth > pd->ce->def->depth || !pd->ce->match)
    {
      pd->depth--;
      return;
    }

  pd->depth--;

  end_func = END_FUNC (pd->ce);

  if (end_func != NULL)
    {
      assert (parsed_el_name != NULL);
      end_res = (*end_func) (data, parsed_el_name);
    }

  if (end_res != 0)
    {
      if (pd->xml_error == XML_CUB_NO_ERROR)
	{
	  pd->xml_error = XML_CUB_ERR_PARSER;
	}
      pd->xml_error_line = XML_GetCurrentLineNumber (pd->xml_parser);
      pd->xml_error_column = XML_GetCurrentColumnNumber (pd->xml_parser);

      XML_StopParser (pd->xml_parser, XML_FALSE);
    }

  assert (pd->ce->match == true);
  /* move level up in schema */
  pd->ce->match = false;
  pd->ce = pd->ce->parent;
}

/*
 * xml_data_handler() - XML element element content handling function;
 *			expat callback function
 *
 *   return: 
 *   data(in): user data
 *   s(in): content buffer
 *   len(in): length (in XML_Char) of content buffer
 *
 *  Note : if encoding is UTF-8, the unit XML_Char is byte
 */
static void XMLCALL
xml_data_handler (void *data, const XML_Char * s, int len)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  ELEM_DATA_FUNC data_func;
  int data_res = -1;

  assert (pd != NULL);

  assert (pd->ce != NULL);
  assert (pd->ce->def != NULL);
  assert (pd->ce->match == true);

  data_func = DATA_FUNC (pd->ce);

  assert (data_func != NULL);

  /* handle data */
  if (data_func != NULL)
    {
      data_res = (*data_func) (data, (const char *) s, len);
    }

  if (data_res != 0)
    {
      if (pd->xml_error == XML_CUB_NO_ERROR)
	{
	  pd->xml_error = XML_CUB_ERR_PARSER;
	}
      pd->xml_error_line = XML_GetCurrentLineNumber (pd->xml_parser);
      pd->xml_error_column = XML_GetCurrentColumnNumber (pd->xml_parser);

      XML_StopParser (pd->xml_parser, XML_FALSE);
    }
}

/* XML interface functions */
/*
 * xml_init_parser() - XML parser initializer
 *
 *   return: pointer to expat XML parser
 *   data(in): XML parser data
 *   encoding(in): encoding charset
 *   element_array(in): array of element definition nodes (schema definition)
 *   count(in): number of elements in schema definition
 */
XML_Parser
xml_init_parser (void *data, const char *encoding,
		 XML_ELEMENT_DEF ** element_array, const int count)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  XML_Parser p = NULL;

  assert (pd != NULL);

  p = XML_ParserCreate (encoding);

  if (p == NULL)
    {
      return NULL;
    }

  pd->xml_parser = p;
  pd->xml_error = XML_CUB_NO_ERROR;
  pd->xml_error_line = -1;
  pd->xml_error_column = -1;

  XML_SetUserData (p, pd);
  /* XML parser : callbacks */
  XML_SetElementHandler (p, xml_elem_start, xml_elem_end);

  /* use built-in header validation */
  if (strcmp (encoding, "UTF-8") == 0)
    {
      XML_SetXmlDeclHandler (p, xml_header_validation_utf8);
    }

  /* input buffer */
  pd->buf = malloc (XML_READ_BUFFSIZE);
  if (pd->buf == NULL)
    {
      return NULL;
    }

  pd->sc = xml_init_schema_tree (element_array, count);

  if (pd->sc == NULL)
    {
      XML_ParserFree (p);
      return NULL;
    }

  pd->ce = pd->sc;
  pd->ce->match = false;
  pd->depth = 0;

  return p;
}

/*
 * xml_destroy_parser() - frees XML parser
 *
 *   return:
 *   data(in): XML parser data
 */
void
xml_destroy_parser (void *data)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;

  assert (pd != NULL);
  assert (pd->xml_parser != NULL);

  XML_ParserFree (pd->xml_parser);
  pd->xml_parser = NULL;

  xml_destroy_schema_tree (pd->sc);
  pd->sc = NULL;
  pd->ce = NULL;
  pd->depth = 0;

  if (pd->buf != NULL)
    {
      free (pd->buf);
      pd->buf = NULL;
    }

  /* used data is not freed */
  pd->ud = NULL;
}

/*
 * xml_destroy_parser() - frees XML parser
 *
 *   return: error code
 *   data(in): XML parser data
 *   fp(in): file to read from
 *   is_finished(out): true if parser has finished
 */
int
xml_parse (void *data, FILE * fp, bool * is_finished)
{
  XML_PARSER_DATA *pd = (XML_PARSER_DATA *) data;
  int len;
  int isFinal;
  XML_ParsingStatus xml_status;

  assert (pd != NULL);
  assert (pd->xml_parser != NULL);

  assert (fp != NULL);
  assert (is_finished != NULL);

  len = (int) fread (pd->buf, 1, XML_READ_BUFFSIZE, fp);
  if (ferror (fp))
    {
      return XML_CUB_ERR_FILE_READ;
    }

  isFinal = (feof (fp) != 0) ? 1 : 0;

  XML_GetParsingStatus (pd->xml_parser, &xml_status);
  if (xml_status.parsing == XML_FINISHED)
    {
      *is_finished = true;
      return XML_CUB_NO_ERROR;
    }

  if (XML_Parse (pd->xml_parser, pd->buf, len, isFinal) == XML_STATUS_ERROR)
    {
      pd->xml_error = XML_CUB_ERR_PARSER;
      pd->xml_error_line = XML_GetCurrentLineNumber (pd->xml_parser);
      pd->xml_error_column = XML_GetCurrentColumnNumber (pd->xml_parser);
      return XML_CUB_ERR_PARSER;
    }

  return XML_CUB_NO_ERROR;
}

/*
 * xml_check_att_value() - checks the attribute value
 *
 *   return: 0 if attribute value matches, 1 otherwise (or attribute not found)
 *   attrs(in): the array of attribute name/value pairs of current XML element
 *   att_name(in):
 *   att_value(in):
 */
int
xml_check_att_value (const char **attrs, const char *att_name,
		     const char *att_value)
{
  const char **curr_att = attrs;

  assert (attrs != NULL);
  assert (att_name != NULL);
  assert (att_value != NULL);

  for (; *curr_att != NULL; curr_att++, curr_att++)
    {
      if (strcmp (curr_att[0], att_name) == 0 &&
	  strcmp (curr_att[1], att_value) == 0)
	{
	  return 0;
	}
    }

  return 1;
}

/*
 * xml_get_att_value() - returns the attribute value
 *
 *   return: 0 if attribute was found, 1 otherwise
 *   attrs(in): the array of attribute name/value pairs of current XML element
 *   att_name(in):
 *   p_att_value(in/out): (returned attribute value)
 */
int
xml_get_att_value (const char **attrs, const char *att_name,
		   char **p_att_value)
{
  const char **curr_att = attrs;

  assert (attrs != NULL);
  assert (att_name != NULL);
  assert (p_att_value != NULL);
  assert (*p_att_value == NULL);

  for (; *curr_att != NULL; curr_att++, curr_att++)
    {
      if (strcmp (curr_att[0], att_name) == 0)
	{
	  *p_att_value = (char *) curr_att[1];
	  return 0;
	}
    }

  return 1;
}
