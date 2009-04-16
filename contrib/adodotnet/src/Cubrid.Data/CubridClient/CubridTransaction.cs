using System;
using System.Data.Common;
using System.Data;

namespace Cubrid.Data.CubridClient
{
    public sealed class CubridTransaction : DbTransaction
    {
        private CubridConnection con;
        private IsolationLevel il;

        public CubridTransaction(CubridConnection con, IsolationLevel il)
        {
            this.con = con;
            this.il = il;
            con.IsolationLevel = il;
        }

        protected override DbConnection DbConnection
        {
            get { return con; } 
        }

        public override IsolationLevel IsolationLevel 
        {
            get { return il; }
        }

        public override void Commit()
        {
            con.Commit();
        }

        public override void Rollback()
        {
            con.Rollback();
        }
    }
}
