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

package com.cubrid.jsp.jdbc;

import com.cubrid.jsp.data.DBType;
import com.cubrid.jsp.data.LobHandleInfo;
import com.cubrid.jsp.impl.SUConnection;
import com.cubrid.jsp.jdbc.CUBRIDServerSideConnection;
import com.cubrid.jsp.jdbc.CUBRIDServerSideJDBCErrorCode;
import com.cubrid.jsp.jdbc.CUBRIDServerSideJDBCErrorManager;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.Flushable;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.sql.Blob;
import java.sql.Connection;
import java.sql.SQLException;
import java.util.ArrayList;

public class CUBRIDServerSideBlob implements Blob {
    /*
     * ======================================================================= |
     * CONSTANT VALUES
     * =======================================================================
     */
    private static final int BLOB_MAX_IO_LENGTH = 128 * 1024; // 128KB at once
    private static final int TYPE = DBType.DB_CLOB;

    /*
     * ======================================================================= |
     * PRIVATE
     * =======================================================================
     */
    private SUConnection conn;
    private boolean isWritable;
    private boolean isLobLocator;
    private LobHandleInfo lobHandle;

    private ArrayList<java.io.Flushable> streamList = new ArrayList<java.io.Flushable>();

    /*
     * ======================================================================= |
     * CONSTRUCTOR
     * =======================================================================
     */
    // make a new blob
    public CUBRIDServerSideBlob(Connection conn) throws SQLException {
        if (conn == null) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(CUBRIDServerSideJDBCErrorCode.ER_INVALID_VALUE,
            null);
        }

        SUConnection suConn = ((CUBRIDServerSideConnection) conn).getSUConnection();

        try {
            lobHandle = suConn.lobNew(DBType.DB_BLOB);
        } catch (IOException e) {
            // TODO: is correct?
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(CUBRIDServerSideJDBCErrorCode.ER_COMMUNICATION,
                    e);
        }

        this.conn = suConn;
        isWritable = true;
        isLobLocator = true;
    }

    // get blob from existing result set
    public CUBRIDServerSideBlob(Connection conn, LobHandleInfo lobHandle, boolean isLobLocator) throws SQLException {
        if (conn == null || lobHandle == null) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(CUBRIDServerSideJDBCErrorCode.ER_INVALID_VALUE,
                    null);
        }

        SUConnection suConn = ((CUBRIDServerSideConnection) conn).getSUConnection();
        this.conn = suConn;
        isWritable = false;
        this.isLobLocator = isLobLocator;
    }

    // get blob from existing result set
    public CUBRIDServerSideBlob(SUConnection suConn, LobHandleInfo lobHandle, boolean isLobLocator)
            throws SQLException {
        if (conn == null || lobHandle == null) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(CUBRIDServerSideJDBCErrorCode.ER_INVALID_VALUE,
                    null);
        }

        this.conn = suConn;
        isWritable = false;
        this.isLobLocator = isLobLocator;
    }

    public SUConnection getStatementHandler() {
        return conn;
    }

    /*
     * ======================================================================= |
     * java.sql.Blob interface
     * =======================================================================
     */
    public long length() throws SQLException {
        if (lobHandle == null) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(CUBRIDServerSideJDBCErrorCode.ER_INVALID_VALUE,
                    null);
        }
        return lobHandle.getLobSize();
    }

    public byte[] getBytes(long pos, int length) throws SQLException {
        if (lobHandle == null) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(CUBRIDServerSideJDBCErrorCode.ER_INVALID_VALUE,
                    null);
        }
        if (pos < 1 || length < 0) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(CUBRIDServerSideJDBCErrorCode.ER_INVALID_VALUE,
                    null);
        }
        if (length == 0) {
            return new byte[0];
        }

        pos--; // pos is now offset from 0
        int real_read_len, read_len, total_read_len = 0;

        if (pos + length > length()) {
            length = (int) (length() - pos);
        }

        if (length <= 0) {
            return new byte[0];
        }

        byte[] buf = new byte[length];

        if (isLobLocator) {
            while (length > 0) {
                read_len = Math.min(length, BLOB_MAX_IO_LENGTH);

                try {
                    real_read_len = conn.lobRead(lobHandle, pos, buf, total_read_len, read_len);
                } catch (IOException e) {
                    // TODO: is correct?
                    throw CUBRIDServerSideJDBCErrorManager
                            .createCUBRIDException(CUBRIDServerSideJDBCErrorCode.ER_COMMUNICATION, e);
                }

                pos += real_read_len;
                length -= real_read_len;
                total_read_len += real_read_len;

                if (real_read_len == 0) {
                    break;
                }
            }
        } else {
            System.arraycopy(lobHandle, (int) pos, buf, 0, length);
            total_read_len = length;
        }

        if (total_read_len < buf.length) {
            // In common case, this code cannot be executed
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(CUBRIDServerSideJDBCErrorCode.ER_UNKNOWN,
                    null);
            // byte[]new_buf = new byte[total_read_len];
            // System.arraycopy (buf, 0, new_buf, 0, total_read_len);
            // return new_buf;
        } else {
            return buf;
        }
    }

    public InputStream getBinaryStream() throws SQLException {
        return getBinaryStream(1, length());
    }

    /* JDK 1.6 */
    public InputStream getBinaryStream(long pos, long length) throws SQLException {
        if (lobHandle == null) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(CUBRIDServerSideJDBCErrorCode.ER_INVALID_VALUE,
                    null);
        }
        if (pos < 1 || length < 0) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(CUBRIDServerSideJDBCErrorCode.ER_INVALID_VALUE,
                    null);
        }

        return new BufferedInputStream(new CUBRIDServerSideBlobInputStream(this, pos, length),
                BLOB_MAX_IO_LENGTH);
    }

    public long position(byte[] pattern, long start) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public long position(Blob pattern, long start) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    public int setBytes(long pos, byte[] bytes) throws SQLException {
        return (setBytes(pos, bytes, 0, bytes.length));
    }

    public int setBytes(long pos, byte[] bytes, int offset, int len) throws SQLException {
        if (lobHandle == null) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(CUBRIDServerSideJDBCErrorCode.ER_INVALID_VALUE,
                    null);
        }
        if (pos < 1 || offset < 0 || len < 0) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(CUBRIDServerSideJDBCErrorCode.ER_INVALID_VALUE,
                    null);
        }
        if (offset + len > bytes.length) {
            throw new IndexOutOfBoundsException();
        }

        if (isWritable) {
            if (length() + 1 != pos) {
                throw CUBRIDServerSideJDBCErrorManager
                        .createCUBRIDException(CUBRIDServerSideJDBCErrorCode.ER_LOB_POS_INVALID, null);
            }

            pos--; // pos is now offset from 0

            int real_write_len, write_len, total_write_len = 0;

            while (len > 0) {
                write_len = Math.min(len, BLOB_MAX_IO_LENGTH);

                try {
                    real_write_len = conn.lobWrite(lobHandle, pos, bytes, offset, write_len);
                } catch (IOException e) {
                    // TODO: is correct?
                    throw CUBRIDServerSideJDBCErrorManager
                            .createCUBRIDException(CUBRIDServerSideJDBCErrorCode.ER_COMMUNICATION, e);
                }

                pos += real_write_len;
                len -= real_write_len;
                offset += real_write_len;
                total_write_len += real_write_len;
            }

            if (pos > length()) {
                lobHandle.setLobSize(pos);
            }

            return total_write_len;
        } else {
            throw CUBRIDServerSideJDBCErrorManager
                    .createCUBRIDException(CUBRIDServerSideJDBCErrorCode.ER_LOB_IS_NOT_WRITEABLE, null);
        }
    }

    public OutputStream setBinaryStream(long pos) throws SQLException {
        if (lobHandle == null) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(CUBRIDServerSideJDBCErrorCode.ER_INVALID_VALUE,
                    null);
        }
        if (pos < 1) {
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(CUBRIDServerSideJDBCErrorCode.ER_INVALID_VALUE,
                    null);
        }

        if (isWritable) {
            if (length() + 1 != pos) {
                throw CUBRIDServerSideJDBCErrorManager
                        .createCUBRIDException(CUBRIDServerSideJDBCErrorCode.ER_LOB_POS_INVALID, null);
            }

            OutputStream out = new BufferedOutputStream(new CUBRIDServerSideBlobOutputStream(this, pos),
                    BLOB_MAX_IO_LENGTH);
            addFlushableStream(out);
            return out;
        } else {
            throw CUBRIDServerSideJDBCErrorManager
                    .createCUBRIDException(CUBRIDServerSideJDBCErrorCode.ER_LOB_IS_NOT_WRITEABLE, null);
        }
    }

    public void truncate(long len) throws SQLException {
        throw new SQLException(new java.lang.UnsupportedOperationException());
    }

    /* JDK 1.6 */
    public void free() throws SQLException {
        conn = null;
        lobHandle = null;
        streamList = null;
        isWritable = false;
        isLobLocator = true;
    }

    public LobHandleInfo getLobHandle() {
        return lobHandle;
    }

    private void addFlushableStream(Flushable out) {
        streamList.add(out);
    }

    public void removeFlushableStream(Flushable out) {
        streamList.remove(out);
    }

    public void flushFlushableStreams() {
        if (!streamList.isEmpty()) {
            for (Flushable out : streamList) {
                try {
                    out.flush();
                } catch (IOException e) {
                }
            }
        }
    }

    public String toString() throws RuntimeException {
        if (isLobLocator == true) {
            return lobHandle.toString();
        } else {
            throw new RuntimeException("The lob locator does not exist because the column type has changed.");
        }
    }

    public boolean equals(Object obj) {
        if (obj instanceof CUBRIDServerSideBlob) {
            CUBRIDServerSideBlob that = (CUBRIDServerSideBlob) obj;
            return lobHandle.equals(that.lobHandle);
        }
        return false;
    }
}
