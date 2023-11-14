package com.cubrid.jsp.connection;

import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketAddress;
import java.util.Deque;
import java.util.LinkedList;
import java.util.List;

public class ConnectionManager {

    public static final int NULL_TRAN_INDEX = -1;
    public static final int INVALID_SOCKET = -1;

    public static final int NULL_REQUEST = 0;
    public static final int INFO_REQUEST = 1;
    public static final int DATA_REQUEST = 2;
    public static final int SERVER_REQUEST = 3; /* unused */
    public static final int UNUSED_REQUEST = 4;
    public static final int SERVER_REQUEST_NEW = 5;

    private static int clientId = 0;

    private Deque<ConnectionEntry> connAnchor = null;

    public Deque<ConnectionEntry> getConnectionAnchor() {
        if (connAnchor == null) {
            connAnchor = new LinkedList<ConnectionEntry>();
        }
        return connAnchor;
    }

    // see css_make_conn ()
    public ConnectionEntry makeConnection(Socket s) {
        ConnectionEntry c = new ConnectionEntry();
        c.initialize(s);

        getConnectionAnchor().addFirst(c);
        return c;
    }

    // see css_close_conn ()

    public ConnectionEntry connectToServer(String hostName, String serverName) {

        ConnectionEntry conn = makeConnection(null);
        return null;

    }

    public ConnectionEntry connect(String hostName, ConnectionEntry conn, int connectType, String serverName, int port,
            int timeout,
            int rid, boolean sendMagic) {
        Socket socket = null;
            socket = ConnectionSupport.connect(hostName, port, timeout);

            if (sendMagic == true && ConnectionSupport.sendMagic(socket) != 0) {
                return null;
            }

            if (ConnectionSupport.sendRequest(socket, connectType, rid, serverName) == 0) {
                conn.setSocket(socket);
                return conn;
            }

        return null;
    }
}
