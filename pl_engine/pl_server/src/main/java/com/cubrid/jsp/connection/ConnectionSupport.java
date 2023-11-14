package com.cubrid.jsp.connection;

import java.io.IOException;
import java.io.InterruptedIOException;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketAddress;
import java.net.SocketException;
import java.net.SocketTimeoutException;
import java.util.Arrays;

public class ConnectionSupport {

    private static final byte[] magic = new byte[32];

    static {
        Arrays.fill(magic, (byte) 0x00);
        magic[0] = 0x00;
        magic[1] = 0x00;
        magic[2] = 0x00;
        magic[3] = 0x01;
        magic[4] = 0x20;
        magic[5] = 0x08;
        magic[6] = 0x11;
        magic[7] = 0x22;
    }

    public static Socket connect(String hostName, int port, int cTimeout) {
        Socket socket = null;
        try {
            socket = new Socket();
            SocketAddress socketAddress = new InetSocketAddress(hostName, port);
            socket.connect(socketAddress, cTimeout);
        } catch (IOException e) {
        }
        return socket;
    }

    public static int readn(Socket socket, byte[] arr, int timeout) {
        int n = -1;
        if (arr == null) {
            return n;
        }

        try {
            socket.setSoTimeout(timeout);
        } catch (SocketException e) {
            return n;
        }

        try {
            n = socket.getInputStream().read(arr);
        } catch (SocketTimeoutException ex) {
            // TODO
        } catch (IOException e) {
            // TODO
        }

        return n;
    }

    public static void readRemainingBytes(Socket socket, int len) {
        // TODO
        byte[] dummy = new byte[len];
        readn(socket, dummy, 0 /* infinite timeout */);
    }

    public static int send(Socket socket, byte[] buf, int timeout) {
        int total = buf.length + Integer.SIZE;

        try {
            socket.getOutputStream().write(total);
            socket.getOutputStream().write(buf);
            socket.getOutputStream().flush();
        } catch (IOException e) {
            return -1;
        }

        return 0; // NO_ERROR
    }

    public static int sendRequestWithDataBuffer(ConnectionEntry conn, int request, byte[] argBuffer,
            byte[] replayBuffer) {
        // TODO
        return 0;
    }

    public static int sendMagic(Socket socket) {
        return send(socket, magic, 0);
    }

    public static int sendRequest(Socket socket, int connectType, int rid, String serverName) {
        return 0;
    }
}
