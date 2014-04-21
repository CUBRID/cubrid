using System;
using System.Data;
using System.Data.Common;
using System.Net.Sockets;
using System.Text;

namespace Cubrid.Data.CubridClient
{
    public sealed class CubridConnection : DbConnection, ICloneable
    {
        private string connectionString;
        private string database, server, user, password, encoding;
        private int port;
        private bool autoCommit = true;

        public CubridConnection()
        {
            database = null;
            server = null;
            user = null;
            password = null;
            port = 0;
        }

        public CubridConnection(string connectionString) : this()
        {
            ConnectionString = connectionString;
        }

        public object Clone()
        {
            return null;
        }

        public new void Dispose()
        {
            Close();
        }

        public override string ConnectionString
        {
            get { return connectionString; }
            set
            {
                this.connectionString = value;
                parseConnectionString();
            }
        }

        public override int ConnectionTimeout
        {
            get { return 30; }
        }

        public override string Database
        {
            get { return database; }
        }

        public override ConnectionState State
        {
            get { return ConnectionState.Closed; }
        }

        public override string ServerVersion
        {
            get
            {
                return "";
            }
        }

        public override string DataSource
        {
            get { return server; }
        }

        public bool AutoCommit
        {
            get { return autoCommit; }
            set { autoCommit = value; }
        }

        protected override DbTransaction BeginDbTransaction(IsolationLevel il)
        {
            autoCommit = false;
            return new CubridTransaction(this, il);
        }

        public override void ChangeDatabase(string databaseName)
        {
            database = databaseName;
            Close();
            Open();
        }

        public override void Close()
        {
            CloseInternal();
        }

        protected override DbCommand CreateDbCommand()
        {
            CubridCommand command = new CubridCommand();
            command.Connection = this;
            return command;
        }

        public override void Open()
        {
            ConnectToServer(server, port, database, user, password);
        }

        private void parseConnectionString()
        {
            char[] seperator = { '=', ';' };
            string[] tokens = connectionString.Split(seperator);

            for (int i = 0; i < tokens.Length; i += 2)
            {
                if (tokens[i].Equals("server"))
                {
                    server = tokens[i + 1];
                }
                else if (tokens[i].Equals("database"))
                {
                    database = tokens[i + 1];
                }
                else if (tokens[i].Equals("port"))
                {
                    port = Convert.ToInt32(tokens[i + 1]);
                }
                else if (tokens[i].Equals("user"))
                {
                    user = tokens[i + 1];
                }
                else if (tokens[i].Equals("password"))
                {
                    password = tokens[i + 1];
                }
                else if (tokens[i].Equals("encoding"))
                {
                    encoding = tokens[i + 1];
                }
                else
                {
                    throw new CubridException("invalid connection parameter:" + tokens[i]);
                }
            }
        }

        private static byte[] driverInfo = {(byte) 'C', (byte) 'U', (byte) 'B', (byte) 'R', (byte) 'K', 
                                            (byte) 3, (byte) 8, (byte) 1, (byte) 0, (byte) 0};
        private byte[] brokerInfo;
        private Socket socket;
        private CubridStream stream;

        private bool isKeepConnection;
        private bool isStatementPooling;
        private IsolationLevel level = IsolationLevel.ReadUncommitted;

        public CubridStream Stream
        {
            get
            {
                stream.Reset();
                return stream;
            }
        }

        public void ConnectToServer(string server, int port, string db, string user, string password)
        {
            this.server = server;
            this.port = port;
            this.database = db;
            this.user = user;
            this.password = password;

            isKeepConnection = false;
            isStatementPooling = false;

            stream = new CubridStream();
            Reconnect();
        }

        private void Reconnect()
        {
            Connect(port);

            int newPort = SendDriverInfo();
            //Console.WriteLine("newport = {0}", newPort);

            if (newPort < 0)
            {
                throw new CubridException("invalid connection");
            }
            else if (newPort > 0)
            {
                socket.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.Linger, true);
                socket.Close();

                Connect(newPort);
            }

            SendDbInfo();
            ReceiveBrokerInfo();

            /* TODO: send isolation level, lock timeout */
        }

        private void Connect(int port)
        {
            try
            {
                socket = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
                socket.Connect(server, port);
                socket.NoDelay = true;
                stream.Reset(new NetworkStream(socket, true));
            }
            catch (SocketException e)
            {
                throw new CubridException(e.Message);
            }
        }

        private int SendDriverInfo()
        {
            try
            {
                stream.WriteBytesToRaw(driverInfo, 0, driverInfo.Length);
                stream.Flush();

                return stream.ReadIntFromRaw();
            }
            catch (SocketException e)
            {
                throw new CubridException(e.Message);
            }
         }

        private void ReceiveBrokerInfo()
        {
            try
            {
                stream.Receive();
                brokerInfo = stream.ReadBytes(4);
                //Console.WriteLine("{0}, {1}, {2}, {3}", brokerInfo[0], brokerInfo[1], brokerInfo[2], brokerInfo[3]);

                isKeepConnection = (brokerInfo[1] == 1);
                isStatementPooling = (brokerInfo[2] == 1);
            }
            catch (SocketException e)
            {
                throw new CubridException(e.Message);
            }
        }

        private void SendDbInfo()
        {
            byte[] dbinfo = new byte[96];

            byte[] dbBytes = Encoding.ASCII.GetBytes(database);
            Array.Copy(dbBytes, 0, dbinfo, 0, dbBytes.Length);

            byte[] userBytes = Encoding.ASCII.GetBytes(user);
            Array.Copy(userBytes, 0, dbinfo, 32, userBytes.Length);

            byte[] passBytes = Encoding.ASCII.GetBytes(password);
            Array.Copy(passBytes, 0, dbinfo, 64, passBytes.Length);

            try
            {
                stream.WriteBytesToRaw(dbinfo, 0, dbinfo.Length);
                stream.Flush();
            }
            catch (SocketException e)
            {
                throw new CubridException(e.Message);
            }
        }

        public void Commit()
        {
            EndTransaction(EndTransactionType.Commit);
            autoCommit = true;
        }

        public void Rollback()
        {
            EndTransaction(EndTransactionType.Rollback);
            autoCommit = true;
        }

        private void EndTransaction(EndTransactionType type)
        {
            stream.Reset();
            stream.WriteCommand(FunctionCode.EndTransaction);
            stream.WriteByteArg((byte)type);

            stream.Flush();

            if (type == EndTransactionType.Commit)
            {
                stream.Receive();
            }

            if (!isKeepConnection)
            {
                socket.SetSocketOption(SocketOptionLevel.Tcp, SocketOptionName.Linger, true);
                socket.Close();
                socket = null;
                Console.WriteLine("Connection is closed.");
            }
        }

        public void CloseInternal()
        {
            if (socket != null)
            {
                stream.Reset();
                stream.WriteCommand(FunctionCode.CloseConnection);
                stream.Flush();
                stream.Receive();
            }

            socket.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.Linger, true);
            socket.Close();
            socket = null;
            //Console.WriteLine("Connection is closed.");
        }

        internal void ReconnectIfNeed()
        {
            if (socket == null)
            {
                Reconnect();
            }
        }

        public IsolationLevel IsolationLevel
        {
            get { return level; }
            set { level = value; }
        }
    }
}
