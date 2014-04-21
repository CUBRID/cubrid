using System;
using System.Data.Common;

namespace Cubrid.Data.CubridClient
{
    public sealed class CubridException : DbException
    {
        public CubridException(string message) : base (message)
        {
        }
    }

    enum ErrorCode
    {
        ER_NO_ERROR = 0,
        ER_NOT_OBJECT = 1,
        ER_DBMS = 2,
        ER_COMMUNICATION = 3,
        ER_NO_MORE_DATA = 4,
        ER_TYPE_CONVERSION = 5,
        ER_BIND_INDEX = 6,
        ER_NOT_BIND = 7,
        ER_WAS_NULL = 8,
        ER_COLUMN_INDEX = 9,
        ER_TRUNCATE = 10,
        ER_SCHEMA_TYPE = 11,
        ER_FILE = 12,
        ER_CONNECTION = 13,
        ER_ISO_TYPE = 14,
        ER_ILLEGAL_REQUEST = 15,
        ER_INVALID_ARGUMENT = 16,
        ER_IS_CLOSED = 17,
        ER_ILLEGAL_FLAG = 18,
        ER_ILLEGAL_DATA_SIZE = 19,
        ER_NO_MORE_RESULT = 20,
        ER_OID_IS_NOT_INCLUDED = 21,
        ER_CMD_IS_NOT_INSERT = 22,
        ER_UNKNOWN = 23,

        /* CAS Error Code */

        CAS_ER_DBMS = -1000,
        CAS_ER_INTERNAL = -1001,
        CAS_ER_NO_MORE_MEMORY = -1002,
        CAS_ER_COMMUNICATION = -1003,
        CAS_ER_ARGS = -1004,
        CAS_ER_TRAN_TYPE = -1005,
        CAS_ER_SRV_HANDLE = -1006,
        CAS_ER_NUM_BIND = -1007,
        CAS_ER_UNKNOWN_U_TYPE = -1008,
        CAS_ER_DB_VALUE = -1009,
        CAS_ER_TYPE_CONVERSION = -1010,
        CAS_ER_PARAM_NAME = -1011,
        CAS_ER_NO_MORE_DATA = -1012,
        CAS_ER_OBJECT = -1013,
        CAS_ER_OPEN_FILE = -1014,
        CAS_ER_SCHEMA_TYPE = -1015,
        CAS_ER_VERSION = -1016,
        CAS_ER_FREE_SERVER = -1017,
        CAS_ER_NOT_AUTHORIZED_CLIENT = -1018,
        CAS_ER_QUERY_CANCEL = -1019,
        CAS_ER_NOT_COLLECTION = -1020,
        CAS_ER_COLLECTION_DOMAIN = -1021,
        CAS_ER_NO_MORE_RESULT_SET = -1022,
        CAS_ER_INVALID_CALL_STMT = -1023,
        CAS_ER_STMT_POOLING = -1024,
        CAS_ER_NOT_IMPLEMENTED = -1100,
        CAS_ER_IS = -1200
    }

}
