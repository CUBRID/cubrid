#include "db_json.h"

DB_JSON_VALIDATION_OBJECT
get_validator_from_schema_string (const char *schema_raw)
{
  rapidjson::Document *doc = new rapidjson::Document ();
  DB_JSON_VALIDATION_OBJECT val_obj;

  if (doc->Parse (schema_raw).HasParseError ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_JSON, 2,
	      rapidjson::GetParseError_En (doc->GetParseError ()), doc->GetErrorOffset ());
      return val_obj;
    }

  val_obj.schema = new rapidjson::SchemaDocument (*doc);
  val_obj.validator = new rapidjson::SchemaValidator (*val_obj.schema);
  val_obj.document = doc;

  return val_obj;
}

DB_JSON_VALIDATION_OBJECT
get_copy_of_validator (const DB_JSON_VALIDATION_OBJECT validation_obj, const char *raw_schema)
{
  DB_JSON_VALIDATION_OBJECT copy;

  copy.document = new rapidjson::Document ();
  copy.document->Parse (raw_schema);
  copy.schema = new rapidjson::SchemaDocument (*validation_obj.document);
  copy.validator = new rapidjson::SchemaValidator (*copy.schema);

  return copy;
}

void *cubrid_json_allocator::Malloc(size_t size)
{
  if (size) //  behavior of malloc(0) is implementation defined.
    {
      return db_private_alloc (NULL, size);
    }
  else
    {
      return NULL; // standardize to returning NULL.

    }
}

void *cubrid_json_allocator::Realloc(void* originalPtr, size_t originalSize, size_t newSize)
{
  (void) originalSize;
  if (newSize == 0)
    {
        db_private_free (NULL, originalPtr);
        return NULL;
    }
  return db_private_realloc (NULL, originalPtr, newSize);
}

void cubrid_json_allocator::Free(void *ptr)
{
  db_private_free (NULL, ptr);
}

const bool cubrid_json_allocator::kNeedFree = true;
