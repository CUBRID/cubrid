/************************************************************************/
/* TODO: license comment                                                */
/************************************************************************/

#ifndef _DB_JSON_H
#define _DB_JSON_H

/* *INDENT-OFF* */
#if defined (__cplusplus)

#include "rapidjson/schema.h"
#include "rapidjson/document.h"
#include "rapidjson/encodings.h"
#include "rapidjson/allocators.h"

class JSON_PRIVATE_ALLOCATOR
{
public:
  static const bool kNeedFree;
  void *Malloc (size_t size);
  void *Realloc (void *originalPtr, size_t originalSize, size_t newSize);
  static void Free (void *ptr);
};

typedef rapidjson::UTF8<> JSON_ENCODING;
typedef rapidjson::MemoryPoolAllocator<JSON_PRIVATE_ALLOCATOR> JSON_PRIVATE_MEMPOOL;

typedef rapidjson::GenericDocument<JSON_ENCODING, JSON_PRIVATE_MEMPOOL> JSON_DOC;
typedef rapidjson::GenericValue<JSON_ENCODING, JSON_PRIVATE_MEMPOOL> JSON_VALUE;
typedef rapidjson::GenericPointer<JSON_VALUE> JSON_POINTER;

class JSON_VALIDATOR
{
public:
  JSON_VALIDATOR ();
  ~JSON_VALIDATOR ();

  int load (const char * schema_raw);
  int copy_from (const JSON_VALIDATOR& my_copy);
  int validate (const JSON_DOC& doc) const;

private:
  int generate_schema_validator (void);

  rapidjson::Document* document;
  rapidjson::SchemaDocument* schema;
  rapidjson::SchemaValidator* validator;
};

#else /* !defined (__cplusplus) */
typedef void JSON_DOC;
typedef void JSON_VALUE;
typedef void JSON_POINTER;

typedef struct json_validator JSON_VALIDATOR;
struct json_validator
{
  int dummy;
};
#endif /* !defined (__cplusplus) */

/* *INDENT-ON* */

#endif /* db_json.h */
