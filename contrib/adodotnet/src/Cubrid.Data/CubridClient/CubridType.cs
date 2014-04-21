using System;
using System.Text;
using System.Data;

namespace Cubrid.Data.CubridClient
{
    public class CubridType
    {
        internal DbType SqlType2DbType(CubridDataType sqltype)
        {
            return DbType.AnsiString;
        }

        internal CubridDataType DbType2SqlType(DbType sqltype)
        {
            return CubridDataType.Bit;
        }

        internal IsolationLevel CubridIso2DbIso(CubridIsolation level)
        {
            return IsolationLevel.ReadCommitted;
        }

        internal CubridIsolation DbIso2CubridIso(IsolationLevel level)
        {
            return CubridIsolation.CommitClass_CommitInstance;
        }


    }
}
