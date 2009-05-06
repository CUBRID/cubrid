/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */

#include		<stdio.h>
#include		<stdlib.h>
#include		<string.h>

#include		"odbc_portable.h"
#include		"odbc_util.h"

#define		UNIT_MEMORY_SIZE		256
#define		STK_SIZE        100

PRIVATE int str_eval_like (const unsigned char *tar,
			   const unsigned char *expr, unsigned char escape);
static int is_korean (unsigned char ch);

/************************************************************************
* name:  InitStr
* arguments: 
*	D_STRING str;
* returns/side-effects: 
*	
* description: 
* NOTE: 
************************************************************************/
PUBLIC void
InitStr (D_STRING * str)
{
  str->value = NULL;
  str->usedSize = 0;
  str->totalSize = 0;
}


/************************************************************************
* name: ReallocImproved
* arguments: 
*	dest - destination memory pointer
*	destSize - total size of dest
*	usedSize - used memory size of dest
*	allocSize - realloc size 
* returns/side-effects: 
*	destSize increased
* description: 
*	if remain size(destSize - usedSize)  < alloc size, 
*	realloc a bunch of bytes  
* NOTE: 
************************************************************************/
PUBLIC ERR_CODE
ReallocImproved (char **dest, int *destSize, int usedSize, int allocSize)
{
  int remainDestSize;

  remainDestSize = *destSize - usedSize;

  /* MUST reserve one byte for extra */
  while (remainDestSize <= allocSize)
    {
      if (*dest == NULL)
	{
	  *dest = UT_ALLOC (UNIT_MEMORY_SIZE);
	  if (*dest == NULL)
	    {
	      *destSize = 0;
	      return -1;
	    }
	  memset (*dest, 0, UNIT_MEMORY_SIZE);
	  *destSize = UNIT_MEMORY_SIZE;
	  remainDestSize = UNIT_MEMORY_SIZE;

	}
      else
	{
	  *dest = (char *) UT_REALLOC (*dest, *destSize + UNIT_MEMORY_SIZE);
	  if (*dest == NULL)
	    {
	      *destSize = 0;
	      return -1;
	    }
	  memset (*dest + *destSize, 0, UNIT_MEMORY_SIZE);

	  *destSize += UNIT_MEMORY_SIZE;
	  remainDestSize += UNIT_MEMORY_SIZE;
	}
    }
  return 0;
}

/************************************************************************
* name:  StrcatImproved
* arguments: 
*	dest - destination string structure
*	  dest.value - character pointer
*	  dest.destSize - in dest memory, total memory size
*	  dest.usedSize - in dest memory, used memory size(strlen), 
*			remain size = destSize - usedSize
*	source - source string start pointer
* returns/side-effects: 
*	OK ok, ERROR error;
*	destSize increased(in RealloImproved)  and usedSize increased
* description: 
*	first malloc( UNIT_MEMORY_SIZE at once ), and then concat string
*						   ~~~~~~~~~~~~~
* NOTE: 
*	For using this function,
*	caller initialize dest to null, destSize to zero, usedSize to zero
*	At least, dest to null -> InitStr()
************************************************************************/
PUBLIC ERR_CODE
StrcatImproved (D_STRING * dest, char *source)
{
  int allocSize;

  allocSize = strlen (source);
  if (dest->value == NULL)
    {
      dest->totalSize = 0;
      dest->usedSize = 0;
    }
  if (ReallocImproved (&(dest->value), &(dest->totalSize), dest->usedSize,
		       allocSize) < 0)
    {
      return -1;
    }
  strcat (dest->value, source);

  dest->usedSize += allocSize;

  return 0;
}

/************************************************************************
* name:  MemcatImproved
* arguments: 
*	dest - destination pointer
*	usedSize - total alloc size = used size + remain size
*	src  - source memory pointer,
*	srcSize - source memory size
* returns/side-effects: 
*	OK, ERROR
* description: 
* 	Not string copy. 
*	So this can copy memory stream including null char(\0)
* NOTE: 
*	Using this function, 
*	initialize dest to null, usedSize to zero, remainSize to zero
*	At least, dest to NULL -> InisStr()
************************************************************************/
PUBLIC ERR_CODE
MemcatImproved (D_STRING * dest, char *src, int srcSize)
{

  if (dest->value == NULL)
    {
      dest->totalSize = 0;
      dest->usedSize = 0;
    }
  if (ReallocImproved (&(dest->value), &(dest->totalSize),
		       dest->usedSize, srcSize) < 0)
    {
      return -1;
    }
  memcpy (dest->value + dest->usedSize, src, srcSize);

  dest->usedSize += srcSize;

  return 0;
}

PUBLIC ERR_CODE
MemcpyImproved (D_STRING * dest, char *src, int srcSize)
{

  if (dest->value == NULL)
    {
      dest->totalSize = 0;
      dest->usedSize = 0;
    }
  else
    {
      dest->usedSize = 0;
    }

  if (ReallocImproved (&(dest->value), &(dest->totalSize),
		       dest->usedSize, srcSize) < 0)
    {
      return -1;
    }
  memcpy (dest->value + dest->usedSize, src, srcSize);

  dest->usedSize += srcSize;

  return 0;
}

/************************************************************************
* name:  FreeStr
* arguments: 
*	node of D_STRING
* returns/side-effects: 
* description: 
* NOTE: 
************************************************************************/
PUBLIC void
FreeStr (D_STRING * str)
{
  if (str->value != NULL)
    {
      UT_FREE (str->value);
    }
  str->value = NULL;
  str->usedSize = 0;
  str->totalSize = 0;
}



/************************************************************************
* name:  ConcatPath
* arguments: 
*	prePath
*	postPath
*	resultPath
* returns/side-effects: 
* description: 
* NOTE: 
************************************************************************/
PUBLIC void
ConcatPath (char *prePath, char *postPath, char *resultPath)
{
  if (prePath[strlen (prePath) - 1] == '/')
    {
      strncpy (resultPath, prePath, strlen (prePath) - 1);
      resultPath[strlen (prePath) - 1] = '\0';
    }
  else
    {
      strcpy (resultPath, prePath);
    }

  strcat (resultPath, "/");

  if (postPath[0] == '/')
    {
      strcat (resultPath, postPath + 1);
    }
  else
    {
      strcat (resultPath, postPath);
    }
}

/*-----------------------------------------------------------------------
 *					char util
 *	true  - return 1
 *	false - return 0
 *	or char
 *----------------------------------------------------------------------*/

PUBLIC short
char_islower (int c)
{
  return ((c) >= 'a' && (c) <= 'z');
}

PUBLIC short
char_isupper (int c)
{
  return ((c) >= 'A' && (c) <= 'Z');
}

PUBLIC short
char_isalpha (int c)
{
  return (char_islower ((c)) || char_isupper ((c)));
}

PUBLIC short
char_isdigit (int c)
{
  return ((c) >= '0' && (c) <= '9');
}

PUBLIC short
char_isxdigit (int c)
{
  return (char_isdigit ((c)) || ((c) >= 'a' && (c) <= 'f')
	  || ((c) >= 'A' && (c) <= 'F'));
}

PUBLIC short
char_isalnum (int c)
{
  return (char_isalpha ((c)) || char_isdigit ((c)));
}

PUBLIC short
char_isspace (int c)
{
  return ((c) == ' ' || (c) == '\t' || (c) == '\r' || (c) == '\n');
}

PUBLIC short
char_isascii (int c)
{
  return ((c) >= 1 && (c) <= 127);
}

// return char
PUBLIC short
char_tolower (int c)
{
  return (char_isupper ((c)) ? ((c) - ('A' - 'a')) : (c));
}

PUBLIC short
char_toupper (int c)
{
  return (char_islower ((c)) ? ((c) + ('A' - 'a')) : (c));
}

/************************************************************************
* name:  IsAlphNumeric
* arguments: 
*	decimal number of ascii charcter 
* returns/side-effects: 
* description: 
*	48 ~ 57 : numeric
*	65 ~ 90 : Upper char
*	97 ~ 122: lower char
* NOTE: 
************************************************************************/
PUBLIC _BOOL_
IsAlphaNumeric (int num)
{
  if ((48 <= num && num <= 57) ||
      (65 <= num && num <= 90) || (97 <= num && num <= 122))
    {
      return _TRUE_;
    }
  else
    {
      return _FALSE_;
    }
}


/************************************************************************
* name:  long_to_byte
* arguments:
* returns/side-effects:
* description:
*    long int를 byte value로 바꿔준다.  htonl와 비슷한 역할을 하지만,
*    os에 independent한 value를 형성하기
*    위해서 만들었다.
* NOTE:
************************************************************************/
PUBLIC void
long_to_byte (long value, unsigned char *bytes, int length)
{
  long share;
  short i;

  memset (bytes, 0, length);

  share = value;

  for (i = 0; share != 0 && i < length; ++i)
    {
      bytes[i] = share % 256;
      share /= 256;
    }
}


/************************************************************************
* name:  byte_to_long
* arguments:
* returns/side-effects:
* description:
*    4byte value를 long int로 바꿔준다.  htonl와 비슷한 역할을 하지만,
*    4 byte로 제약하고 싶었으며, os에 independent한 value를 형성하기
*    위해서 만들었다.
* NOTE:
************************************************************************/
PUBLIC void
byte_to_long (unsigned char *bytes, int length, long *value)
{
  int i;

  for (i = (length - 1), *value = 0; i >= 0; --i)
    {
      *value = (*value) * 256 + (int) bytes[i];
    }
}

/************************************************************************
* name:  bincpy
* arguments: 
*	D_BINARY dest - destination
*	src - source binary
*	size - source size
* returns/side-effects: 
* description: 
* NOTE: 
************************************************************************/
PUBLIC ERR_CODE
bincpy (D_BINARY * dest, char *src, int size)
{
  if (src == NULL || size < 0)
    {
      dest->value = NULL;
      dest->size = 0;
      return 0;
    }

  dest->value = (char *) UT_ALLOC (size);
  if (dest->value == NULL)
    {
      dest->size = -1;
      return -1;
    }

  memcpy (dest->value, src, size);
  dest->size = size;
  return -1;
}


/************************************************************************
* name: binfree
* arguments: 
*	src - source
* returns/side-effects: 
* description: 
* NOTE: 
************************************************************************/
PUBLIC ERR_CODE
binfree (D_BINARY * src)
{
  UT_FREE (src->value);
  src->size = -1;
  return 0;
}



/************************************************************************
* name: ut_alloc
* arguments:
*		int size - size of allocated memory
* returns/side-effects:
*		void* - memory pointer
* description:
* NOTE:
************************************************************************/
PUBLIC void *
ut_alloc (SQLLEN size)
{
  char *new = NULL;

  new = malloc (size);

#ifdef _MEM_DEBUG
  if (new == NULL)
    {
      printf ("Alloc : NULL\n");
    }
  else
    {
      printf ("Alloc : %p\n", new);
    }
#endif

  return new;
}

/************************************************************************
* name: ut_free
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC void
ut_free (void *ptr)
{
#ifdef _MEM_DEBUG
  if (ptr == NULL)
    {
      printf ("Free : NULL\n");
    }
  else
    {
      printf ("Free : %p\n", ptr);
    }
#endif
  if (ptr != NULL)
    free (ptr);
}

/************************************************************************
* name: ut_realloc
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC void *
ut_realloc (void *ptr, int size)
{
  void *new = NULL;

  new = realloc (ptr, size);

#ifdef _DEBUG
  if (new == NULL)
    {
      printf ("Realloc : NULL\n");
    }
  else
    {
      printf ("Realloc : %p\n", new);
    }
#endif

  return new;
}


/************************************************************************
* name: ut_make_string
* arguments:
*		const char *src - source string ( not always null-terminated string)
*		length -
*			if length < 0, copy all src(null-term).
*			else copy length characters and null-term.
* returns/side-effects:
*		char * - string pointer
* description:
* NOTE:
************************************************************************/
PUBLIC char *
ut_make_string (const char *src, int length)
{
  char *temp = NULL;

  if (src != NULL)
    {
      if (length < 0)
	{
	  temp = UT_ALLOC (strlen (src) + 1);
	  strcpy (temp, src);
	}
      else
	{
	  temp = UT_ALLOC (length + 1);
	  strncpy (temp, src, length);
	  temp[length] = '\0';
	}
    }

  return temp;
}

/************************************************************************
* name: ut_append_string
* arguments:
*		const char *src - source string ( not always null-terminated string)
*		length -
*			if length < 0, copy all src(null-term).
*			else copy length characters and null-term.
* returns/side-effects:
*		char * - string pointer
* description:
* NOTE:
************************************************************************/
PUBLIC char *
ut_append_string (char *str1, char *str2, int len2)
{
  char *temp = NULL;
  int str_size;

  if (str1 != NULL)
    {
      if (len2 < 0)
	{
	  temp = UT_REALLOC (str1, strlen (str1) + strlen (str2) + 1);
	  strcat (temp, str2);
	}
      else
	{
	  str_size = strlen (str1) + len2;
	  temp = UT_REALLOC (str1, str_size + 1);
	  strncpy (temp + strlen (str1), str2, len2);
	  temp[str_size] = '\0';
	}
    }
  else
    {
      temp = UT_MAKE_STRING (str2, len2);
    }

  return temp;
}

/************************************************************************
* name: ut_make_binary
* arguments:
*		const char *src - source binary
*		length -
*			if length < 0, NULL
*			else copy length binary
* returns/side-effects:
*		char * - string pointer
* description:
* NOTE:
************************************************************************/
PUBLIC char *
ut_make_binary (const char *src, int length)
{
  char *temp = NULL;

  if (src != NULL && length > 0)
    {
      temp = UT_ALLOC (length);
      memcpy (temp, src, length);
    }

  return temp;
}

/************************************************************************
* name: add_element_to_setstring
* arguments:
* NOTE:
************************************************************************/
PUBLIC void
add_element_to_setstring (char *setstring, char *element)
{
  if (setstring == NULL || element == NULL)
    {
      return;
    }

  if (setstring[0] == '\0')
    {				// empty set string
      strcpy (setstring, element);
    }
  else
    {
      strcat (setstring, UT_SET_DELIMITER);
      strcat (setstring, element);
    }
}



/************************************************************************
* name: element_from_setstring
* arguments:
* returns/side-effects:
*       0 false - no more data, 1 ture - value ok
* description:
*       setstring means set delimited by DELIMITER
*       eg) "1;;2;;5"
* NOTE:
************************************************************************/
PUBLIC int
element_from_setstring (char **current, char *buf)
{
  char *pt;

  if (*current == NULL || **current == '\0')
    {
      buf[0] = '\0';
      return 0;
    }


  pt = strstr (*current, UT_SET_DELIMITER);
  if (pt != NULL)
    {
      strncpy (buf, *current, pt - *current + 1);
      buf[pt - *current] = '\0';
      *current = pt + strlen (UT_SET_DELIMITER);
    }
  else
    {
      strcpy (buf, *current);
      *current = *current + strlen (*current);
    }


  return 1;
}


/************************************************************************
* name: size_from_setstring
* arguments:
* returns/side-effects:
* description:
* NOTE:
************************************************************************/
PUBLIC int
size_from_setstring (char *setstring)
{
  char *pt;
  int i;

  if (setstring[0] == '\0')
    return 0;

  pt = setstring;
  for (i = 1;; ++i)
    {
      pt = strstr (pt, UT_SET_DELIMITER);
      if (pt == NULL)
	{
	  return i;
	}
      else
	{
	  pt += strlen (UT_SET_DELIMITER);
	}
    }
}


/************************************************************************
* name: 
* arguments: 
* returns/side-effects: 
* description: 
* NOTE: 
************************************************************************/
PUBLIC void
get_value_from_connect_str (char *szConnStrIn,
			    char *value, int size, char *keyword)
{
  char *pt1, *pt2;
  char buf[256];

  sprintf (buf, "%s=", keyword);

  pt1 = szConnStrIn;
  while (1)
    {
      pt1 = strstr (pt1, buf);
      if (pt1 == NULL || pt1 == szConnStrIn || *(pt1 - 1) == ';')
	break;
      else
	{
	  pt1 += strlen (buf);
	}
    }
  if (pt1 != NULL)
    {
      pt1 += strlen (buf);
      pt2 = strchr (pt1, ';');
      if (pt2 != NULL)
	{
	  strncpy (value, pt1, pt2 - pt1);
	  value[pt2 - pt1] = '\0';
	}
      else
	{
	  strcpy (value, pt1);
	}
    }
  else
    {
      value[0] = '\0';
    }
}


/*-----------------------------------------------------------------------
 *							Linked List
 *----------------------------------------------------------------------*/
/************************************************************************
 * name:        ListHeadAdd - append a node to a designated list 	*
 *                                                                      *
 * arguments:   head    - the list header 				*
 *              key - the key of the node				*
 *              val - the value of the node				*
 *              assignFunc - a function for assigning key and value	*
 *                                                                      *
 * returns/side-effects:                                                *
 * 	OK(0), ERROR(-1) if malloc error or assign error		*
 *                                                                      *
 * description: add new node to the list head. 				*
 *                                                                      *
 * NOTE:                                                                *
 *  Since the type of 'key' and 'value' of the node is 'void*', 	*
 *  it is only possible to assign value which size is same as void ptr. *
 *  For example, if string is copied to 'key', you must allocate memory	*
 *  in assigning function.						*
 ************************************************************************/

PUBLIC ERR_CODE
ListHeadAdd (ST_LIST * head, void *key, void *val,
	     ERR_CODE (*assignFunc) (ST_LIST *, void *, void *))
{
  ST_LIST *newNode;
  ERR_CODE retval;

  newNode = (ST_LIST *) UT_ALLOC (sizeof (ST_LIST));
  if (newNode == NULL)
    return -1;

  retval = (*assignFunc) (newNode, (void *) key, (void *) val);
  if (retval < 0)
    {
      UT_FREE (newNode);
      return -1;
    }
  newNode->next = head->next;
  head->next = newNode;

  return 0;
}

PUBLIC ERR_CODE
ListTailAdd (ST_LIST * head, void *key, void *val,
	     ERR_CODE (*assignFunc) (ST_LIST *, void *, void *))
{
  ST_LIST *newNode;
  ST_LIST *temp;
  ERR_CODE retval;

  newNode = (ST_LIST *) UT_ALLOC (sizeof (ST_LIST));
  if (newNode == NULL)
    return -1;

  retval = (*assignFunc) (newNode, (void *) key, (void *) val);
  if (retval < 0)
    {
      UT_FREE (newNode);
      return -1;
    }

  /* find tail node */
  for (temp = head;; temp = temp->next)
    {
      if (temp->next == NULL)
	break;
    }

  temp->next = newNode;
  newNode->next = NULL;

  return 0;
}

/************************************************************************
 * name:        ListFind 						*
 *                                                                      *
 * arguments:   head - list header 					*
 *              key - key value for list search				*
 *              cmpFunc - comparing function 				*
 *                                                                      *
 * returns/side-effects:                                                *
 *              node ptr if exist.					*
 *		NULL ptr otherwise.					*
 *                                                                      *
 * description: search list vlaue  with key 				*
 *									*
 * NOTE:                                                                *
 ************************************************************************/

PUBLIC void *
ListFind (ST_LIST * head, void *key, ERR_CODE (*cmpFunc) (void *, void *))
{
  ST_LIST *temp;
  ERR_CODE retval;

  for (temp = head->next; temp != NULL; temp = temp->next)
    {
      if ((retval = (*cmpFunc) (temp->key, key)) == 0)
	return temp->value;
    }
  return NULL;
}

/************************************************************************
 * name:        ListDeleteNode - delete a node			*
 *                                                                      *
 * arguments:   head	- list header					*
 *              key     - key value 					*
 *		cmpFunc - comparing function				*
 *		nodeDelete - 'key', 'value' deallocation function	*
 *                                                                      *
 * returns/side-effects:                                                *
 *	OK - delete a node, ERROR - delete none node			*
 *                                                                      *
 * description: delete one node from list.				*
 *                                                                      *
 * NOTE:        							*
 *  If 'key' or 'value' has its own memory, the memory must be freed	*
 *  in 'nodeDelete' function. But, the node container is freed in	*
 *  this module.							*
 ************************************************************************/

PUBLIC ERR_CODE
ListDeleteNode (ST_LIST * head, void *key,
		ERR_CODE (*cmpFunc) (void *, void *),
		void (*nodeDelete) (ST_LIST *))
{
  ST_LIST *temp;
  ST_LIST *del;

  for (temp = head; temp->next != NULL; temp = temp->next)
    {
      if ((*cmpFunc) (temp->next->key, key) == 0)
	{
	  del = temp->next;
	  temp->next = temp->next->next;
	  if (nodeDelete)
	    (*nodeDelete) (del);
	  UT_FREE (del);
	  return 0;
	}
    }
  return -1;
}

/************************************************************************
 * name:        ListDelete - delete list				*
 *                                                                      *
 * arguments:   head    - list header					*
 *              nodeDelete - 'key', 'value' deallocation function	*
 *                                                                      *
 * returns/side-effects:                                                *
 *              (ST_LIST*) NULL						*
 *                                                                      *
 * description: delete all nodes of list.				*
 *									*
 * NOTE:        							*
 *  If 'key' or 'value' has its own memory, the memory must be freed	*
 *  in 'nodeDelete' function. But, the node container is freed in	*
 *  this module.							*
 *  dummy header포함하여 모두 지운다.
 ************************************************************************/

PUBLIC void
ListDelete (ST_LIST * head, void (*nodeDelete) (ST_LIST *))
{
  ST_LIST *temp;
  ST_LIST *del;

  temp = head;

  while (temp != NULL)
    {
      del = temp;
      temp = temp->next;
      if (nodeDelete)
	(*nodeDelete) (del);
      UT_FREE (del);
    }
}

/* Create dummy header */
PUBLIC ERR_CODE
ListCreate (ST_LIST ** head)
{

  *head = (ST_LIST *) UT_ALLOC (sizeof (ST_LIST));
  if (*head == NULL)
    return -1;
  (*head)->key = NULL;
  (*head)->value = NULL;
  (*head)->next = NULL;
  return 0;
}

#ifdef DEBUG
void PUBLIC
ListPrint (ST_LIST * head, void (*nodePrint) (ST_LIST *))
{
  ST_LIST *temp = head->next;

  while (temp != NULL)
    {
      (*nodePrint) (temp);
      temp = temp->next;
    }
}
#endif

/************************************************************************
* name: IsAnyNode
* arguments:
*	ST_LIST head;
* returns/side-effects:
* description:
* 	Due to using dummy head....
* NOTE:
************************************************************************/

PUBLIC _BOOL_
IsAnyNode (ST_LIST * head)
{
  if (head->next != NULL)
    {
      return _TRUE_;
    }
  else
    {
      return _FALSE_;
    }
}

/************************************************************************
* name: NodeHead
* arguments:
*	ST_LIST head;
* returns/side-effects:
* 	head node pointer - ST_LIST* 
* description:
* NOTE:
************************************************************************/

PUBLIC ST_LIST *
HeadNode (ST_LIST * dummyHead)
{
  if (dummyHead != NULL)
    {
      return dummyHead->next;
    }
  else
    {
      return NULL;
    }
}

/************************************************************************
* name: NodeNext
* arguments:
*	this node - ST_LIST*
* returns/side-effects:
*	next node - ST_LIST*
* description:
* NOTE:
************************************************************************/

PUBLIC ST_LIST *
NextNode (ST_LIST * node)
{
  if (node != NULL)
    {
      return node->next;
    }
  else
    {
      return NULL;
    }
}


/************************************************************************
* name:  NodeAssign
* arguments: 
*	node - data node
*	key  - key as string
*	value - value string
* returns/side-effects: 
*	OK , ERROR if malloc error
* description: 
* NOTE: 
************************************************************************/

PUBLIC ERR_CODE
NodeAssign (ST_LIST * node, void *key, void *value)
{
  if (key != NULL)
    {
      node->key = (char *) UT_ALLOC (strlen ((char *) key) + 1);
      if (node->key == NULL)
	return -1;

      strcpy ((char *) node->key, (char *) key);
    }
  else
    {
      node->key = NULL;
    }

  node->value = value;

  return 0;
}


/************************************************************************
* name: NodeCompare
* arguments: 
*	key - key as string
*	search - search string
* returns/side-effects: 
*	OK if equal , ERROR else
* description: 
* NOTE: 
************************************************************************/

PUBLIC ERR_CODE
NodeCompare (void *key, void *search)
{
  if (key == NULL || key == search)
    return -1;

  if (strcmp ((char *) key, (char *) search) == 0)
    {
      return 0;
    }
  else
    {
      return -1;
    }
}


/************************************************************************
* name:  NodeFree
* arguments: 
*	node - ST_List node
* returns/side-effects: 
* description: 
* NOTE: 
************************************************************************/

void
NodeFree (ST_LIST * node)
{
  if (node != NULL)
    {
      if (node->key != NULL)
	{
	  UT_FREE (node->key);
	}
      if (node->value != NULL)
	{
	  UT_FREE (node->value);
	}
    }
}

/******************************************************************************
 * str_like
 *
 * Arguments:
 *               src: (IN) Source string.
 *           pattern: (IN) Pattern match string.
 *          esc_char: (IN) Pointer to escape character.  This pointer should
 *                         be NULL when an escape character is not used.
 *			case_sensitive : (IN) 1 - case sensitive, 0 - case insensitive
 *
 * Returns: int 
 *			_TRUE_(match), _FALSE_(not match), _ERROR_(error)
 *
 * Errors:
 *
 * Description:
 *     Perform a "like" regular expression pattern match between the pattern
 *     string and the source string.  The pattern string may contain the
 *     '%' character to match 0 or more characters, or the '_' character
 *     to match exactly one character.  These special characters may be
 *     interpreted as normal characters my escaping them.  In this case the
 *     escape character is none NULL.  It is assumed that all strings are
 *     of the same codeset.
 *
 *
 *****************************************************************************/

PUBLIC int
str_like (const unsigned char *src,
	  const unsigned char *pattern,
	  const unsigned char esc_char, short case_sensitive)
{
  int result;
  unsigned char *low_src = NULL, *low_pattern = NULL;
  int i, n;

  if (case_sensitive)
    {
      result = str_eval_like (src, pattern, (unsigned char) esc_char);
    }
  else
    {
      low_src = UT_MAKE_STRING (src, -1);
      low_pattern = UT_MAKE_STRING (pattern, -1);

      n = strlen (low_src);
      for (i = 0; i < n; ++i)
	{
	  low_src[i] = (unsigned char) char_tolower (low_src[i]);
	}

      n = strlen (low_pattern);
      for (i = 0; i < n; ++i)
	{
	  low_pattern[i] = (unsigned char) char_tolower (low_pattern[i]);
	}

      result = str_eval_like (low_src, low_pattern, (unsigned char) esc_char);

      NC_FREE (low_src);
      NC_FREE (low_pattern);
    }

  return result;
}


PRIVATE int
str_eval_like (const unsigned char *tar,
	       const unsigned char *expr, unsigned char escape)
{
  const int IN_CHECK = 0;
  const int IN_PERCENT = 1;
  const int IN_PERCENT_UNDERLINE = 2;

  int status = IN_CHECK;
  const unsigned char *tarstack[STK_SIZE], *exprstack[STK_SIZE];
  int stackp = -1;
  int inescape = 0;

  if (escape == 0)
    escape = 2;
  while (1)
    {
      if (status == IN_CHECK)
	{
	  if (*expr == escape)
	    {
	      expr++;
	      inescape = 1;
	      if (*expr != '%' && *expr != '_')
		{
		  return _ERROR_;
		}
	      continue;
	    }

	  if (inescape)
	    {
	      if (*tar == *expr)
		{
		  tar++;
		  expr++;
		}
	      else
		{
		  if (stackp >= 0 && stackp <= STK_SIZE)
		    {
		      tar = tarstack[stackp];
		      if (is_korean (*tar))
			{
			  tar += 2;
			}
		      else
			{
			  tar++;
			}
		      expr = exprstack[stackp--];
		    }
		  else
		    {
		      return _FALSE_;
		    }
		}
	      inescape = 0;
	      continue;
	    }

	  /* goto check */
	  if (*expr == 0)
	    {
	      while (*tar == ' ')
		{
		  tar++;
		}

	      if (*tar == 0)
		{
		  return _TRUE_;
		}
	      else
		{
		  if (stackp >= 0 && stackp <= STK_SIZE)
		    {
		      tar = tarstack[stackp];
		      if (is_korean (*tar))
			{
			  tar += 2;
			}
		      else
			{
			  tar++;
			}
		      expr = exprstack[stackp--];
		    }
		  else
		    {
		      return _FALSE_;
		    }
		}
	    }
	  else if (*expr == '%')
	    {
	      status = IN_PERCENT;
	      while (*(expr + 1) == '%')
		{
		  expr++;
		}
	    }
	  else if ((*expr == '_')
		   || (!is_korean (*tar) && *tar == *expr)
		   || (is_korean (*tar) &&
		       *tar == *expr && *(tar + 1) == *(expr + 1)))
	    {
	      if (is_korean (*tar))
		{
		  tar += 2;
		}
	      else
		{
		  tar++;
		}
	      if (is_korean (*expr))
		{
		  expr += 2;
		}
	      else
		{
		  expr++;
		}
	    }
	  else if (stackp >= 0 && stackp <= STK_SIZE)
	    {
	      tar = tarstack[stackp];
	      if (is_korean (*tar))
		{
		  tar += 2;
		}
	      else
		{
		  tar++;
		}

	      expr = exprstack[stackp--];
	    }
	  else if (stackp > STK_SIZE)
	    {
	      return _ERROR_;
	    }
	  else
	    {
	      return _FALSE_;
	    }
	}
      else if (status == IN_PERCENT)
	{
	  if (*(expr + 1) == '_')
	    {
	      if (stackp >= STK_SIZE)
		{
		  return _ERROR_;
		}
	      tarstack[++stackp] = tar;
	      exprstack[stackp] = expr;
	      expr++;

	      if (stackp > STK_SIZE)
		{
		  return _ERROR_;
		}
	      inescape = 0;
	      status = IN_PERCENT_UNDERLINE;
	      continue;
	    }

	  if (*(expr + 1) == escape)
	    {
	      expr++;
	      inescape = 1;
	      if (*(expr + 1) != '%' && *(expr + 1) != '_')
		{
		  return _ERROR_;
		}
	    }

	  while (*tar && *tar != *(expr + 1))
	    {
	      if (is_korean (*tar))
		{
		  tar += 2;
		}
	      else
		{
		  tar++;
		}
	    }

	  if (*tar == *(expr + 1))
	    {
	      if (stackp >= STK_SIZE)
		{
		  return _ERROR_;
		}
	      tarstack[++stackp] = tar;
	      exprstack[stackp] = expr;
	      if (is_korean (*expr))
		{
		  expr += 2;
		}
	      else
		{
		  expr++;
		}

	      if (stackp > STK_SIZE)
		{
		  return _ERROR_;
		}
	      inescape = 0;
	      status = IN_CHECK;
	    }
	}
      if (status == IN_PERCENT_UNDERLINE)
	{
	  if (*expr == escape)
	    {
	      expr++;
	      inescape = 1;
	      if (*expr != '%' && *expr != '_')
		{
		  return _ERROR_;
		}
	      continue;
	    }

	  if (inescape)
	    {
	      if (*tar == *expr)
		{
		  tar++;
		  expr++;
		}
	      else
		{
		  if (stackp >= 0 && stackp <= STK_SIZE)
		    {
		      tar = tarstack[stackp];
		      if (is_korean (*tar))
			{
			  tar += 2;
			}
		      else
			{
			  tar++;
			}
		      expr = exprstack[stackp--];
		    }
		  else
		    {
		      return _FALSE_;
		    }
		}
	      inescape = 0;
	      continue;
	    }

	  /* goto check */
	  if (*expr == 0)
	    {
	      while (*tar == ' ')
		{
		  tar++;
		}

	      if (*tar == 0)
		{
		  return _TRUE_;
		}
	      else
		{
		  if (stackp >= 0 && stackp <= STK_SIZE)
		    {
		      tar = tarstack[stackp];
		      if (is_korean (*tar))
			{
			  tar += 2;
			}
		      else
			{
			  tar++;
			}
		      expr = exprstack[stackp--];
		    }
		  else
		    {
		      return _FALSE_;
		    }
		}
	    }
	  else if (*expr == '%')
	    {
	      status = IN_PERCENT;
	      while (*(expr + 1) == '%')
		{
		  expr++;
		}
	    }
	  else if ((*expr == '_')
		   || (!is_korean (*tar) && *tar == *expr)
		   || (is_korean (*tar) &&
		       *tar == *expr && *(tar + 1) == *(expr + 1)))
	    {
	      if (is_korean (*tar))
		{
		  tar += 2;
		}
	      else
		{
		  tar++;
		}
	      if (is_korean (*expr))
		{
		  expr += 2;
		}
	      else
		{
		  expr++;
		}
	    }
	  else if (stackp >= 0 && stackp <= STK_SIZE)
	    {
	      tar = tarstack[stackp];
	      if (is_korean (*tar))
		{
		  tar += 2;
		}
	      else
		{
		  tar++;
		}

	      expr = exprstack[stackp--];
	    }
	  else if (stackp > STK_SIZE)
	    {
	      return _ERROR_;
	    }
	  else
	    {
	      return _FALSE_;
	    }
	}

      if (*tar == 0)
	{
	  if (*expr)
	    {
	      while (*expr == '%')
		{
		  expr++;
		}
	    }

	  if (*expr == 0)
	    {
	      return _TRUE_;
	    }
	  else
	    {
	      return _FALSE_;
	    }
	}
    }
}

PRIVATE int
is_korean (unsigned char ch)
{
  return (ch >= 0xb0 && ch <= 0xc8) || (ch >= 0xa1 && ch <= 0xfe);
}


/*-----------------------------------------------------------------------
 *						Connection string
 *----------------------------------------------------------------------*/

/************************************************************************
 * name: next_element
 * arguments:
 * returns/side-effects:
 *		더이상 value가 없을 때 NULL return
 * description:
 * NOTE:
 *	element list structure
 *		KEY1=VALUE1\0KEY2=VALUE2\0KEY3=VALUE3\0\0
 *		\0 - end of attribute
 *		\0 - end of list
 ************************************************************************/
PUBLIC const char *
next_element (const char *element_list)
{
  const char *pt;

  if (element_list == NULL)
    return NULL;

  pt = element_list;

  pt += strlen (element_list) + 1;	// +1 is for '\0'

  if (*pt == '\0')
    return NULL;		// end of list

  return pt;
}

/************************************************************************
 * name: attr_value
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PUBLIC const char *
element_value (const char *element)
{
  const char *pt;

  if (element == NULL)
    return NULL;

  pt = strchr (element, '=');
  if (pt == NULL)
    return NULL;

  ++pt;				// for '='

  return pt;
}

/************************************************************************
 * name: element_value_by_key
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PUBLIC const char *
element_value_by_key (const char *element_list, const char *key)
{
  const char *pt;
  const char *val;

  if (element_list == NULL || key == NULL)
    return NULL;

  pt = element_list;
  do
    {
      if (_strnicmp (pt, key, strlen (key)) == 0)
	{
	  val = element_value (pt);
	  return val;
	}
      pt = next_element (pt);
    }
  while (pt != NULL);

  return NULL;
}

/************************************************************************
 * name: is_oidstr
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 *	앞뒤의 공백문자를 허용한다.
 ************************************************************************/
PUBLIC short
is_oidstr_array (char **array, int size)
{
  int i;

  for (i = 0; i < size; ++i)
    {
      if (is_oidstr (array[i]) == _FALSE_)
	return _FALSE_;
    }

  return _TRUE_;
}

/************************************************************************
 * name: is_oidstr
 * arguments:
 * returns/side-effects:
 * description:
 *	string이 oid인지 판별한다. 
 * NOTE:
 *	앞뒤의 공백문자를 허용한다.
 ************************************************************************/
PUBLIC short
is_oidstr (char *str)
{
  char *pt;
  char state;


  for (pt = str, state = 0; *pt != '\0'; ++pt)
    {
      switch (state)
	{
	case 0:		// start
	  if (*pt == '@')
	    state = 1;
	  else if (*pt == ' ' || *pt == '\t')
	    break;
	  else
	    return _FALSE_;

	  break;

	case 1:		// first digit of page id
	case 3:		// first digit of slot id
	case 5:		// first digit of volume id
	  if (*pt >= '0' && *pt <= '9')
	    ++state;
	  else
	    {
	      return _FALSE_;
	    }
	  break;

	case 2:		// page id
	case 4:		// slot id
	  if (*pt == '|')
	    ++state;
	  else if (*pt < '0' || *pt > '9')
	    {
	      return _FALSE_;
	    }
	  break;

	case 6:
	  if (*pt == ' ' || *pt == '\t')
	    state = 7;
	  else if (*pt < '0' || *pt > '9')
	    {
	      return _FALSE_;
	    }

	  break;
	case 7:
	  if (*pt == ' ' || *pt == '\t')
	    break;
	  else
	    return _FALSE_;

	  break;

	default:
	  return _FALSE_;
	}
    }

  if (state != 6 && state != 7)
    {
      return _FALSE_;
    }

  return _TRUE_;
}


/************************************************************************
 * name:  replace_oid
 * arguments:
 * returns/side-effects:
 *		교체된 oid parameter 개수를 return한다.
 * description:
 * NOTE:
 *		parameter number는 1을 base로 하고 있다고 가정했다.
 ************************************************************************/
PUBLIC int
replace_oid (char *sql_text, char **org_param_pos_pt,
	     char **oid_param_pos_pt, char **oid_param_val_pt)
{
  char *oid_buf = NULL;
  char oid_param_pos[256];
  char org_param_pos[256];
  char oid_param_val[1024];
  char buf[126];
  char *pt;
  char *pt_tmp;
  char current_param_pos = 0;
  char oid_param_num = 0;

  org_param_pos[0] = '\0';
  oid_param_pos[0] = '\0';
  oid_param_val[0] = '\0';

  for (pt = sql_text; *pt != '\0'; ++pt)
    {
      switch (*pt)
	{
	case '?':
	  ++current_param_pos;
	  sprintf (buf, "%d", current_param_pos);
	  add_element_to_setstring (org_param_pos, buf);
	  break;

	case '\'':
	  // find the matched string marker
	  pt_tmp = pt;
	  while (pt_tmp = strchr (pt_tmp + 1, '\''))
	    {
	      if (pt_tmp == NULL || *(pt_tmp - 1) != '\\')
		break;

	    }
	  if (pt_tmp == NULL)
	    break;

	  oid_buf = UT_MAKE_STRING (pt + 1, (int) (pt_tmp - pt - 1));
	  trim (oid_buf);

	  if (is_oidstr (oid_buf) == _TRUE_)
	    {
	      ++current_param_pos;

	      ++oid_param_num;

	      sprintf (buf, "%d", current_param_pos);
	      add_element_to_setstring (oid_param_pos, buf);

	      sprintf (buf, "%s", oid_buf);
	      add_element_to_setstring (oid_param_val, buf);

	      // replace oid string value to parameter marker
	      *pt = '?';
	      memcpy (pt + 1, pt_tmp + 1, strlen (pt_tmp + 1) + 1);	// include Null terminator
	    }

	  NA_FREE (oid_buf);
	  break;
	}
    }

  if (org_param_pos_pt != NULL)
    *org_param_pos_pt = UT_MAKE_STRING (org_param_pos, -1);
  if (oid_param_pos_pt != NULL)
    *oid_param_pos_pt = UT_MAKE_STRING (oid_param_pos, -1);
  if (oid_param_val_pt != NULL)
    *oid_param_val_pt = UT_MAKE_STRING (oid_param_val, -1);

  return oid_param_num;

}


/************************************************************************
 * name: trim
 * arguments:
 * returns/side-effects:
 * description:
 *	전후의 공백문자를 제거한 형태로 변형한다. (str의 내용이 변형됨)
 * NOTE:
 ************************************************************************/
PUBLIC char *
trim (char *str)
{
  char *p;
  char *s;

  if (str == NULL)
    return (str);

  for (s = str;
       *s != '\0' && (*s == ' ' || *s == '\t' || *s == 0x0d || *s == 0x0a);
       s++)
    ;
  if (*s == '\0')
    {
      *str = '\0';
      return (str);
    }

  /* *s must be a non-white char */
  for (p = s; *p != '\0'; p++)
    ;
  for (p--; *p == ' ' || *p == '\t' || *p == 0x0d || *p == 0x0a; p--)
    ;
  *++p = '\0';

  if (s != str)
    memcpy (str, s, strlen (s) + 1);

  return (str);
}

/************************************************************************
 * name: str_value_assign
 * arguments:
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PUBLIC RETCODE
str_value_assign (const char *in_value,
		  char *out_buf, SQLLEN out_buf_len, SQLLEN *val_len_ptr)
{
  RETCODE rc = ODBC_SUCCESS;


  if (in_value != NULL)
    {
      if (out_buf != NULL && out_buf_len > 0)
	{
	  strncpy (out_buf, in_value, out_buf_len - 1);
	  out_buf[out_buf_len - 1] = '\0';
	}

      if ((unsigned int) out_buf_len <= strlen (in_value))
	rc = ODBC_SUCCESS_WITH_INFO;
    }

  if (val_len_ptr != NULL)
    {
      *val_len_ptr = ODBC_STRLEN_IND (in_value);
    }

  return rc;
}


/************************************************************************
 * name: bin_value_assign
 * arguments:
 *		in_value - input binary start pointer
 *		in_val_len - input binary length
 *		out_buf - output binary start pointer
 *		out_buf_len - output binary length
 *		val_len_ptr - input binary length or null indicator
 * returns/side-effects:
 * description:
 * NOTE:
 ************************************************************************/
PUBLIC RETCODE
bin_value_assign (const void *in_value,
		  SQLLEN in_val_len,
		  char *out_buf, SQLLEN out_buf_len, SQLLEN *val_len_ptr)
{
  RETCODE rc = ODBC_SUCCESS;

  if (in_value != NULL)
    {
      if (out_buf != NULL)
	{
	  memcpy (out_buf, in_value, MIN (out_buf_len, in_val_len));
	}

      if (out_buf_len < in_val_len)
	{
	  rc = ODBC_SUCCESS_WITH_INFO;
	}
    }

  if (val_len_ptr != NULL)
    {
      if (in_value == NULL)
	{
	  *val_len_ptr = SQL_NULL_DATA;
	}
      else
	{
	  *val_len_ptr = in_val_len;
	}
    }

  return rc;
}
