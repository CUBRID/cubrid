/*
 *
 * Copyright (c) 2016 CUBRID Corporation.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */

package com.cubrid.jsp.connection;

import java.io.IOException;
import java.net.Socket;
import java.util.List;

public class ConnectionEntry {

    public static final int CONN_OPEN = 1;
    public static final int CONN_CLOSED = 2;
    public static final int CONN_CLOSING = 3;

    public static final int COMMAND_TYPE = 1;
    public static final int DATA_TYPE = 2;
    public static final int ABORT_TYPE = 3;
    public static final int CLOSE_TYPE = 4;
    public static final int ERROR_TYPE = 5;

    private Socket socket;
    private int requestId;
    private int status;
    private int invalidateSnapshot;
    private int clientId;
    private int dbError;
    private boolean isInTransaction;
    private boolean resetOnCommit;
    private boolean isInMethod;
    private boolean isInFlashback;

    private List<QueueEntry> requestQueue;
    private List<QueueEntry> dataQueue;
    private List<QueueEntry> abortQueue;
    private List<QueueEntry> bufferQueue;
    private List<QueueEntry> errorQueue;

    private long sessionId;
    private int transactionId;

    public void initialize(Socket s) {
        this.requestId = 0;
        this.socket = s;
        this.status = CONN_OPEN;

        // TODO
        // this.clientId = ++css_ClientId;

        this.dataQueue = null;
        this.requestQueue = null;
        this.abortQueue = null;
        this.bufferQueue = null;
        this.errorQueue = null;
        this.setTranIndex(ConnectionManager.NULL_TRAN_INDEX);
        this.invalidateSnapshot = 1;
        this.isInMethod = false;
        this.dbError = 0;
    }

    public void setTranIndex(int idx) {
        // currently, java client does not require the following
        /*
         * if (idx == LOG_SYSTEM_TRAN_INDEX)
         * {
         * assert (false);
         * tran_index = NULL_TRAN_INDEX;
         * }
         */
        this.transactionId = idx;
    }

    // see css_close_conn ()
    public void shutdown() throws IOException {
        if (socket != null && !socket.isClosed()) {
            socket.close();
            socket = null;
        }
        status = CONN_CLOSED;
    }

    // see css_close_conn ()
    public void close() throws IOException {
        if (socket != null && !socket.isClosed()) {
            shutdown();
            initialize(null);
        }
    }

    public int getStatus() {
        return status;
    }

    public int getRequestId() {
        return requestId;
    }

    public Socket getSocket() {
        return socket;
    }

    public int getInvalidateSnapshot() {
        return invalidateSnapshot;
    }

    public int getClientId() {
        return clientId;
    }

    public int getDbError() {
        return dbError;
    }

    public boolean isInTransaction() {
        return isInTransaction;
    }

    public boolean isResetOnCommit() {
        return resetOnCommit;
    }

    public boolean isInMethod() {
        return isInMethod;
    }

    public boolean isInFlashback() {
        return isInFlashback;
    }

    public List<QueueEntry> getRequestQueue() {
        return requestQueue;
    }

    public List<QueueEntry> getDataQueue() {
        return dataQueue;
    }

    public List<QueueEntry> getAbortQueue() {
        return abortQueue;
    }

    public List<QueueEntry> getBufferQueue() {
        return bufferQueue;
    }

    public List<QueueEntry> getErrorQueue() {
        return errorQueue;
    }

    public long getSessionId() {
        return sessionId;
    }

    public int getTransactionId() {
        return transactionId;
    }

    public void setSocket(Socket socket) {
        this.socket = socket;
    }
}
