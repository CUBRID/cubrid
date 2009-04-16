using System;
using System.Data;
using System.Data.Common;
using System.Collections.Generic;
using System.Text;
using System.Diagnostics;

namespace Cubrid.Data.CubridClient
{
    public sealed class CubridCommand : DbCommand, ICloneable
    {
        private CubridConnection con;
        private CubridTransaction tran;
        private string sql;
        private CubridParameterCollection param;
        private bool isPrepared;
        private CubridDataReader cursor;
        private CommandType commandType;

        private int handle;
        private int resultCacheLifetime;
        private CubridStatementype statementType;
        private int bindCount;
        private bool isUpdateable;
        private int columnCount;
        private ColumnMetaData[] columnInfos;
        private CubridParameter[] parameters;
        private int cache_reusable;
        private int resultCount;
        private ResultInfo[] resultInfos;

        public CubridCommand()
        {
            param = new CubridParameterCollection();
            isPrepared = false;
        }

        public CubridCommand(CubridConnection con, int handle)
            : this()
        {
            this.con = con;
            this.handle = handle;

            GetOutResultSet(handle);
        }

        public CubridCommand(string sql)
            : this()
        {
            this.sql = sql;
        }

        public CubridCommand(string sql, CubridConnection con)
            : this(sql)
        {
            this.con = con;
        }

        public CubridCommand(string sql, CubridConnection con, CubridTransaction tran)
            : this(sql, con)
        {
            this.tran = tran;
        }

        override
        public string CommandText
        {
            get { return sql; }
            set { sql = value; }
        }

        override
        public int CommandTimeout
        {
            get { return 15; }
            set { }
        }

        override
        public CommandType CommandType
        {
            get { return commandType; }
            set { commandType = value; }
        }

        override
        protected DbConnection DbConnection
        {
            get { return con; }
            set { con = (CubridConnection)value; }
        }

        
        internal CubridConnection CubridbConnection
        {
            get { return con; }
            set { con = value; }
        }

        override
        protected DbTransaction DbTransaction
        {
            get { return null; }
            set { }
        }

        override
        public bool DesignTimeVisible
        {
            get { return false; }
            set { }
        }

        override
        public UpdateRowSource UpdatedRowSource
        {
            get { return UpdateRowSource.None; }
            set { }
        }

        override
        protected DbParameterCollection DbParameterCollection
        {
            get { return param; }
        }

        public new CubridParameterCollection Parameters
        {
            get { return param; }
        }

        private PrepareOption GetPrepareOption()
        {
            switch (commandType)
            {
                case CommandType.StoredProcedure:
                    return PrepareOption.StoredProcedureCall;
                default:
                    break;
            }

            return PrepareOption.Normal;
        }

        override
        public void Prepare()
        {
            PrepareInternal(sql, GetPrepareOption());
            param.BindCount = bindCount;
            isPrepared = true;
        }

        protected override DbParameter CreateDbParameter()
        {
            return new CubridParameter();
        }

        private void Bind()
        {
            if (isPrepared == false)
            {
                Prepare();
            }

            for (int i = 0; i < param.Count; i++)
            {
                Bind(i, (CubridParameter)param[i]);
            }
        }

        override
        public object ExecuteScalar()
        {
            Bind();
            ExecuteInternal();
            return null;
        }

        override
        public void Cancel()
        {
        }

        internal DbDataReader GetDataReaderFromStoredProcedure()
        {
            //int totalTupleNumber = con.Stream.RequestMoveCursor(handle, 0, CursorOrigin.CURSOR_SET);
            //Console.WriteLine(totalTupleNumber);
            con.Stream.RequestFetch(handle);
            int tupleCount = con.Stream.ReadInt();

            cursor = new CubridDataReader(this, handle, resultCount, columnInfos, tupleCount);
            return cursor;
        }

        protected override DbDataReader ExecuteDbDataReader(CommandBehavior behavior)
        {
            Bind();

            if (!IsQueryStatement())
            {
                throw new CubridException("Invalid Query Type for ExecuteDataReader");
            }

            ExecuteInternal();
            return cursor;
        }

        public override int ExecuteNonQuery()
        {
            Bind();

            if (IsQueryStatement())
            {
                throw new CubridException("Invalid Query Type for ExecuteNonQuery");
            }

            ExecuteInternal();

            if (statementType == CubridStatementype.CallStoredProcedure)
            {
                con.Stream.RequestFetch(handle);
                con.Stream.ReadInt(); // always 1

                int colCount = GetOutModeParameterCount() + 1;
                ResultTuple tuple = new ResultTuple(colCount);
                con.Stream.ReadResultTupleSP(tuple, colCount, con);
                //Console.WriteLine(tuple.ToString());

                int k = 1;
                for (int i = 0; i < parameters.Length; i++)
                {
                    if (parameters[i].Direction == ParameterDirection.Output ||
                        parameters[i].Direction == ParameterDirection.InputOutput)
                    {
                        parameters[i].Value = tuple[k];
                        k++;
                    }
                }
            }

            return resultCount;
        }

        private int GetOutModeParameterCount()
        {
            int count = 0;

            for (int i = 0; i < parameters.Length; i++)
            {
                if (parameters[i].Direction == ParameterDirection.Output ||
                    parameters[i].Direction == ParameterDirection.InputOutput)
                {
                    count++;
                }
            }

            return count;
        }

        public object Clone()
        {
            return null;
        }

        public void Close()
        {
            con.Stream.RequestCloseHandle(handle);
        }

        internal int BindCount
        {
            get { return bindCount; }
        }

        internal CubridStatementype StatementType
        {
            get { return statementType; }
            set { statementType = value; }
        }

        internal void PrepareInternal(string sql, PrepareOption flag)
        {
            con.ReconnectIfNeed();

            CubridStream stream = con.Stream;

            stream.RequestPrepare(sql, flag);

            handle = stream.ResponseCode;
            resultCacheLifetime = stream.ReadInt();
            statementType = (CubridStatementype)stream.ReadByte();
            bindCount = stream.ReadInt();
            isUpdateable = (stream.ReadByte() == 1);
            columnCount = stream.ReadInt();

            Debug.WriteLine("handle = " + handle);
            Debug.WriteLine("resultCacheLifetime = " + resultCacheLifetime);
            Debug.WriteLine("statementType = " + statementType);
            Debug.WriteLine("bindCount = " + bindCount);
            Debug.WriteLine("isUpdateable = " + isUpdateable);
            Debug.WriteLine("columnCount = " + columnCount);

            columnInfos = stream.ReadColumnInfo(columnCount);

            if (bindCount > 0)
            {
                parameters = new CubridParameter[bindCount];
            }

            if (statementType == CubridStatementype.CallStoredProcedure)
            {
                columnCount = bindCount + 1;
            }
        }

        internal void ExecuteInternal()
        {
            if (parameters != null && IsAllParameterBound() == false)
            {
                throw new CubridException("All parameters are not bound.");
            }

            CubridStream stream = con.Stream;
            byte[] paramModes = null;
            byte fetchFlag = 0;

            if (statementType == CubridStatementype.CallStoredProcedure && parameters != null)
            {
                paramModes = new byte[parameters.Length];
                    for (int i = 0; i < parameters.Length; i++)
                    {
                        paramModes[i] = (byte) parameters[i].Direction;
                    }
            }

            if (statementType == CubridStatementype.Select)
            {
                fetchFlag = 1;
            }

            int totalTupleCount = stream.RequestExecute(handle, ExecutionOption.Normal, parameters,
                paramModes, fetchFlag, con.AutoCommit);

            cache_reusable = stream.ReadByte();
            resultCount = stream.ReadInt();

            Debug.WriteLine("cache_reusable = {0}" + cache_reusable);
            Debug.WriteLine("resultCount = {0}" + resultCount);

            resultInfos = stream.ReadResultInfo(resultCount);

            if (statementType == CubridStatementype.Select)
            {
                int fetchCode = stream.ReadInt();
                int tupleCount = stream.ReadInt();
                cursor = new CubridDataReader(this, handle, totalTupleCount, columnInfos, tupleCount);
            }
        }

        public void ExecuteBatch()
        {
        }

        public void ExecuteArray()
        {
        }

        internal bool NextResult()
        {
            CubridStream stream = con.Stream;

            int totalTupleCount = stream.RequestNextResult(handle);

            CubridStatementype commandTypeIs = (CubridStatementype)stream.ReadByte();
            bool isUpdatable = (stream.ReadByte() == 1) ? true : false;
            int columnNumber = stream.ReadInt();
            columnInfos = stream.ReadColumnInfo(columnNumber);


            if (commandTypeIs == CubridStatementype.Select)
            {
                cursor = new CubridDataReader(this, handle, totalTupleCount, columnInfos);
            }

            return true;
        }

        private bool IsAllParameterBound()
        {
            for (int i = 0; i < parameters.Length; i++)
            {
                if (parameters[i] == null)
                {
                    return false;
                }
            }

            return true;
        }

        internal void Bind(int index, CubridDataType type, object value)
        {
            parameters[index] = new CubridParameter();
            parameters[index].Type = type;
            parameters[index].Value = value;
        }

        internal void Bind(int index, CubridDataType type, object value, ParameterDirection mode)
        {
            Bind(index, type, value);
            parameters[index].Mode = mode;
        }

        internal void Bind(int index, CubridParameter bindParameter)
        {
            parameters[index] = bindParameter;
        }

        internal void GetOutResultSet(int handle)
        {
            CubridStream stream = con.Stream;

            this.handle = stream.RequestOutResultSet(handle); //TODO: check to need to free old handle

            statementType = (CubridStatementype)stream.ReadByte();
            resultCount = stream.ReadInt();
            isUpdateable = (stream.ReadByte() == 1);
            columnCount = stream.ReadInt();

            Debug.WriteLine("handle = " + handle);
            Debug.WriteLine("statementType = " + statementType);
            Debug.WriteLine("isUpdateable = " + isUpdateable);
            Debug.WriteLine("columnCount = " + columnCount);

            columnInfos = stream.ReadColumnInfo(columnCount);
        }

        private bool IsQueryStatement()
        {
            switch (statementType)
            {
                case CubridStatementype.Select:
                case CubridStatementype.Call:
                case CubridStatementype.GetStats:
                case CubridStatementype.Evaluate:
                    return true;

                default:
                    break;
            }

            return false;
        }
    }


    //TODO: get more results, next fetch, prepare call, sp resultset, oid
    //TODO: column info cache

    internal class ResultInfo
    {
        private CubridStatementype stmtType;
        private int resultCount;
        private CubridOid oid;
        private int cacheTimeSec;
        private int cacheTImeUsec;

        public CubridStatementype StmtType
        {
            set { stmtType = value; }
            get { return stmtType; }
        }

        public int ResultCount
        {
            set { resultCount = value; }
            get { return resultCount; }
        }

        public CubridOid Oid
        {
            set { oid = value; }
            get { return oid; }
        }

        public int CacheTimeSec
        {
            set { cacheTimeSec = value; }
            get { return cacheTimeSec; }
        }

        public int CacheTImeUsec
        {
            set { cacheTImeUsec = value; }
            get { return cacheTImeUsec; }
        }
    }

    internal class ColumnMetaData
    {
        private CubridDataType type;
        private CubridDataType collectionElementType;
        private int scale;
        private int precision;
        private string name;
        private string table;
        private string realName;
        private bool isNull;

        public CubridDataType Type
        {
            get { return type; }
            set { CheckType(value); }
        }

        private void CheckType(CubridDataType type)
        {
            int collectionType = 0;
            collectionType = ((byte)type) & ((byte)0x60);

            Debug.WriteLine("collectionTypeOrNot = {0}" + collectionType);

            switch (collectionType)
            {
                case 0x00:
                    this.type = type;
                    collectionElementType = CubridDataType.Null;
                    break;
                case 0x20:
                    this.type = CubridDataType.Set;
                    collectionElementType = (CubridDataType)(((byte)type) & ((byte)0x1F));
                    break;
                case 0x40:
                    this.type = CubridDataType.Multiset;
                    collectionElementType = (CubridDataType)(((byte)type) & ((byte)0x1F));
                    break;
                case 0x60:
                    this.type = CubridDataType.Sequence;
                    collectionElementType = (CubridDataType)(((byte)type) & ((byte)0x1F));
                    break;
                default:
                    this.type = CubridDataType.Null;
                    collectionElementType = CubridDataType.Null;
                    break;
            }
        }

        public int Scale
        {
            get { return scale; }
            set { scale = value; }
        }

        public int Precision
        {
            get { return precision; }
            set { precision = value; }
        }

        public string Name
        {
            get { return name; }
            set { name = value; }
        }

        public string Table
        {
            get { return table; }
            set { table = value; }
        }

        public string RealName
        {
            get { return realName; }
            set { realName = value; }
        }

        public bool IsNull
        {
            get { return isNull; }
            set { isNull = value; }
        }

        public override string ToString()
        {
            StringBuilder builder = new StringBuilder();

            builder.Append("ColInfo: type = " + type);
            builder.Append(", scale = " + scale);
            builder.Append(", precision = " + precision);
            builder.Append(", name = " + name);
            builder.Append(", table = " + table);
            builder.Append(", isNull = " + isNull + "\n");

            return builder.ToString();
        }
    }

    internal class BatchResult
    {
        private int result;
        private StmtType stmtType;
        private int errorCode;
        private string errorMessage;

        public int Result
        {
            set { result = value; }
            get { return result; }
        }

        public StmtType StmtType
        {
            set { stmtType = value; }
            get { return stmtType; }
        }

        public int ErrorCode
        {
            set { errorCode = value; }
            get { return errorCode; }
        }

        public string ErrorMessage
        {
            set { errorMessage = value; }
            get { return errorMessage; }
        }

    }
}
