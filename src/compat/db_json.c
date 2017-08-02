#include "db_json.h"

DB_JSON_VALIDATION_OBJECT * get_validator_from_schema_string (const char * schema_raw)
{
  rapidjson::Document * doc = new rapidjson::Document();
  DB_JSON_VALIDATION_OBJECT * val_obj;

  if (doc->Parse (schema_raw).HasParseError ())
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_INVALID_JSON, 2,
              rapidjson::GetParseError_En (doc->GetParseError()),
              doc->GetErrorOffset());
      return NULL;
    }

  val_obj = (DB_JSON_VALIDATION_OBJECT *) malloc (sizeof (DB_JSON_VALIDATION_OBJECT));
  val_obj->schema = new rapidjson::SchemaDocument (*doc);
  val_obj->validator = new rapidjson::SchemaValidator (*val_obj->schema);
  val_obj->document = doc;

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
