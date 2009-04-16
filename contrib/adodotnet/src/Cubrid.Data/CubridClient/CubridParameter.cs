using System;
using System.Data;
using System.Data.Common;
using System.Text;

namespace Cubrid.Data.CubridClient
{
    public sealed class CubridParameter : DbParameter
    {
        private string name;

        public CubridParameter()
        {
            val = null;
        }

        public CubridParameter(object value)
        {
            val = value;
        }

        public CubridParameter(CubridDataType type)
        {
            this.type = type;
        }

        public CubridParameter(
            string parameterName,
            CubridType dbType,
            int size,
            ParameterDirection direction,
            bool isNullable,
            byte precision,
            byte scale,
            string sourceColumn,
            DataRowVersion sourceVersion,
            Object value
        )
        {
        }

        public override DbType DbType
        {
            get { return userType; }
            set { UserType = value; }
        }

        public override ParameterDirection Direction
        {
            get { return mode; }
            set { mode = value; }
        }

        public override bool IsNullable
        {
            get { return false; }
            set { }
        }

        public override string ParameterName
        {
            get { return name; }
            set { name = value; }
        }

        public override int Size
        {
            get { return 1; }
            set { }
        }

        public override string SourceColumn
        {
            get { return null; }
            set { }
        }

        public override bool SourceColumnNullMapping
        {
            get { return false; }
            set { }
        }

        public override DataRowVersion SourceVersion
        {
            get { return DataRowVersion.Current; }
            set { }
        }

        public override object Value
        {
            get { return val; }
            set { val = value; }
        }

        public override void ResetDbType()
        {
        }

        private CubridDataType type;
        private DbType userType;
        private object val;
        private ParameterDirection mode = ParameterDirection.Input;

        public CubridDataType Type
        {
            get { return type; }
            set { type = value; }
        }

        public DbType UserType
        {
            get { return userType; }
            set
            {
                userType = value;

                switch (userType)
                {
                    case DbType.Int16:
                        type = CubridDataType.Short;
                        break;
                    case DbType.Int32:
                        type = CubridDataType.Int;
                        break;
                    case DbType.String:
                        type = CubridDataType.String;
                        break;
                    case DbType.Single:
                        type = CubridDataType.Float;
                        break;
                    case DbType.Double:
                        type = CubridDataType.Double;
                        break;
                    case DbType.Date:
                        type = CubridDataType.Date;
                        break;
                    case DbType.Time:
                        type = CubridDataType.Time;
                        break;
                    case DbType.DateTime:
                        type = CubridDataType.Timestamp;
                        break;
                    default:
                        break;
                }
            }
        }

        public ParameterDirection Mode
        {
            get { return mode; }
            set { mode = value; }
        }

        internal void Write(CubridStream stream)
        {
            if (val == null)
            {
                stream.WriteByteArg((byte)CubridDataType.Null);
                stream.WriteNullArg();
            }
            else
            {
                stream.WriteByteArg((byte)type);

                switch (type)
                {
                    case CubridDataType.Null:
                        stream.WriteNullArg();
                        break;

                    case CubridDataType.Char:
                    case CubridDataType.Nchar:
                    case CubridDataType.String:
                    case CubridDataType.Varnchar:
                        stream.WriteStringArg((string)val, Encoding.Default);
                        break;

                    case CubridDataType.Short:
                        stream.WriteShortArg((short)val);
                        break;

                    case CubridDataType.Int:
                        stream.WriteIntArg((int)val);
                        break;

                    case CubridDataType.Float:
                        stream.WriteFloatArg((float)val);
                        break;

                    case CubridDataType.Double:
                    case CubridDataType.Monetary:
                        stream.WriteDoubleArg((double)val);
                        break;

                    case CubridDataType.Date:
                        stream.WriteDateArg((DateTime)val);
                        break;

                    case CubridDataType.Time:
                        stream.WriteTimeArg((DateTime)val);
                        break;

                    case CubridDataType.Timestamp:
                        stream.WriteDateTimeArg((DateTime)val);
                        break;

                    case CubridDataType.Set:
                    case CubridDataType.Multiset:
                    case CubridDataType.Sequence:
                        stream.WriteCollection((object[])val);
                        break;

                    case CubridDataType.Bit:
                    case CubridDataType.Varbit:
                        stream.WriteByteArg((byte[])val);
                        break;

                    case CubridDataType.Numeric:
                        break;

                    default:
                        break;
                }
            }
        }
    }
}
