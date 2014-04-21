#define WriteArg

using System;
using System.Text;
using System.Net;
using System.IO;
using System.Collections;
using Cubrid.Data.CubridClient;
using System.Net.Sockets;
using System.Diagnostics;

namespace Cubrid.Data.CubridClient
{
    public class CubridStream
    {
        #region RequestWriter

        private const int BUFFER_COUNT = 1024;
        private const int BUFFER_CAPACITY = 1024 * 100;

        private byte[][] requestBuffer;
        private int writeCursor;
        private int writtenLength;
        private int requestBufferCursor;
        private int requestBufferCount;
        private NetworkStream baseStream;

        public NetworkStream Stream
        {
            get { return baseStream; }
            set { baseStream = value; }
        }

        public CubridStream()
        {
            requestBuffer = new byte[BUFFER_COUNT][];
            requestBufferCount = 0;
            NewBuffer();
        }

        public void Reset()
        {
            writeCursor = 0;
            writtenLength = 0;
            requestBufferCursor = 0;
        }

        public void Reset(NetworkStream stream)
        {
            this.baseStream = stream;
            Reset();
        }

        public void WriteCommand(FunctionCode command)
        {
            WriteInt(0); /* reserved for total stream length */
            WriteByte((byte)command);
        }

        public void WriteShortArg(short value)
        {
            WriteInt(2);
            WriteShort(value);
        }

        public void WriteIntArg(int value)
        {
            WriteInt(4);
            WriteInt(value);
        }

        public void WriteFloatArg(float value)
        {
            WriteInt(4);
            WriteFloat(value);
        }

        public void WriteDoubleArg(double value)
        {
            WriteInt(8);
            WriteDouble(value);
        }

        public void WriteStringArg(string value, Encoding encoding)
        {
            byte[] b;

            b = encoding.GetBytes(value);

            WriteInt(b.Length + 1);
            WriteByte(b, 0, b.Length);
            WriteByte(0); /* NULL termination */
        }

        public void WriteByteArg(byte value)
        {
            WriteInt(1);
            WriteByte(value);
        }

        public void WriteByteArg(byte[] value)
        {
            WriteByteArg(value, 0, value.Length);
        }

        public void WriteByteArg(byte[] value, int offset, int length)
        {
            WriteInt(length);
            WriteByte(value, offset, length);
        }

        public void WriteDateArg(DateTime value)
        {
            WriteInt(12);
            WriteShort((short)value.Year);
            WriteShort((short)value.Month);
            WriteShort((short)value.Day);
            WriteShort(0);
            WriteShort(0);
            WriteShort(0);
        }

        public void WriteTimeArg(DateTime value)
        {
            WriteInt(12);
            WriteShort(0);
            WriteShort(0);
            WriteShort(0);
            WriteShort((short)value.Hour);
            WriteShort((short)value.Minute);
            WriteShort((short)value.Second);
        }

        public void WriteDateTimeArg(DateTime value)
        {
            WriteInt(12);
            WriteShort((short)value.Year);
            WriteShort((short)value.Month);
            WriteShort((short)value.Day);
            WriteShort((short)value.Hour);
            WriteShort((short)value.Minute);
            WriteShort((short)value.Second);
        }

        public void WriteOidArg(CubridOid value)
        {
            WriteInt(CubridOid.OID_BYTE_SIZE);
            WriteByte(value.Oid, 0, CubridOid.OID_BYTE_SIZE);
        }

        public void WriteCollection(object[] array)
        {
            /* TODO: */
        }

        public void WriteNullArg()
        {
            WriteInt(0);
        }

        public void WriteCacheTime()
        {
            WriteInt(8);
            WriteInt(0);
            WriteInt(0);
        }

        public void Flush()
        {
            WriteIntOverride(writtenLength - 4, 0, 0);

            for (int i = 0; i < requestBufferCursor; i++)
            {
                baseStream.Write(requestBuffer[i], 0, requestBuffer[i].Length);
            }

            if (writeCursor > 0)
            {
                baseStream.Write(requestBuffer[requestBufferCursor], 0, writeCursor);
            }

            baseStream.Flush();
            Reset(baseStream);
        }

        private void WriteShort(short value)
        {
            WriteByte(BitConverter.GetBytes(IPAddress.HostToNetworkOrder(value)), 0, 2);
        }

        private void WriteInt(int value)
        {
            WriteByte(BitConverter.GetBytes(IPAddress.HostToNetworkOrder(value)), 0, 4);
        }

        private void WriteIntOverride(int value, int bufferCursor, int pos)
        {
            int savedLength = this.writtenLength;
            int savedBufferCursor = this.requestBufferCursor;
            int savedPos = this.writeCursor;

            this.requestBufferCursor = bufferCursor;
            this.writeCursor = pos;

            WriteInt(value);

            this.writtenLength = savedLength;
            this.requestBufferCursor = savedBufferCursor;
            this.writeCursor = savedPos;
        }

        private void WriteFloat(float value)
        {
            int i = BitConverter.ToInt32(BitConverter.GetBytes(value), 0);
            WriteByte(BitConverter.GetBytes(IPAddress.HostToNetworkOrder(i)), 0, 4);
        }

        private void WriteDouble(double value)
        {
            long l = BitConverter.DoubleToInt64Bits(value);
            WriteByte(BitConverter.GetBytes(IPAddress.HostToNetworkOrder(l)), 0, 8);
        }

        private void WriteByte(byte value)
        {
            if (writeCursor >= BUFFER_CAPACITY)
            {
                requestBufferCursor++;
                if (requestBufferCursor >= requestBufferCount)
                {
                    NewBuffer();
                }
                writeCursor = 0;
            }

            requestBuffer[requestBufferCursor][writeCursor] = value;
            writeCursor++;
            writtenLength++;
        }

        private void WriteByte(byte[] value, int offset, int length)
        {
            if (writeCursor + length <= BUFFER_CAPACITY)
            {
                Array.Copy(value, offset, requestBuffer[requestBufferCursor], writeCursor, length);
                writeCursor += length;
                this.writtenLength += length;
            }
            else
            {
                for (int i = 0; i < length; i++)
                {
                    WriteByte(value[offset]);
                    offset++;
                }
            }
        }

        private void NewBuffer()
        {
            requestBuffer[requestBufferCount] = new byte[BUFFER_CAPACITY];
            requestBufferCount++;
        }

        internal void WriteBytesToRaw(byte[] driverInfo, int p, int p_3)
        {
            baseStream.Write(driverInfo, p, p_3);
            baseStream.Flush();
        }

        #endregion

        #region ResponseReader

        private int readCursor;
        private int responseBufferCapacity;
        private byte[] responseBuffer;
        private int responseCode;

        public int ResponseCode
        {
            get { return responseCode; }
        }

        public int Receive()
        {
            readCursor = 0;
            responseBufferCapacity = ReadStreamSize();
            responseBuffer = new byte[responseBufferCapacity];

            FillBuffer();

            responseCode = ReadInt(); //always not!!!
            if (responseCode < 0)
            {
                string emsg = ReadString(responseBufferCapacity - 4, Encoding.Default);
                Console.WriteLine("Error: {0}, {1}", responseCode, emsg);
                throw new CubridException(emsg);
            }

            return responseCode;
        }

        private int ReadStreamSize()
        {
            byte[] b = new byte[4];

            baseStream.Read(b, 0, 4);
            return IPAddress.NetworkToHostOrder(BitConverter.ToInt32(b, 0));
        }

        private void FillBuffer()
        {
            int read = 0, r;

            while (read < responseBufferCapacity)
            {
                r = baseStream.Read(responseBuffer, read, responseBufferCapacity - read);
                if (r == 0)
                {
                    responseBufferCapacity = read;
                    break;
                }

                read += r;
            }
        }

        public byte ReadByte()
        {
            if (readCursor >= responseBufferCapacity)
            {
                throw new CubridException("invalid buffer position");
            }

            return responseBuffer[readCursor++];
        }

        public void ReadBytes(byte[] value, int offset, int length)
        {
            if (value == null)
            {
                return;
            }

            if (readCursor + length > responseBufferCapacity)
            {
                throw new CubridException("invalid buffer position");
            }

            Array.Copy(responseBuffer, readCursor, value, offset, length);
            readCursor += length;
        }

        public void ReadBytes(byte[] value)
        {
            ReadBytes(value, 0, value.Length);
        }

        public byte[] ReadBytes(int size)
        {
            byte[] value = new byte[size];
            ReadBytes(value, 0, size);
            return value;
        }

        public short ReadShort()
        {
            if (readCursor + 2 > responseBufferCapacity)
            {
                throw new CubridException("invalid buffer position");
            }

            short value = IPAddress.NetworkToHostOrder(BitConverter.ToInt16(responseBuffer, readCursor));
            readCursor += 2;

            return value;
        }

        public int ReadInt()
        {
            if (readCursor + 4 > responseBufferCapacity)
            {
                throw new CubridException("invalid buffer position");
            }

            int value = IPAddress.NetworkToHostOrder(BitConverter.ToInt32(responseBuffer, readCursor));
            readCursor += 4;

            return value;
        }

        public long ReadLong()
        {
            if (readCursor + 8 > responseBufferCapacity)
            {
                throw new CubridException("invalid buffer position");
            }

            long value = IPAddress.NetworkToHostOrder(BitConverter.ToInt64(responseBuffer, readCursor));
            readCursor += 8;

            return value;
        }

        public float ReadFloat()
        {
            return BitConverter.ToSingle(BitConverter.GetBytes(ReadInt()), 0);
        }

        public double ReadDouble()
        {
            return BitConverter.Int64BitsToDouble(ReadLong());
        }

        public string ReadString(int size, Encoding encoding)
        {
            string value;

            if (size <= 0)
            {
                return null;
            }

            if (readCursor + size > responseBufferCapacity)
            {
                throw new CubridException("invalid buffer position");
            }

            value = encoding.GetString(responseBuffer, readCursor, size - 1);
            readCursor += size;

            return value;
        }

        public DateTime ReadDate()
        {
            int year, month, day;

            year = ReadShort();
            month = ReadShort();
            day = ReadShort();

            return new DateTime(year, month, day);
        }

        public DateTime ReadTime()
        {
            int hour, min, sec;

            hour = ReadShort();
            min = ReadShort();
            sec = ReadShort();

            return new DateTime(0, 0, 0, hour, min, sec);
        }

        public DateTime ReadDateTime()
        {
            int year, month, day, hour, min, sec;

            year = ReadShort();
            month = ReadShort();
            day = ReadShort();

            hour = ReadShort();
            min = ReadShort();
            sec = ReadShort();

            return new DateTime(year, month, day, hour, min, sec);
        }

        public CubridOid ReadOid()
        {
            byte[] oid = ReadBytes(CubridOid.OID_BYTE_SIZE);
            return new CubridOid(oid);
        }

        internal int ReadIntFromRaw()
        {
            byte[] b = new byte[4];
            baseStream.Read(b, 0, 4);

            return IPAddress.NetworkToHostOrder(BitConverter.ToInt32(b, 0));
        }

        #endregion

        internal void RequestPrepare(string sql, PrepareOption flag)
        {
            WriteCommand(FunctionCode.Prepare);
            WriteStringArg(sql, Encoding.Default);
            WriteByteArg((byte)flag);

            Flush();
            Receive();
        }

        internal void RequestCloseHandle(int handle)
        {
            WriteCommand(FunctionCode.CloseStatement);
            WriteIntArg(handle);

            Flush();
            Receive();
        }

        internal int RequestExecute(int handle, ExecutionOption flag, 
            CubridParameter[] parameters, byte[] paramModes, byte fetchFlag, bool autoCommit)
        {
            WriteCommand(FunctionCode.Execute);

            WriteIntArg(handle);
            WriteByteArg((byte)flag);
            WriteIntArg(0); /* max field */
            WriteIntArg(0); /* max fetch size */
            
            if (paramModes != null)
            {
                WriteByteArg(paramModes);
            }
            else
            {
                WriteNullArg(); /* bind mode */
            }

            WriteByteArg(fetchFlag); /* fetch flag */
            WriteByteArg(autoCommit ? (byte)1 : (byte)0); /* auto commit */
            WriteByteArg(1); /* not scrollable */
            WriteCacheTime(); /* cache time */

            /* bind parameter */
            if (parameters != null)
            {
                for (int i = 0; i < parameters.Length; i++)
                {
                    parameters[i].Write(this);
                }
            }

            Flush();

            return Receive();
        }

        internal int RequestNextResult(int handle)
        {
            WriteCommand(FunctionCode.NextResult);

            WriteIntArg(handle);
            WriteIntArg(0);

            Flush();

            return Receive();
        }

        internal int RequestMoveCursor(int handle, int offset, CursorOrigin origin)
        {
            WriteCommand(FunctionCode.Cursor);

            WriteIntArg(handle);
            WriteIntArg(offset);
            WriteIntArg((int)origin);

            Flush();
            Receive();

            return ReadInt();
        }

        internal int RequestFetch(int handle)
        {
            WriteCommand(FunctionCode.Fetch);

            WriteIntArg(handle);
            WriteIntArg(1); /* position */ //TODO: check always 1 ??
            WriteIntArg(0); /* fetchsize */
            WriteByteArg(0); /* isSensitive */
            WriteIntArg(0); /* ? */

            Flush();

            return Receive();
        }

        internal ResultInfo[] ReadResultInfo(int resultCount)
        {
            ResultInfo[] resultInfos = new ResultInfo[resultCount];

            for (int i = 0; i < resultCount; i++)
            {
                resultInfos[i] = new ResultInfo();

                resultInfos[i].StmtType = (CubridStatementype)ReadByte();
                resultInfos[i].ResultCount = ReadInt();
                resultInfos[i].Oid = ReadOid();

                resultInfos[i].CacheTimeSec = ReadInt();
                resultInfos[i].CacheTImeUsec = ReadInt();

                //Debug.WriteLine("stmtType = {0}" + stmtType);
                //Debug.WriteLine("count = {0}" + count);
                //Debug.WriteLine("oid = {0}" + oid);
                //Debug.WriteLine("cacheTimeSec = {0}" + cacheTimeSec);
                //Debug.WriteLine("cacheTImeUsec = {0}" + cacheTImeUsec);
            }

            return resultInfos;
        }

        internal ColumnMetaData[] ReadColumnInfo(int count)
        {
            ColumnMetaData[] infoArray = new ColumnMetaData[count];

            for (int i = 0; i < count; i++)
            {
                ColumnMetaData info = new ColumnMetaData();

                info.Type = (CubridDataType)ReadByte();
                info.Scale = ReadShort();
                info.Precision = ReadInt();
                info.Name = ReadString(ReadInt(), Encoding.ASCII);
                info.RealName = ReadString(ReadInt(), Encoding.ASCII);
                info.Table = ReadString(ReadInt(), Encoding.ASCII);
                info.IsNull = (ReadByte() == 0);

                Debug.WriteLine(info);

                infoArray[i] = info;
            }

            return infoArray;
        }

        internal void ReadResultTuple(ResultTuple tuple, ColumnMetaData[] columnInfos, CubridStatementype stmtType, CubridConnection con)
        {
            tuple.Index = ReadInt();
            tuple.Oid = ReadOid();

            for (int j = 0; j < columnInfos.Length; j++)
            {
                int size = ReadInt();
                object val;

                if (size <= 0)
                {
                    val = null;
                }
                else
                {
                    CubridDataType type = CubridDataType.Null;

                    if (stmtType == CubridStatementype.Call
                        || stmtType == CubridStatementype.Evaluate
                        || stmtType == CubridStatementype.CallStoredProcedure
                        || columnInfos[j].Type == CubridDataType.Null)
                    {
                        type = (CubridDataType)ReadByte();
                        size--;
                    }
                    else
                    {
                        type = columnInfos[j].Type;
                    }

                    val = ReadValue(j, type, size, con);
                }

                tuple[j] = val;
                tuple[columnInfos[j].Name] = val;
            }

            Debug.WriteLine(tuple);
        }

        internal void ReadResultTupleSP(ResultTuple tuple, int colCount, CubridConnection con)
        {
            tuple.Index = ReadInt();
            tuple.Oid = ReadOid();

            for (int i = 0; i < colCount; i++)
            {
                int size = ReadInt();
                object val;

                if (size <= 0)
                {
                    val = null;
                }
                else
                {
                    CubridDataType type;

                    type = (CubridDataType)ReadByte();
                    size--;

                    val = ReadValue(i, type, size, con);
                }

                tuple[i] = val;
            }
            Debug.WriteLine(tuple);
        }

        internal object ReadValue(int index, CubridDataType type, int size, CubridConnection con)
        {

            switch (type)
            {
                case CubridDataType.Char:
                case CubridDataType.Nchar:
                case CubridDataType.String:
                case CubridDataType.Varnchar:
                    return ReadString(size, Encoding.Default);

                case CubridDataType.Short:
                    return ReadShort();

                case CubridDataType.Int:
                    return ReadInt();

                case CubridDataType.Float:
                    return ReadFloat();

                case CubridDataType.Double:
                case CubridDataType.Monetary:
                    return ReadDouble();

                case CubridDataType.Date:
                    return ReadDate();

                case CubridDataType.Time:
                    return ReadTime();

                case CubridDataType.Timestamp:
                    return ReadDateTime();

                case CubridDataType.Object:
                    return ReadOid();

                case CubridDataType.Bit:
                case CubridDataType.Varbit:
                    return ReadBytes(size);

                case CubridDataType.Set:
                case CubridDataType.Multiset:
                case CubridDataType.Sequence:
                    CubridDataType baseType = (CubridDataType)ReadByte();
                    int count = ReadInt();
                    object[] seq = new object[count];

                    for (int i = 0; i < count; i++)
                    {
                        int elesize = ReadInt();
                        if(elesize <= 0)
                        {
                            seq[i] = null;
                        }
                        else
                        {
                            seq[i] = ReadValue(i, baseType, elesize, con);
                        }
                    }

                    return seq;

                case CubridDataType.ResultSet:
                    int handle = ReadInt();
                    CubridCommand cmd = new CubridCommand(con, handle);
                    return cmd.GetDataReaderFromStoredProcedure();

                default:
                    return null; //throw exception ??
            }
        }


        internal int RequestBatchExecute(string[] sqls)
        {

            WriteCommand(FunctionCode.ExecuteBatch);
            WriteIntArg(0); //auto commit

            for (int i = 0; i < sqls.Length; i++)
            {
                if (sqls[i] != null)
                {
                    WriteStringArg(sqls[i], Encoding.Default);
                }
                else
                {
                    WriteNullArg(); // throwing exception is right???
                }
            }

            Flush();
            return Receive();
        }

        internal int RequestBatchExecute(int handle, CubridParameterCollection[] paramCollection)
        {
            CubridParameter parameter;

            WriteCommand(FunctionCode.ExecuteBatchPrepared);
            WriteIntArg(handle);
            WriteIntArg(0); //auto commit

            for (int i = 0; i < paramCollection.Length; i++)
            {
                for (int j = 0; j < paramCollection[i].Count; j++)
                {
                    parameter = (CubridParameter) paramCollection[i][j];
                    if (parameter != null)
                    {
                        parameter.Write(this);
                    }
                }
            }

            Flush();
            return Receive();
        }

        internal BatchResult[] ReadBatchResult(int count)
        {
            BatchResult[] results = new BatchResult[count];

            for (int i = 0; i < count; i++)
            {
                results[i].StmtType = (StmtType) ReadByte(); //statement type
                results[i].Result = ReadInt(); //execute result

                if (results[i].Result < 0)
                {
                    results[i].ErrorCode = results[i].Result;
                    results[i].Result = -3; //???
                    results[i].ErrorMessage = ReadString(ReadInt(), Encoding.Default);
                }
                else
                {
                    ReadInt(); //dummy
                    ReadShort();//dummy
                    ReadShort();//dummy
                }
            }

            return results;
        }

        internal string RequestServerVersion()
        {
            WriteCommand(FunctionCode.GetDbVersion);
            WriteIntArg(0); //auto commit

            Flush();
            Receive(); 

            return ReadString(responseBufferCapacity - 4, Encoding.Default);
        }

        internal string RequestQueryPlan(int handle)
        {
            WriteCommand(FunctionCode.GetQueryInfo);
            WriteIntArg(handle);
            WriteByteArg(0x01); //QUERY_INFO_PLAN flag

            Flush();
            Receive(); 

            return ReadString(responseBufferCapacity - 4, Encoding.Default);
        }

        internal string RequestQueryPlan(string sql)
        {
            WriteCommand(FunctionCode.GetQueryInfo);
            WriteIntArg(0); //no handle
            WriteByteArg(0x01); //QUERY_INFO_PLAN flag
            WriteStringArg(sql, Encoding.Default);

            Flush();
            Receive(); 

            return ReadString(responseBufferCapacity - 4, Encoding.Default);
        }

        internal void RequestCloseConnection()
        {
            WriteCommand(FunctionCode.CloseConnection);

            Flush();
            Receive();
        }

        internal int RequestOutResultSet(int handle)
        {
            WriteCommand(FunctionCode.MakeOutResultSet);
            WriteIntArg(handle);

            Flush();
            Receive();

            return ReadInt();
        }

        internal bool RequestCheck()
        {
            WriteCommand(FunctionCode.CheckCas);
            Flush();

            int result = ReadIntFromRaw();

            if (result == 0)
                return true;

            if (result < 4)
                return false;

            result = ReadIntFromRaw();
            if (result < 0)
                return false;

            return true;
        }
    }
}
