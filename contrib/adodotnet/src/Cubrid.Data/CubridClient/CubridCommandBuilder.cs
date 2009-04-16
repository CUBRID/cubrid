using System;
using System.Data.Common;
using System.Data;

namespace Cubrid.Data.CubridClient
{
    public sealed class CubridCommandBuilder : DbCommandBuilder
    {
        protected override void ApplyParameterInfo(DbParameter parameter, DataRow row, StatementType statementType, bool whereClause)
        {
        }

        protected override string GetParameterName(string parameterName)
        {
            return null;
        }

        protected override string GetParameterName(int parameterOrdinal)
        {
            return null;
        }

        protected override string GetParameterPlaceholder(int parameterOrdinal)
        {
            return GetParameterName(parameterOrdinal);
        }

        protected override void SetRowUpdatingHandler(DbDataAdapter adapter)
        {
        }

    }
}
