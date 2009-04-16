using System;
using System.Data.Common;
using System.Security;
using System.Security.Permissions;

namespace Cubrid.Data.CubridClient
{
    public sealed class CubridClientFactory : DbProviderFactory
    {
        public override bool CanCreateDataSourceEnumerator { get { return false;} }

        public override DbCommand CreateCommand()
        {
            return new CubridCommand();
        }

        public override DbCommandBuilder CreateCommandBuilder()
        {
            return null;
        }

        public override DbConnection CreateConnection()
        {
            return new CubridConnection();
        }

        public override DbConnectionStringBuilder CreateConnectionStringBuilder()
        {
            return null;
        }

        public override DbDataAdapter CreateDataAdapter()
        {
            return null;
        }

        public override DbDataSourceEnumerator CreateDataSourceEnumerator()
        {
            return null;
        }

        public override DbParameter CreateParameter()
        {
            return new CubridParameter();
        }

        public override CodeAccessPermission CreatePermission(PermissionState state)
        {
            return null;
        }


    }
}
