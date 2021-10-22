package com.cubrid.jsp.jdbc;

import com.cubrid.jsp.data.DataUtilities;
import com.cubrid.jsp.data.SOID;
import com.cubrid.jsp.impl.SUConnection;
import com.cubrid.jsp.impl.SUStatement;
import cubrid.sql.CUBRIDOID;
import java.io.IOException;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.NoSuchElementException;
import java.util.StringTokenizer;

public class CUBRIDServerSideOID implements CUBRIDOID {
    private CUBRIDServerSideConnection connection;
    private SUConnection requestHandler;

    private SOID oid;
    private boolean isClosed;

    /*
     * Just for Driver's uses. DO NOT create an object with this constructor!
     */
    public CUBRIDServerSideOID(CUBRIDServerSideConnection con, SOID o) {
        connection = con;
        requestHandler = con.getSUConnection();
        oid = o;
        isClosed = false;
    }

    public CUBRIDServerSideOID(CUBRIDServerSideConnection con, byte[] o) {
        connection = con;
        requestHandler = con.getSUConnection();
        oid = new SOID(o);
        isClosed = false;
    }

    public CUBRIDServerSideOID(SUConnection suConn, SOID o) {
        requestHandler = suConn;
        oid = o;
        isClosed = false;
    }

    public CUBRIDServerSideOID(SUConnection suConn, byte[] o) {
        requestHandler = suConn;
        oid = new SOID(o);
        isClosed = false;
    }

    public CUBRIDServerSideOID(CUBRIDOID o) {
        connection = (CUBRIDServerSideConnection) o.getConnection();
        byte[] oidBytes = o.getOID();
        oid = new SOID(oidBytes);
        isClosed = false;
    }

    public ResultSet getValues(String attrNames[]) throws SQLException {
        try {
            SUStatement stmtImpl = requestHandler.getByOID(this, attrNames);
            return new CUBRIDServerSideResultSet(stmtImpl);
        } catch (IOException e) {
            // TODO: is correct?
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_COMMUNICATION, e);
        }
    }

    public void setValues(String[] attrNames, Object[] values) throws SQLException {
        if (attrNames == null || values == null) {
            throw new IllegalArgumentException();
        }

        if (attrNames.length != values.length) {
            throw new IllegalArgumentException();
        }
        try {
            requestHandler.putByOID(this, attrNames, values);
        } catch (IOException e) {
            // TODO: is correct?
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_COMMUNICATION, e);
        }
    }

    public void remove() throws SQLException {
        try {
            requestHandler.oidCmd(this, CUBRIDServerSideConstants.DROP_BY_OID);
        } catch (IOException e) {
            // TODO: is correct?
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_COMMUNICATION, e);
        }
    }

    public boolean isInstance() throws SQLException {
        try {
            Object instance_obj =
                    requestHandler.oidCmd(this, CUBRIDServerSideConstants.IS_INSTANCE);
            if (instance_obj == null) {
                return false;
            }
        } catch (IOException e) {
            // TODO: is correct?
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_COMMUNICATION, e);
        }
        return true;
    }

    public void setReadLock() throws SQLException {
        try {
            requestHandler.oidCmd(this, CUBRIDServerSideConstants.GET_READ_LOCK_BY_OID);
        } catch (IOException e) {
            // TODO: is correct?
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_COMMUNICATION, e);
        }
    }

    public void setWriteLock() throws SQLException {
        try {
            requestHandler.oidCmd(this, CUBRIDServerSideConstants.GET_WRITE_LOCK_BY_OID);
        } catch (IOException e) {
            // TODO: is correct?
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_COMMUNICATION, e);
        }
    }

    public void addToSet(String attrName, Object value) throws SQLException {
        if (attrName == null) {
            throw new IllegalArgumentException();
        }

        try {
            requestHandler.addElementToSet(this, attrName, value);
        } catch (IOException e) {
            // TODO: is correct?
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_COMMUNICATION, e);
        }
    }

    public void removeFromSet(String attrName, Object value) throws SQLException {
        if (attrName == null) {
            throw new IllegalArgumentException();
        }

        try {
            requestHandler.dropElementInSet(this, attrName, value);
        } catch (IOException e) {
            // TODO: is correct?
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_COMMUNICATION, e);
        }
    }

    public void addToSequence(String attrName, int index, Object value) throws SQLException {
        if (attrName == null) {
            throw new IllegalArgumentException();
        }

        try {
            requestHandler.insertElementIntoSequence(this, attrName, index, value);
        } catch (IOException e) {
            // TODO: is correct?
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_COMMUNICATION, e);
        }
    }

    public void putIntoSequence(String attrName, int index, Object value) throws SQLException {
        if (attrName == null) {
            throw new IllegalArgumentException();
        }

        try {
            requestHandler.putElementInSequence(this, attrName, index, value);
        } catch (IOException e) {
            // TODO: is correct?
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_COMMUNICATION, e);
        }
    }

    public void removeFromSequence(String attrName, int index) throws SQLException {
        if (attrName == null) {
            throw new IllegalArgumentException();
        }

        try {
            requestHandler.dropElementInSequence(this, attrName, index);
        } catch (IOException e) {
            // TODO: is correct?
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_COMMUNICATION, e);
        }
    }

    public String getOidString() throws SQLException {
        if (oid == null) {
            return "";
        }

        return ("@" + oid.pageId + "|" + oid.slotId + "|" + oid.volId);
    }

    public byte[] getOID() {
        byte[] bOID = null;
        if (oid != null) {
            bOID = new byte[DataUtilities.OID_BYTE_SIZE];
            bOID[0] = ((byte) ((oid.pageId >>> 24) & 0xFF));
            bOID[1] = ((byte) ((oid.pageId >>> 16) & 0xFF));
            bOID[2] = ((byte) ((oid.pageId >>> 8) & 0xFF));
            bOID[3] = ((byte) ((oid.pageId >>> 0) & 0xFF));
            bOID[4] = ((byte) ((oid.slotId >>> 8) & 0xFF));
            bOID[5] = ((byte) ((oid.slotId >>> 0) & 0xFF));
            bOID[6] = ((byte) ((oid.volId >>> 8) & 0xFF));
            bOID[7] = ((byte) ((oid.volId >>> 0) & 0xFF));
        }
        return bOID;
    }

    public Connection getConnection() {
        return connection;
    }

    public synchronized String getTableName() throws SQLException {
        try {
            String tablename =
                    (String)
                            requestHandler.oidCmd(
                                    this, CUBRIDServerSideConstants.GET_CLASS_NAME_BY_OID);
            return tablename;
        } catch (IOException e) {
            // TODO: is correct?
            throw CUBRIDServerSideJDBCErrorManager.createCUBRIDException(
                    CUBRIDServerSideJDBCErrorCode.ER_COMMUNICATION, e);
        }
    }

    public static CUBRIDOID getNewInstance(Connection con, String oidStr) throws SQLException {
        if (con == null || oidStr == null) {
            throw new IllegalArgumentException();
        }
        if (oidStr.charAt(0) != '@') throw new IllegalArgumentException();
        StringTokenizer oidStringArray = new StringTokenizer(oidStr, "|");
        try {
            int page = Integer.parseInt(oidStringArray.nextToken().substring(1));
            short slot = Short.parseShort(oidStringArray.nextToken());
            short vol = Short.parseShort(oidStringArray.nextToken());

            SOID sOid = new SOID(page, slot, vol);
            return new CUBRIDServerSideOID((CUBRIDServerSideConnection) con, sOid);
        } catch (NoSuchElementException e) {
            throw new IllegalArgumentException();
        }
    }

    private void close() throws SQLException {
        if (isClosed) {
            return;
        }
        isClosed = true;
        connection = null;
        oid = null;
    }
}
