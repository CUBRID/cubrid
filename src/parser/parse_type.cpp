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
const char* str(pt_type_enum type)
{
    switch(type)
    {
        case PT_TYPE_NONE:
            return "PT_TYPE_NONE";
        case PT_TYPE_INTEGER:
            return "PT_TYPE_INTEGER";
        case PT_TYPE_FLOAT:
            return "PT_TYPE_FLOAT";
        case PT_TYPE_DOUBLE:
            return "PT_TYPE_DOUBLE";
        case PT_TYPE_SMALLINT:
            return "PT_TYPE_SMALLINT";
        case PT_TYPE_DATE:
            return "PT_TYPE_DATE";
        case PT_TYPE_TIME:
            return "PT_TYPE_TIME";
        case PT_TYPE_TIMESTAMP:
            return "PT_TYPE_TIMESTAMP";
        case PT_TYPE_DATETIME:
            return "PT_TYPE_DATETIME";
        case PT_TYPE_MONETARY:
            return "PT_TYPE_MONETARY";
        case PT_TYPE_NUMERIC:
            return "PT_TYPE_NUMERIC";
        case PT_TYPE_CHAR:
            return "PT_TYPE_CHAR";
        case PT_TYPE_VARCHAR:
            return "PT_TYPE_VARCHAR";
        case PT_TYPE_NCHAR:
            return "PT_TYPE_NCHAR";
        case PT_TYPE_VARNCHAR:
            return "PT_TYPE_VARNCHAR";
        case PT_TYPE_BIT:
            return "PT_TYPE_BIT";
        case PT_TYPE_VARBIT:
            return "PT_TYPE_VARBIT";
        case PT_TYPE_LOGICAL:
            return "PT_TYPE_LOGICAL";
        case PT_TYPE_MAYBE:
            return "PT_TYPE_MAYBE";
        case PT_TYPE_JSON:
            return "PT_TYPE_JSON";
        case PT_TYPE_NA:
            return "PT_TYPE_NA";
        case PT_TYPE_NULL:
            return "PT_TYPE_NULL";
        case PT_TYPE_STAR:
            return "PT_TYPE_STAR";
        case PT_TYPE_OBJECT:
            return "PT_TYPE_OBJECT";
        case PT_TYPE_SET:
            return "PT_TYPE_SET";
        case PT_TYPE_MULTISET:
            return "PT_TYPE_MULTISET";
        case PT_TYPE_SEQUENCE:
            return "PT_TYPE_SEQUENCE";
        case PT_TYPE_MIDXKEY:
            return "PT_TYPE_MIDXKEY";
        case PT_TYPE_COMPOUND:
            return "PT_TYPE_COMPOUND";
        case PT_TYPE_EXPR_SET:
            return "PT_TYPE_EXPR_SET";
        case PT_TYPE_RESULTSET:
            return "PT_TYPE_RESULTSET";
        case PT_TYPE_BIGINT:
            return "PT_TYPE_BIGINT";
        case PT_TYPE_BLOB:
            return "PT_TYPE_BLOB";
        case PT_TYPE_CLOB:
            return "PT_TYPE_CLOB";
        case PT_TYPE_ELO:
            return "PT_TYPE_ELO";
        case PT_TYPE_ENUMERATION:
            return "PT_TYPE_ENUMERATION";
        case PT_TYPE_TIMESTAMPLTZ:
            return "PT_TYPE_TIMESTAMPLTZ";
        case PT_TYPE_TIMESTAMPTZ:
            return "PT_TYPE_TIMESTAMPTZ";
        case PT_TYPE_DATETIMETZ:
            return "PT_TYPE_DATETIMETZ";
        case PT_TYPE_DATETIMELTZ:
            return "PT_TYPE_DATETIMELTZ";
        case PT_TYPE_MAX:
            return "PT_TYPE_MAX";
        case PT_TYPE_TIMETZ:
            return "PT_TYPE_TIMETZ";
        case PT_TYPE_TIMELTZ:
            return "PT_TYPE_TIMELTZ";
        default:
            assert(false);
            return nullptr;
    }
}

//--------------------------------------------------------------------------------
const char* str(const pt_arg_type& type, string_buffer& sb){
    switch(type.type){
        case pt_arg_type::NORMAL:
            sb("%s", str(type.val.type));
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
