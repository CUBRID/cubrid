#include "parse_type.hpp"
#include "string_buffer.hpp"

const char* str(pt_generic_type_enum type)
{
    static const char* arr[] = {
        "GT_NONE",
        "GT_STRING",
        "GT_STRING_VARYING",
        "GT_CHAR",
        "GT_NCHAR",
        "GT_BIT",
        "GT_DISCRETE_NUMBER",
        "GT_NUMBER",
        "GT_DATE",
        "GT_DATETIME",
        "GT_SEQUENCE",
        "GT_LOB",
        "GT_QUERY",
        "GT_PRIMITIVE",
        "GT_ANY",
        "GT_JSON_VAL",
        "GT_JSON_DOC",
        "GT_JSON_PATH",
    };
    return arr[type];
}

//--------------------------------------------------------------------------------
const char* str(PT_TYPE_ENUM type)
{
    switch(type)
    {
        case PT_MIN:
            return "MIN";
        case PT_MAX:
            return "MAX";
        case PT_SUM:
            return "SUM";
        case PT_AVG:
            return "AVG";
        case PT_STDDEV:
            return "STDDEV";
        case PT_VARIANCE:
            return "VARIANCE";
        case PT_STDDEV_POP:
            return "STDDEV_POP";
        case PT_VAR_POP:
            return "VAR_POP";
        case PT_STDDEV_SAMP:
            return "STDDEV_SAMP";
        case PT_VAR_SAMP:
            return "VAR_SAMP";
        case PT_COUNT:
            return "COUNT";
        case PT_COUNT_STAR:
            return "COUNT_STAR";
        case PT_GROUPBY_NUM:
            return "GROUPBY_NUM";
        case PT_AGG_BIT_AND:
            return "AGG_BIT_AND";
        case PT_AGG_BIT_OR:
            return "AGG_BIT_OR";
        case PT_AGG_BIT_XOR:
            return "AGG_BIT_XOR";
        case PT_GROUP_CONCAT:
            return "GROUP_CONCAT";
        case PT_ROW_NUMBER:
            return "ROW_NUMBER";
        case PT_RANK:
            return "RANK";
        case PT_DENSE_RANK:
            return "DENSE_RANK";
        case PT_NTILE:
            return "NTILE";
        case PT_TOP_AGG_FUNC:
            return "TOP_AGG_FUNC";
        case PT_LEAD:
            return "LEAD";
        case PT_LAG:
            return "LAG";
        case PT_GENERIC:
            return "GENERIC";
        case F_SET:
            return "SET";
        case F_TABLE_SET:
            return "TABLE_SET";
        case F_MULTISET:
            return "MULTISET";
        case F_TABLE_MULTISET:
            return "TABLE_MULTISET";
        case F_SEQUENCE:
            return "SEQUENCE";
        case F_TABLE_SEQUENCE:
            return "TABLE_SEQUENCE";
        case F_TOP_TABLE_FUNC:
            return "TOP_TABLE_FUNC";
        case F_MIDXKEY:
            return "MIDXKEY";
        case F_VID:
            return "VID";
        case F_GENERIC:
            return "GENERIC";
        case F_CLASS_OF:
            return "CLASS_OF";
        case F_INSERT_SUBSTRING:
            return "INSERT_SUBSTRING";
        case F_ELT:
            return "ELT";
        case F_JSON_OBJECT:
            return "JSON_OBJECT";
        case F_JSON_ARRAY:
            return "JSON_ARRAY";
        case F_JSON_MERGE:
            return "JSON_MERGE";
        case F_JSON_INSERT:
            return "JSON_INSERT";
        case F_JSON_REMOVE:
            return "JSON_REMOVE";
        case F_JSON_ARRAY_APPEND:
            return "JSON_ARRAY_APPEND";
        case F_JSON_GET_ALL_PATHS:
            return "JSON_GET_ALL_PATHS";
        case F_JSON_REPLACE:
            return "JSON_REPLACE";
        case F_JSON_SET:
            return "JSON_SET";
        case F_JSON_KEYS:
            return "JSON_KEYS";
        case PT_FIRST_VALUE:
            return "FIRST_VALUE";
        case PT_LAST_VALUE:
            return "LAST_VALUE";
        case PT_NTH_VALUE:
            return "NTH_VALUE";
        case PT_MEDIAN:
            return "MEDIAN";
        case PT_CUME_DIST:
            return "CUME_DIST";
        case PT_PERCENT_RANK:
            return "PERCENT_RANK";
        case PT_PERCENTILE_CONT:
            return "PERCENTILE_CONT";
        case PT_PERCENTILE_DISC:
            return "PERCENTILE_DISC";
        default:
            assert(false);
            return nullptr;
    }
}

//--------------------------------------------------------------------------------
const char* str(const pt_arg_type& type, string_buffer& sb){
    switch(type.type){
        case pt_arg_type::NORMAL:
            sb("%d", type.val.index);
            break;
        case pt_arg_type::GENERIC:
            sb("%s", str(type.val.generic_type));
            break;
        case pt_arg_type::INDEX:
            sb("IDX%d", type.val.index);
            break;
    }
    return sb.get_buffer();
}
