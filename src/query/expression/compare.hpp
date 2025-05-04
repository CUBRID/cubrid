#include "dbtype_def.h"
#include "object_primitive.h"
#include <assert.h>

#ifndef _COMPARE_H_
#define _COMPARE_H_

namespace Compare
{
  using Cmpfunc = DB_VALUE_COMPARE_RESULT (*)(const DB_VALUE *, const DB_VALUE *);

  DB_VALUE_COMPARE_RESULT NoneCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT IntegerCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT FloatCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT DoubleCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT StringCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT ObjectCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT SetCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT MultisetCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT SequenceCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT EloCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT TimeCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT TimestampCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT DateCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT MonetaryCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT VariableCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT SubCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT PointerCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT ErrorCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT ShortCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT VobjCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT OidCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT DbValueCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT NumericCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT BitCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT VarbitCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT CharCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT NcharCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT VarncharCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT ResultsetCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT MidkeyCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT TableCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT BigintCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT DatetimeCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT BlobCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT ClobCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT EnumerationCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT TimestamptzCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT TimestampltzCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT DatetimetzCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT DatetimeltzCmp (const DB_VALUE * x, const DB_VALUE * y);
  DB_VALUE_COMPARE_RESULT JsonCmp (const DB_VALUE * x, const DB_VALUE * y);

  DB_VALUE_COMPARE_RESULT compare_with_eqaul_type_error_temp (const DB_VALUE * value1, const DB_VALUE * value2, DB_TYPE db_type);
  
}

#endif