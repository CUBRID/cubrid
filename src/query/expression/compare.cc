#include "expression/compare.h"
#include "dbtype.h"
#include "dbtype_def.h"

#define CMP(d1, d2)                                     \
     ((d1) < (d2) ? DB_LT : (d1) > (d2) ? DB_GT : DB_EQ)

namespace Compare {


DB_VALUE_COMPARE_RESULT NoneCmp(const DB_VALUE *x, const DB_VALUE *y) {
    assert(false); // TODO
    return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT IntegerCmp(const DB_VALUE *x, const DB_VALUE *y) {
    int i1, i2;

    i1 = db_get_int(x);
    i2 = db_get_int(y);

    return CMP (i1, i2);
}
DB_VALUE_COMPARE_RESULT FloatCmp(const DB_VALUE *x, const DB_VALUE *y) {
    assert(false); // TODO
    return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT DoubleCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT StringCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT ObjectCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT SetCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT MultisetCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT SequenceCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT EloCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT TimeCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT TimestampCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT DateCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT MonetaryCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT VariableCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT SubCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT PointerCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT ErrorCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT ShortCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT VobjCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT OidCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT DbValueCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT NumericCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT BitCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT VarbitCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT CharCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT NcharCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}  
DB_VALUE_COMPARE_RESULT VarncharCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT ResultsetCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT MidkeyCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT TableCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT BigintCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT DatetimeCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT BlobCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT ClobCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT EnumerationCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT TimestamptzCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT TimestampltzCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT DatetimetzCmp(const DB_VALUE *x, const DB_VALUE *y) {
assert(false); // TODO
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT DatetimeltzCmp(const DB_VALUE *x, const DB_VALUE *y) {
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}
DB_VALUE_COMPARE_RESULT JsonCmp(const DB_VALUE *x, const DB_VALUE *y) {
return DB_VALUE_COMPARE_RESULT::DB_UNK;
}

static Cmpfunc CompareTable[static_cast < int >(DB_TYPE::DB_TYPE_LAST) + 1] = {
    NoneCmp,            // DB_TYPE_FIRST, DB_TYPE_UNKNOWN, DB_TYPE_NULL = 0
    IntegerCmp,         // DB_TYPE_INTEGER = 1
    FloatCmp,           // DB_TYPE_FLOAT = 2
    DoubleCmp,          // DB_TYPE_DOUBLE = 3
    StringCmp,          // DB_TYPE_STRING, DB_TYPE_VARCHAR = 4
    ObjectCmp,          // DB_TYPE_OBJECT = 5
    SetCmp,             // DB_TYPE_SET = 6
    MultisetCmp,        // DB_TYPE_MULTISET = 7
    SequenceCmp,        // DB_TYPE_SEQUENCE, DB_TYPE_LIST = 8
    EloCmp,             // DB_TYPE_ELO = 9
    TimeCmp,            // DB_TYPE_TIME = 10
    TimestampCmp,       // DB_TYPE_TIMESTAMP , DB_TYPE_UTIME = 11
    DateCmp,            // DB_TYPE_DATE = 12
    MonetaryCmp,        // DB_TYPE_MONETARY = 13
    VariableCmp,        // DB_TYPE_VARIABLE = 14
    SubCmp,             // DB_TYPE_SUB = 15
    PointerCmp,         // DB_TYPE_POINTER = 16
    ErrorCmp,           // DB_TYPE_ERROR = 17
    ShortCmp,           // DB_TYPE_SHORT, DB_TYPE_SMALLINT = 18
    VobjCmp,            // DB_TYPE_VOBJ = 19
    OidCmp,             // DB_TYPE_OID = 20
    DbValueCmp,         // DB_TYPE_DB_VALUE = 21
    NumericCmp,         // DB_TYPE_NUMERIC = 22
    BitCmp,             // DB_TYPE_BIT = 23
    VarbitCmp,          // DB_TYPE_VARBIT = 24
    CharCmp,            // DB_TYPE_CHAR = 25
    NcharCmp,           // DB_TYPE_NCHAR = 26
    VarncharCmp,        // DB_TYPE_VARNCHAR = 27
    ResultsetCmp,       // DB_TYPE_RESULTSET = 28
    MidkeyCmp,          // DB_TYPE_MIDKEY = 29
    TableCmp,           // DB_TYPE_TABLE = 30
    BigintCmp,          // DB_TYPE_BIGINT = 31
    DatetimeCmp,        // DB_TYPE_DATETIME = 32
    BlobCmp,            // DB_TYPE_BLOB = 33
    ClobCmp,            // DB_TYPE_CLOB = 34
    EnumerationCmp,     // DB_TYPE_ENUMERATION = 35
    TimestamptzCmp,     // DB_TYPE_TIMESTAMPTZ = 36
    TimestampltzCmp,    // DB_TYPE_TIMESTAMPLTZ = 37
    DatetimetzCmp,      // DB_TYPE_DATETIMETZ = 38
    DatetimeltzCmp,     // DB_TYPE_DATETIMELTZ = 39
    JsonCmp,            // DB_TYPE_JSON = 40
};

DB_VALUE_COMPARE_RESULT
compare_with_eqaul_type_error_temp (const DB_VALUE * value1, const DB_VALUE * value2, DB_TYPE db_type)
{
    Compare::Cmpfunc cmp_func = (Cmpfunc)CompareTable[static_cast<int>(db_type)];
    return cmp_func(value1, value2);
}

} // namespace Compare