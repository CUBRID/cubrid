#include "db_json_types_internal.hpp"

/************************************************************************/
/* JSON_DOC implementation                                              */
/************************************************************************/

bool JSON_DOC::IsLeaf()
{
  return !IsArray () && !IsObject ();
}

void *
JSON_PRIVATE_ALLOCATOR::Malloc (size_t size)
{
  if (size)			//  behavior of malloc(0) is implementation defined.
    {
      char *p = (char *) db_private_alloc (NULL, size);
      if (prm_get_bool_value (PRM_ID_JSON_LOG_ALLOCATIONS))
	{
	  er_print_callstack (ARG_FILE_LINE, "JSON_ALLOC: Traced pointer=%p\n", p);
	}
      return p;
    }
  else
    {
      return NULL;		// standardize to returning NULL.
    }
}

void *
JSON_PRIVATE_ALLOCATOR::Realloc (void *originalPtr, size_t originalSize, size_t newSize)
{
  (void) originalSize;
  char *p;
  if (newSize == 0)
    {
      db_private_free (NULL, originalPtr);
      return NULL;
    }
  p = (char *) db_private_realloc (NULL, originalPtr, newSize);
  if (prm_get_bool_value (PRM_ID_JSON_LOG_ALLOCATIONS))
    {
      er_print_callstack (ARG_FILE_LINE, "Traced pointer=%p\n", p);
    }
  return p;
}

void
JSON_PRIVATE_ALLOCATOR::Free (void *ptr)
{
  db_private_free (NULL, ptr);
}

const bool JSON_PRIVATE_ALLOCATOR::kNeedFree = true;

JSON_WALKER::JSON_WALKER ()
  : m_stop (false)
  , m_skip (false)
{

}

int
JSON_WALKER::WalkDocument (JSON_DOC &document)
{
  return WalkValue (db_json_doc_to_value (document));
}

int
JSON_WALKER::WalkValue (JSON_VALUE &value)
{
  int error_code = NO_ERROR;

  if (m_stop)
    {
      return NO_ERROR;
    }
  error_code = CallBefore (value);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  if (m_stop)
    {
      return NO_ERROR;
    }

  if (value.IsObject ())
    {
      for (auto it = value.MemberBegin (); it != value.MemberEnd (); ++it)
	{
	  CallOnKeyIterate (it->name);
	  if (m_stop)
	    {
	      return NO_ERROR;
	    }
	  if (m_skip)
	    {
	      continue;
	    }
	  error_code = WalkValue (it->value);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return error_code;
	    }
	  if (m_stop)
	    {
	      return NO_ERROR;
	    }
	}
    }
  else if (value.IsArray ())
    {
      for (JSON_VALUE *it = value.Begin (); it != value.End (); ++it)
	{
	  CallOnArrayIterate ();
	  if (m_stop)
	    {
	      return NO_ERROR;
	    }
	  if (m_skip)
	    {
	      continue;
	    }
	  error_code = WalkValue (*it);
	  if (error_code != NO_ERROR)
	    {
	      ASSERT_ERROR ();
	      return error_code;
	    }
	  if (m_stop)
	    {
	      return NO_ERROR;
	    }
	}
    }

  error_code = CallAfter (value);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  return NO_ERROR;
}

DB_JSON_TYPE
db_json_get_type_of_value (const JSON_VALUE *val)
{
  if (val == NULL)
    {
      return DB_JSON_NULL;
    }

  if (val->IsString ())
    {
      return DB_JSON_STRING;
    }
  else if (val->IsInt ())
    {
      return DB_JSON_INT;
    }
  else if (val->IsInt64 ())
    {
      return DB_JSON_BIGINT;
    }
  else if (val->IsFloat () || val->IsDouble ())
    {
      return DB_JSON_DOUBLE;
    }
  else if (val->IsObject ())
    {
      return DB_JSON_OBJECT;
    }
  else if (val->IsArray ())
    {
      return DB_JSON_ARRAY;
    }
  else if (val->IsNull ())
    {
      return DB_JSON_NULL;
    }
  else if (val->IsBool ())
    {
      return DB_JSON_BOOL;
    }

  return DB_JSON_UNKNOWN;
}

/*
* db_json_doc_to_value ()
* doc (in)
* value (out)
* We need this cast in order to use the overloaded methods
* JSON_DOC is derived from GenericDocument which also extends GenericValue
* Yet JSON_DOC and JSON_VALUE are two different classes because they are templatized and their type is not known
* at compile time
*/
JSON_VALUE &
db_json_doc_to_value (JSON_DOC &doc)
{
  return reinterpret_cast<JSON_VALUE &> (doc);
}

const JSON_VALUE &
db_json_doc_to_value (const JSON_DOC &doc)
{
  return reinterpret_cast<const JSON_VALUE &> (doc);
}
