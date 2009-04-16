using System;
using System.Data;
using System.Data.Common;
using System.Collections;
using System.Diagnostics;
using System.Text;
using System.Collections.Generic;

namespace Cubrid.Data.CubridClient
{
    public sealed class CubridDataReader : DbDataReader
    {
        private CubridConnection con;
        private CubridCommand stmt;
        private ResultTuple resultTuple;
        private int current;
        private int resultCount;
        private ColumnMetaData[] columnMetaData;
        private int handle;
        private int tupleCount = 0;

        public override int Depth { get { return 1; } }
        
        public override int FieldCount 
        {
            get { return GetColumnCount(); }
        }
        
        public override bool HasRows { get { return false; } }
        
        public override bool IsClosed { get { return false; } }
        
        public override int RecordsAffected { get { return 1; } }
        
        public override int VisibleFieldCount { get { return 1; } }

        public override object this[int ordinal] 
        { 
            get { return GetObject(ordinal); }
        }
        
        public override object this[string name] 
        {
            get { return GetObject(name); } 
        }

        internal CubridDataReader(CubridCommand stmt, int handle, int count, ColumnMetaData[] columnInfos)
        {
            this.stmt = stmt;
            this.con = (CubridConnection) stmt.Connection;
            this.handle = handle;
            resultCount = count;
            this.columnMetaData = columnInfos;
            current = 0;
            resultTuple = new ResultTuple(columnInfos.Length);
        }

        internal CubridDataReader(CubridCommand stmt, int handle, int count, ColumnMetaData[] columnInfos, int tupleCount)
        {
            this.stmt = stmt;
            this.con = (CubridConnection)stmt.Connection;
            this.handle = handle;
            resultCount = count;
            this.columnMetaData = columnInfos;
            current = 0;
            this.tupleCount = tupleCount;
            resultTuple = new ResultTuple(columnInfos.Length);
        }

        public override void Close()
        {
        }

        protected override void Dispose(bool disposing)
        {
        }

        public override bool GetBoolean(int ordinal)
        {
            return false;
        }

        public override byte GetByte(int ordinal)
        {
            return 0;
        }

        public override long GetBytes(int ordinal, long dataOffset, byte[] buffer, int bufferOffset, int length)
        {
            return 0;
        }

        public override char GetChar(int ordinal)
        {
            return ' ';
        }

        public override long GetChars(int ordinal, long dataOffset, char[] buffer, int bufferOffset, int length)
        {
            return 0;
        }

        public override string GetDataTypeName(int ordinal)
        {
            return GetColumnTypeName(ordinal);
        }

        public override Type GetFieldType(int ordinal)
        {
            return GetColumnType(ordinal);
        }

        public override DateTime GetDateTime(int ordinal)
        {
            return Convert.ToDateTime(GetObject(ordinal));
        }

        protected override DbDataReader GetDbDataReader(int ordinal)
        {
            return null;
        }

        public override decimal GetDecimal(int ordinal)
        {
            return 0;
        }

        public override double GetDouble(int ordinal)
        {
            return Convert.ToDouble(GetObject(ordinal));
        }

        public override IEnumerator GetEnumerator()
        {
            return null;
        }

        public override float GetFloat(int ordinal)
        {
            return Convert.ToSingle(GetObject(ordinal));
        }

        public override Guid GetGuid(int ordinal)
        {
            return Guid.Empty;
        }

        public override short GetInt16(int ordinal)
        {
            return GetShort(ordinal);
        }

        public override int GetInt32(int ordinal)
        {
            return GetInt(ordinal);
        }

        public override long GetInt64(int ordinal)
        {
            return 0;
        }

        public override string GetName(int ordinal)
        {
            return GetColumnName(ordinal);
        }

        public override int GetOrdinal(string name)
        {
            return 0;
        }

        public override Type GetProviderSpecificFieldType(int ordinal)
        {
            return null;
        }

        public override object GetProviderSpecificValue(int ordinal)
        {
            return null;
        }

        public override int GetProviderSpecificValues(object[] values)
        {
            return 0;
        }

        public override DataTable GetSchemaTable()
        {
            return null;
        }

        public override string GetString(int ordinal)
        {
            return Convert.ToString(GetObject(ordinal));
        }

        public DbDataReader GetDataReader(int ordinal)
        {
            return (CubridDataReader) GetObject(ordinal);
        }

        public override object GetValue(int ordinal)
        {
            return GetObject(ordinal);
        }

        public override int GetValues(object[] values)
        {
            return GetAllValues(values);
        }

        public override bool IsDBNull(int ordinal)
        {
            return IsNull(ordinal);
        }

        public override bool NextResult()
        {
            return false;
        }

        public override bool Read()
        {
            return Next();
        }

        public bool Next()
        {
            current++;

            if (current > resultCount)
            {
                return false;
            }

            if (current > tupleCount)
            {
                /* TODO: refetch */
                /* move server side cursor and fetch */
                // must cursor to be moved???
            }

            con.Stream.ReadResultTuple(resultTuple, columnMetaData, stmt.StatementType, con);

            return true;
        }

        private void MoveCursor(int offset, CursorOrigin origin)
        {
            CubridStream stream = con.Stream;

            int totalTupleNumber = stream.RequestMoveCursor(handle, offset, origin);
        }

        private void Fetch()
        {
            CubridStream stream = con.Stream;

            tupleCount += stream.RequestFetch(handle);
        }

        public object GetObject(int index)
        {
            return resultTuple[index];
        }

        public object GetObject(string name)
        {
            return resultTuple[name];
        }

        public short GetShort(int index)
        {
            return Convert.ToInt16(GetObject(index));
        }

        public short GetShort(string name)
        {
            return Convert.ToInt16(GetObject(name));
        }

        public int GetInt(int index)
        {
            return Convert.ToInt32(GetObject(index));
        }

        public int GetInt(string name)
        {
            return Convert.ToInt32(GetObject(name));
        }

        public float GetFloat(string name)
        {
            return Convert.ToSingle(GetObject(name));
        }

        public double GetDouble(string name)
        {
            return Convert.ToDouble(GetObject(name));
        }

        public string GetString(string name)
        {
            return Convert.ToString(GetObject(name));
        }

        public DateTime GetTime(int index)
        {
            return GetDateTime(index);
        }

        public DateTime GetTime(string name)
        {
            return GetDateTime(name);
        }

        public DateTime GetDate(int index)
        {
            return GetDateTime(index);
        }

        public DateTime GetDate(string name)
        {
            return GetDateTime(name);
        }

        public DateTime GetDateTime(string name)
        {
            return Convert.ToDateTime(GetObject(name));
        }

        public CubridOid GetOid(int index)
        {
            return (CubridOid)GetObject(index);
        }

        public CubridOid GetOid(string name)
        {
            return (CubridOid)GetObject(name);
        }

        public object[] GetArray(int index)
        {
            return (object[])GetObject(index);
        }

        public object[] GetArray(string name)
        {
            return (object[])GetObject(name);
        }

        public string GetColumnName(int index)
        {
            return columnMetaData[index].Name;
        }

        public string GetColumnTypeName(int index)
        {
            switch (columnMetaData[index].Type)
            {
                case CubridDataType.Char:
                    return "CHAR";

                case CubridDataType.Nchar:
                    return "NCHAR";

                case CubridDataType.String:
                    return "STRING";

                case CubridDataType.Varnchar:
                    return "VARCHAR";

                case CubridDataType.Short:
                    return "SHORT";

                case CubridDataType.Int:
                    return "INT";

                case CubridDataType.Float:
                    return "FLOAT";

                case CubridDataType.Double:
                    return "DOUBLE";

                case CubridDataType.Monetary:
                    return "MONETARY";

                case CubridDataType.Date:
                    return "DATE";

                case CubridDataType.Time:
                    return "TIME";

                case CubridDataType.Timestamp:
                    return "TIMESTAMP";

                case CubridDataType.Object:
                    return "OBJECT";

                case CubridDataType.Bit:
                    return "BIT";

                case CubridDataType.Varbit:
                    return "VARBIT";

                case CubridDataType.Set:
                    return "SET";

                case CubridDataType.Multiset:
                    return "MULTISET";

                case CubridDataType.Sequence:
                    return "SEQUENCE";

                default:
                    return null;
            }
        }

        public Type GetColumnType(int index)
        {
            switch (columnMetaData[index].Type)
            {
                case CubridDataType.Char:
                case CubridDataType.Nchar:
                case CubridDataType.Varnchar:
                case CubridDataType.String:
                    return System.Type.GetType("string");

                case CubridDataType.Short:
                    return System.Type.GetType("short");

                case CubridDataType.Int:
                    return System.Type.GetType("int");

                case CubridDataType.Float:
                    return System.Type.GetType("float");

                case CubridDataType.Double:
                case CubridDataType.Monetary:
                    return System.Type.GetType("double");

                case CubridDataType.Date:
                    return System.Type.GetType("date");

                case CubridDataType.Time:
                    return System.Type.GetType("time");

                case CubridDataType.Timestamp:
                    return System.Type.GetType("datetime");

                case CubridDataType.Object:
                    return System.Type.GetType("object");

                case CubridDataType.Bit:
                case CubridDataType.Varbit:
                    return System.Type.GetType("byte[]"); // vaild type string??

                case CubridDataType.Set:
                case CubridDataType.Multiset:
                case CubridDataType.Sequence:
                    return System.Type.GetType("object[]");  // vaild type string??

                default:
                    return null;
            }
        }

        public bool IsNull(int index)
        {
            return columnMetaData[index].IsNull;
        }

        public int GetAllValues(object[] values)
        {
            return columnMetaData.Length;
        }

        public int GetColumnCount()
        {
            return columnMetaData.Length;
        }
    }

    internal class ResultTuple
    {
        private int index;
        private CubridOid oid;
        private int valueCount;
        private object[] valueArray;
        private Dictionary<string, object> valueDictionary;

        internal ResultTuple(int count)
        {
            valueCount = count;
            valueArray = new object[count];
            valueDictionary = new Dictionary<string, object>();
        }

        internal int Index
        {
            get { return index; }
            set { index = value; }
        }

        internal CubridOid Oid
        {
            get { return oid; }
            set { oid = value; }
        }

        internal object this[int idx]
        {
            get { return valueArray[idx]; }
            set { valueArray[idx] = value; }
        }

        internal object this[string name]
        {
            get { return valueDictionary[name]; }
            set { valueDictionary[name] = value; }
        }

        public override string ToString()
        {
            StringBuilder builder = new StringBuilder();

            for (int i = 0; i < valueCount; i++)
            {
                if (valueArray[i] != null)
                {
                    builder.Append("Tuple: index = " + i);
                    builder.Append(", value = " + valueArray[i]);
                    builder.Append(", type = " + valueArray[i].GetType() + "\n");
                }
                else
                {
                    builder.Append("Tuple: index = " + i + ", value = null, type = null\n");
                }
            }

            return builder.ToString();
        }
    }
}
