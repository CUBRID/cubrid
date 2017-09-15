/************************************************************************/
/* TODO: license comment                                                */
/*       function comments                                              */
/************************************************************************/

#include "db_json.h"

#include "error_manager.h"
#include "memory_alloc.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/error/en.h"

/* *INDENT-OFF* */

JSON_VALIDATOR::JSON_VALIDATOR (void)
{
  document = NULL;
  schema = NULL;
  validator = NULL;
}

JSON_VALIDATOR::~JSON_VALIDATOR (void)
{
  if (validator != NULL)
    {
      delete validator;
      validator = NULL;
    }
  if (schema != NULL)
    {
      delete schema;
      schema = NULL;
    }
  if (document != NULL)
    {
      delete document;
      document = NULL;
    }
}

int
JSON_VALIDATOR::load (const char *schema_raw)
{
  if (schema_raw == NULL)
    {
      /* no schema */
      return NO_ERROR;
    }

  /* don't leak resources */
  this->~JSON_VALIDATOR ();

  /* todo: do we have to allocate document? */
  document = new rapidjson::Document ();
  if (document == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (rapidjson::Document));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  document->Parse (schema_raw);
  if (document->HasParseError ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_JSON, 2,
              rapidjson::GetParseError_En (document->GetParseError ()), document->GetErrorOffset ());
      return ER_INVALID_JSON;
    }
  return generate_schema_validator ();
}

int
JSON_VALIDATOR::copy_from (const JSON_VALIDATOR& copy_schema)
{
  /* don't leak resources */
  this->~JSON_VALIDATOR ();

  if (copy_schema.document == NULL)
    {
      /* no schema actually */
      assert (copy_schema.schema == NULL && copy_schema.validator == NULL);
      return NO_ERROR;
    }

  document = new rapidjson::Document ();
  if (document == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (rapidjson::Document));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  /* todo: is this safe? */
  document->CopyFrom (*copy_schema.document, document->GetAllocator ());
  return generate_schema_validator ();
}

int
JSON_VALIDATOR::generate_schema_validator (void)
{
  schema = new rapidjson::SchemaDocument (*document);
  if (schema == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (rapidjson::SchemaDocument));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  validator = new rapidjson::SchemaValidator (*schema);
  if (validator == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (rapidjson::SchemaValidator));
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }
  return NO_ERROR;
}

int
JSON_VALIDATOR::validate (const JSON_DOC& doc) const
{
  int error_code = NO_ERROR;
  if (validator == NULL)
    {
      return NO_ERROR;
    }

  if (!doc.Accept (*validator))
    {
      rapidjson::StringBuffer sb1, sb2;

      validator->GetInvalidSchemaPointer ().StringifyUriFragment (sb1);
      validator->GetInvalidDocumentPointer ().StringifyUriFragment (sb2);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_JSON_INVALIDATED_BY_SCHEMA, 3, sb1.GetString (),
              validator->GetInvalidSchemaKeyword (), sb2.GetString ());
      error_code = ER_JSON_INVALIDATED_BY_SCHEMA;
    }
  validator->Reset ();

  return error_code;
}

void *
JSON_PRIVATE_ALLOCATOR::Malloc (size_t size)
{
  if (size)			//  behavior of malloc(0) is implementation defined.
    {
      return db_private_alloc (NULL, size);
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
  if (newSize == 0)
    {
      db_private_free (NULL, originalPtr);
      return NULL;
    }
  return db_private_realloc (NULL, originalPtr, newSize);
}

void
JSON_PRIVATE_ALLOCATOR::Free (void *ptr)
{
  db_private_free (NULL, ptr);
}

const bool JSON_PRIVATE_ALLOCATOR::kNeedFree = true;

/* *INDENT-ON* */
