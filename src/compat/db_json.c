#include "db_json.h"

DB_JSON_VALIDATION_OBJECT * get_validator_from_schema_string (const char * schema_raw, int * rc)
{
  assert (rc != 0);

  rapidjson::Document * doc = new rapidjson::Document();
  DB_JSON_VALIDATION_OBJECT * val_obj;

  if (doc->Parse (schema_raw).HasParseError ())
    {
      *rc = ER_INVALID_JSON_SCHEMA;
      return NULL;
    }

  val_obj = (DB_JSON_VALIDATION_OBJECT *) malloc (sizeof (DB_JSON_VALIDATION_OBJECT));
  val_obj->schema = new rapidjson::SchemaDocument (*doc);
  val_obj->validator = new rapidjson::SchemaValidator (*val_obj->schema);
  val_obj->document = doc;

  *rc = NO_ERROR;
  return val_obj;
}

DB_JSON_VALIDATION_OBJECT * get_copy_of_validator (DB_JSON_VALIDATION_OBJECT * validation_obj, const char * raw_schema)
{
  DB_JSON_VALIDATION_OBJECT * copy = (DB_JSON_VALIDATION_OBJECT *) malloc (sizeof (DB_JSON_VALIDATION_OBJECT));

  copy->document = new rapidjson::Document;
  copy->document->Parse (raw_schema);
  copy->schema = new rapidjson::SchemaDocument (*validation_obj->document);
  copy->validator = new rapidjson::SchemaValidator (*copy->schema);

  return copy;
}
