#ifndef _DB_JSON_H
#define _DB_JSON_H

#if defined (__cplusplus)
#include "rapidjson/schema.h"
#include "rapidjson/document.h"
#include "error_code.h"
#include "error_manager.h"
#include "rapidjson/error/en.h"
#include "rapidjson/encodings.h"
#include "rapidjson/allocators.h"
#include "memory_alloc.h"

class cubrid_json_allocator
{
public:
  static const bool kNeedFree;
  void *Malloc (size_t size);
  void *Realloc (void *originalPtr, size_t originalSize, size_t newSize);
  static void Free (void *ptr);
};

typedef
  rapidjson::GenericDocument <
  rapidjson::UTF8 <>,
  rapidjson::MemoryPoolAllocator <
cubrid_json_allocator > >
  cubrid_document;
typedef
  rapidjson::GenericSchemaDocument <
  rapidjson::GenericValue <
  rapidjson::UTF8 <>,
  rapidjson::MemoryPoolAllocator <
cubrid_json_allocator > > >
  cubrid_schema_document;
typedef
  rapidjson::GenericSchemaValidator <
  cubrid_schema_document >
  cubrid_schema_validator;
typedef
  rapidjson::GenericValue <
  rapidjson::UTF8 <>,
  rapidjson::MemoryPoolAllocator <
cubrid_json_allocator > >
  cubrid_value;
typedef
  rapidjson::GenericPointer <
  rapidjson::GenericValue <
  rapidjson::UTF8 <>,
  rapidjson::MemoryPoolAllocator <
cubrid_json_allocator > > >
  cubrid_pointer;

typedef struct db_json_validation
  DB_JSON_VALIDATION_OBJECT;
struct db_json_validation
{
  rapidjson::Document *
    document;
  rapidjson::SchemaDocument *
    schema;
  rapidjson::SchemaValidator *
    validator;
};

DB_JSON_VALIDATION_OBJECT
get_validator_from_schema_string (const char *schema_raw);
DB_JSON_VALIDATION_OBJECT
get_copy_of_validator (const DB_JSON_VALIDATION_OBJECT validator, const char *raw_schema);
#endif

#endif /* db_json.h */
