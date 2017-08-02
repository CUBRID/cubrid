#ifndef _DB_JSON_H
#define _DB_JSON_H

#include "rapidjson/schema.h"
#include "rapidjson/document.h"
#include "error_code.h"
#include "error_manager.h"
#include "rapidjson/error/en.h"

typedef struct db_json_validation DB_JSON_VALIDATION_OBJECT;
struct db_json_validation
{
  rapidjson::Document * document;
  rapidjson::SchemaDocument * schema;
  rapidjson::SchemaValidator * validator;
};

DB_JSON_VALIDATION_OBJECT * get_validator_from_schema_string (const char * schema_raw);
DB_JSON_VALIDATION_OBJECT * get_copy_of_validator (DB_JSON_VALIDATION_OBJECT * validator, const char * raw_schema);

#endif /* db_json.h */
