
namespace Cubrid.Data.CubridClient
{
    public enum FunctionCode
    {
        EndTransaction = 1,
        Prepare = 2,
        Execute = 3,
        GetDbParameter = 4,
        SetDbParameter = 5,
        CloseStatement = 6,
        Cursor = 7,
        Fetch = 8,
        GetSchemaInfo = 9,
        GetByOid = 10,
        PutByOid = 11,
        GloNew = 12,
        GloSave = 13,
        GloLoad = 14,
        GetDbVersion = 15,
        GetClassNumberObjects = 16,
        RelatedToOid = 17,
        RelatedToCollection = 18,

        /* since 2.0 */
        NextResult = 19,
        ExecuteBatch = 20,
        ExecuteBatchPrepared = 21,
        CursorUpdate = 22,
        GetQueryInfo = 24,

        /* since 3.0 */
        GloCommand = 25,
        Savepoint = 26,
        GetParameterInfo = 27,
        XaPrepare = 28,
        XaRecover = 29,
        XaEndTransaction = 30,

        CloseConnection = 31,
        CheckCas = 32,

        MakeOutResultSet = 33,

        GetGeneratedKeys = 34
    }

    public enum CubridStatementype
    {
        AlterTable = 0,
        AlterSerial,
        Commit,
        RegisterDatabase,
        CreateTable,
        CreateIndex,
        CreateTrigger,
        CreateSerial,
        DropDatabase,
        DropTable,
        DropIndex,
        DropLabel,
        DropTrigger,
        DropSerial,
        Evaluate,
        RenameTable,
        Rollback,
        Grant,
        Revoke,
        UpdateStats,
        Insert,
        Select,
        Update,
        Delete,
        Call,
        GetIsolation,
        GetTimeout,
        GetOption,
        SetOption,
        Scope,
        GetTrigger,
        SetTrigger,
        Savepoint,
        Prepare,
        Attach,
        Use,
        RemoveTrigger,
        RenameTrigger,
        OnLdb,
        Getldb,
        SetLdb,
        GetStats,

        CallStoredProcedure = 0x7e,
        Unknown = 0x7f
    }

    public enum CubridIsolation
    {
        Unknown = 0,
        CommitClass_UncommitInstance = 1,
        CommitClass_CommitInstance = 2,
        /* TRAN_READ_UNCOMMITTED */
        RepeatableClass_UncommitInstance = 3,
        /* TRAN_READ_COMMITTED */
        RepeatableClass_CommitInstance = 4,
        /* TRAN_SERIALIZABLE */
        RepeatableClass_RepeatableInstance = 5,
        Serializable = 6
    }

    public enum CubridDataType
    {
        Null = 0,
        Char = 1,
        String = 2,
        Varchar = 2,
        Nchar = 3,
        Varnchar = 4,
        Bit = 5,
        Varbit = 6,
        Numeric = 7,
        Decimal = 7,
        Int = 8,
        Short = 9,
        Monetary = 10,
        Float = 11,
        Double = 12,
        Date = 13,
        Time = 14,
        Timestamp = 15,
        Set = 16,
        Multiset = 17,
        Sequence = 18,
        Object = 19,
        ResultSet = 20
    }

    public enum SchemaType
    {
        SCH_CLASS = 1,
        SCH_VCLASS,
        SCH_QUERY_SPEC,
        SCH_ATTRIBUTE,
        SCH_CLASS_ATTRIBUTE,
        SCH_METHOD,
        SCH_CLASS_METHOD,
        SCH_METHOD_FILE,
        SCH_SUPERCLASS,
        SCH_SUBCLASS,
        SCH_CONSTRAINT,
        SCH_TRIGGER,
        SCH_CLASS_PRIVILEGE,
        SCH_ATTR_PRIVILEGE,
        SCH_DIRECT_SUPER_CLASS,
        SCH_PRIMARY_KEY
    }

    public enum PrepareOption
    {
        Normal = 0x00,
        IncludeOid = 0x01,
        Updateable = 0x02,
        StoredProcedureCall = 0x40
    }

    public enum OidCommand
    {
        DROP_BY_OID = 1,
        IS_INSTANCE = 2,
        GET_READ_LOCK_BY_OID = 3,
        GET_WRITE_LOCK_BY_OID = 4,
        GET_CLASS_NAME_BY_OID = 5,
        IS_GLO_INSTANCE = 6
    }

    public enum GloCommand
    {
        READ_DATA = 1,
        WRITE_DATA = 2,
        INSERT_DATA = 3,
        DELETE_DATA = 4,
        TRUNCATE_DATA = 5,
        APPEND_DATA = 6,
        DATA_SIZE = 7,
        COMPRESS_DATA = 8,
        DESTROY_DATA = 9,
        LIKE_SEARCH = 10,
        REG_SEARCH = 11,
        BINARY_SEARCH = 12
    }

    public enum GloNewType
    {
        LO = 1,
        FBO = 2
    }

    public enum EndTransactionType
    {
        Commit = 1,
        Rollback = 2
    }

    public enum DbParam
    {
        DB_PARAM_ISOLATION_LEVEL = 1,
        DB_PARAM_LOCK_TIMEOUT = 2
    }

    public enum CollectionCommand
    {
        GET_COLLECTION_VALUE = 1,
        GET_SIZE_OF_COLLECTION = 2,
        DROP_ELEMENT_IN_SET = 3,
        ADD_ELEMENT_TO_SET = 4,
        DROP_ELEMENT_IN_SEQUENCE = 5,
        INSERT_ELEMENT_INTO_SEQUENCE = 6,
        PUT_ELEMENT_ON_SEQUENCE = 7
    }

    public enum LockTimeout
    {
        LOCK_TIMEOUT_NOT_USED = -2,
        LOCK_TIMEOUT_INFINITE = -1
    }

    public enum TransactionState
    {
        CON_STATUS_OUT_TRAN = 0,
        CON_STATUS_IN_TRAN = 1
    }

    public enum CursorOrigin
    {
        CURSOR_SET = 0,
        CURSOR_CUR = 1,
        CURSOR_END = 2
    }

    public enum StmtType
    {
        NORMAL = 0,
        GET_BY_OID = 1,
        GET_SCHEMA_INFO = 2,
        GET_AUTOINCREMENT_KEYS = 3
    }

    public enum ExecutionType
    {
        NORMAL_EXECUTE = 0,
        ASYNC_EXECUTE = 1
    }

    public enum ExecutionOption
    {
        Normal = 0x00,
        Async = 0x01,
        QueryAll = 0x02,
        QueryInfo = 0x04,
        QueryPlanOnly = 0x08
    }
}
