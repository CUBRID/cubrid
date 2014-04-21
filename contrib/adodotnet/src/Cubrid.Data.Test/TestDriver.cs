using System;
using System.Data;
using System.Data.Common;
using Cubrid.Data.CubridClient;

namespace Cubrid.Data.Test
{
    class TestDriver
    {
        static void Main(string[] args)
        {
            //TestSimple();
            TestTransaction();
            TestQuery();
            TestStoredProcedure();
            TestStoredProcedureResultSet();
        }

        static void TestSimple()
        {
            try
            {
                CubridConnection con = new CubridConnection();
                con.ConnectionString = "server=localhost;database=testdb;port=30000;user=dba;password=";
                con.Open();

                string sql = "CREATE TABLE t(a int, b char(10), c string, d float, e double, f date)";
                CubridCommand command = new CubridCommand(sql, con);
                command.ExecuteNonQuery();

                command.Close();
                con.Close();
            }
            catch (CubridException e)
            {
                Console.WriteLine(e);
            }
        }

        static void TestTransaction()
        {
            DbTransaction tran = null;

            try
            {
                CubridConnection con = new CubridConnection();
                con.ConnectionString = "server=localhost;database=testdb;port=30000;user=dba;password=";
                con.Open();

                tran = con.BeginTransaction();

                string sql = "INSERT INTO t VALUES(?, ?, ?, ?, ?, ?)";
                CubridCommand cmd = new CubridCommand(sql, con);

                cmd.Parameters.Add(1);
                cmd.Parameters[0].DbType = DbType.Int32;

                cmd.Parameters.Add("cubrid");
                cmd.Parameters[1].DbType = DbType.String;

                cmd.Parameters.Add(CubridDataType.String);
                cmd.Parameters[2].Value = "ado.net provider";

                cmd.Parameters.Add(3.14f);
                cmd.Parameters[3].DbType = DbType.Single;

                cmd.Parameters.Add(3.14159253637283645);
                cmd.Parameters[4].DbType = DbType.Double;

                cmd.Parameters.Add(new DateTime());
                cmd.Parameters[5].DbType = DbType.Date;

                cmd.ExecuteNonQuery();

                cmd.Parameters[0].Value = 2;
                cmd.Parameters[1].Value = "open";
                cmd.Parameters[2].Value = "source";
                cmd.Parameters[3].Value = 735626.34f;
                cmd.Parameters[4].Value = 8373625.383635235373;
                cmd.Parameters[5].Value = new DateTime(2008, 12, 24);

                cmd.ExecuteNonQuery();
                tran.Commit();

                cmd.Close();
                con.Close();
            }
            catch (CubridException e)
            {
                tran.Rollback();
                Console.WriteLine(e);
            }
        }
        
        static void TestQuery()
        {
            try
            {
                CubridConnection con = new CubridConnection();
                con.ConnectionString = "server=localhost;database=testdb;port=30000;user=dba;password=";
                con.Open();

                string sql = "SELECT * FROM t";
                CubridCommand cmd = new CubridCommand(sql, con);
                DbDataReader reader = cmd.ExecuteReader();

                while (reader.Read())
                {
                    Console.Write(reader.GetInt32(0) + " | ");
                    Console.Write(reader.GetString(1) + " | ");
                    Console.Write(reader.GetString(2) + " | ");
                    Console.Write(reader.GetFloat(3) + " | ");
                    Console.Write(reader.GetDouble(4) + " | ");
                    Console.WriteLine(reader.GetDateTime(5));
                }

                cmd.Close();
                con.Close();
            }
            catch (CubridException e)
            {
                Console.WriteLine(e);
            }
        }
        
        static void TestStoredProcedure()
        {
            try
            {
                CubridConnection con = new CubridConnection();
                con.ConnectionString = "server=localhost;database=testdb;port=30000;user=dba;password=";
                con.Open();

                string sql = "CREATE FUNCTION sp1(a int) RETURN string AS LANGUAGE JAVA NAME 'SpTest.test1(int) return java.lang.String'";
                CubridCommand cmd = new CubridCommand(sql, con);
                cmd.ExecuteNonQuery();
                cmd.Close();

                sql = "? = CALL sp1(?)";
                cmd = new CubridCommand(sql, con);
                cmd.CommandType = CommandType.StoredProcedure;

                cmd.Parameters.Add();
                cmd.Parameters[0].Direction = ParameterDirection.Output;
                cmd.Parameters[0].DbType = DbType.String;

                cmd.Parameters.Add(12345678);
                cmd.Parameters[1].DbType = DbType.Int32;

                cmd.ExecuteNonQuery();
                Console.WriteLine("result: " + cmd.Parameters[0].Value);
                cmd.Close();

                sql = "DROP FUNCTION sp1";
                cmd = new CubridCommand(sql, con);
                cmd.ExecuteNonQuery();
                cmd.Close();

                con.Close();
            }
            catch (CubridException e)
            {
                Console.WriteLine(e);
            }
        }

        static void TestStoredProcedureResultSet()
        {
            try
            {
                CubridConnection con = new CubridConnection();
                con.ConnectionString = "server=localhost;database=testdb;port=30000;user=dba;password=";
                con.Open();

                string sql = "CREATE FUNCTION sp2() RETURN cursor AS LANGUAGE JAVA NAME 'SpTest.test2() return java.sql.ResultSet'";
                CubridCommand cmd = new CubridCommand(sql, con);
                cmd.ExecuteNonQuery();
                cmd.Close();

                sql = "? = CALL sp2()";
                cmd = new CubridCommand(sql, con);
                cmd.CommandType = CommandType.StoredProcedure;

                cmd.Parameters.Add(CubridDataType.ResultSet);
                cmd.Parameters[0].Direction = ParameterDirection.Output;

                cmd.ExecuteNonQuery();

                DbDataReader reader = (DbDataReader)cmd.Parameters[0].Value;

                while (reader.Read())
                {
                    Console.Write(reader.GetInt32(0) + " | ");
                    Console.Write(reader.GetString(1) + " | ");
                    Console.Write(reader.GetString(2) + " | ");
                    Console.Write(reader.GetFloat(3) + " | ");
                    Console.Write(reader.GetDouble(4) + " | ");
                    Console.WriteLine(reader.GetDateTime(5));
                }

                cmd.Close();

                sql = "DROP FUNCTION sp2";
                cmd = new CubridCommand(sql, con);
                cmd.ExecuteNonQuery();
                cmd.Close();

                con.Close();
            }
            catch (CubridException e)
            {
                Console.WriteLine(e);
            }
        }
    }
}
