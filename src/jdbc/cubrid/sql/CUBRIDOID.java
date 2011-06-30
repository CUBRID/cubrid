/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
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

package cubrid.sql;

/**
 * Title:        CUBRID JDBC Driver
 * Description:
 * @version 2.0
 */

import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.NoSuchElementException;
import java.util.StringTokenizer;

import cubrid.jdbc.driver.CUBRIDConnection;
import cubrid.jdbc.driver.CUBRIDException;
import cubrid.jdbc.driver.CUBRIDJDBCErrorCode;
import cubrid.jdbc.driver.CUBRIDResultSet;
import cubrid.jdbc.jci.UConnection;
import cubrid.jdbc.jci.UError;
import cubrid.jdbc.jci.UErrorCode;
import cubrid.jdbc.jci.UJCIUtil;
import cubrid.jdbc.jci.UStatement;

public class CUBRIDOID {
	private CUBRIDConnection cur_con;
	private byte[] oid;
	private boolean is_closed;
	private UError error;

	/*
	 * Just for Driver's uses. DO NOT create an object with this constructor!
	 */
	public CUBRIDOID(CUBRIDConnection con, byte[] o) {
		cur_con = con;
		oid = o;
		is_closed = false;
	}

	public CUBRIDOID(CUBRIDOID o) {
		cur_con = o.cur_con;
		oid = o.oid;
		is_closed = false;
	}

	public synchronized ResultSet getValues(String attrNames[])
			throws SQLException {
		checkIsOpen();

		UStatement u_stmt = null;
		UConnection u_con = cur_con.getUConnection();
		synchronized (u_con) {
			u_stmt = u_con.getByOID(this, attrNames);
			error = u_con.getRecentError();
		}

		checkError();
		return new CUBRIDResultSet(u_stmt);
	}

	public synchronized void setValues(String[] attrNames, Object[] values)
			throws SQLException {
		checkIsOpen();

		if (attrNames == null || values == null) {
			throw new IllegalArgumentException();
		}

		if (attrNames.length != attrNames.length) {
			throw new IllegalArgumentException();
		}

		UConnection u_con = cur_con.getUConnection();
		synchronized (u_con) {
			u_con.putByOID(this, attrNames, values);
			error = u_con.getRecentError();
		}

		checkError();
	}

	public synchronized void remove() throws SQLException {
		checkIsOpen();

		UConnection u_con = cur_con.getUConnection();
		synchronized (u_con) {
			u_con.oidCmd(this, UConnection.DROP_BY_OID);
			error = u_con.getRecentError();
		}

		checkError();

		if (u_con.getAutoCommit()) {
			u_con.turnOnAutoCommitBySelf();
		}

	}

	public synchronized boolean isInstance() throws SQLException {
		checkIsOpen();

		Object instance_obj;
		UConnection u_con = cur_con.getUConnection();
		synchronized (u_con) {
			instance_obj = u_con.oidCmd(this, UConnection.IS_INSTANCE);
			error = u_con.getRecentError();
		}

		checkError();
		if (instance_obj == null)
			return false;
		else
			return true;
	}

	public synchronized void setReadLock() throws SQLException {
		checkIsOpen();

		UConnection u_con = cur_con.getUConnection();
		synchronized (u_con) {
			u_con.oidCmd(this, u_con.GET_READ_LOCK_BY_OID);
			error = u_con.getRecentError();
		}

		checkError();
	}

	public synchronized void setWriteLock() throws SQLException {
		checkIsOpen();

		UConnection u_con = cur_con.getUConnection();
		synchronized (u_con) {
			u_con.oidCmd(this, u_con.GET_WRITE_LOCK_BY_OID);
			error = u_con.getRecentError();
		}

		checkError();
	}

	public synchronized void addToSet(String attrName, Object value)
			throws SQLException {
		checkIsOpen();

		if (attrName == null) {
			throw new IllegalArgumentException();
		}

		UConnection u_con = cur_con.getUConnection();
		synchronized (u_con) {
			u_con.addElementToSet(this, attrName, value);
			error = u_con.getRecentError();
		}

		checkError();
	}

	public synchronized void removeFromSet(String attrName, Object value)
			throws SQLException {
		checkIsOpen();

		if (attrName == null) {
			throw new IllegalArgumentException();
		}

		UConnection u_con = cur_con.getUConnection();
		synchronized (u_con) {
			u_con.dropElementInSet(this, attrName, value);
			error = u_con.getRecentError();
		}

		checkError();
	}

	public synchronized void addToSequence(String attrName, int index,
			Object value) throws SQLException {
		checkIsOpen();

		if (attrName == null) {
			throw new IllegalArgumentException();
		}

		UConnection u_con = cur_con.getUConnection();
		synchronized (u_con) {
			u_con.insertElementIntoSequence(this, attrName, index, value);
			error = u_con.getRecentError();
		}

		checkError();
	}

	public synchronized void putIntoSequence(String attrName, int index,
			Object value) throws SQLException {
		checkIsOpen();

		if (attrName == null) {
			throw new IllegalArgumentException();
		}

		UConnection u_con = cur_con.getUConnection();
		synchronized (u_con) {
			u_con.putElementInSequence(this, attrName, index, value);
			error = u_con.getRecentError();
		}

		checkError();
	}

	public synchronized void removeFromSequence(String attrName, int index)
			throws SQLException {
		checkIsOpen();

		if (attrName == null) {
			throw new IllegalArgumentException();
		}

		UConnection u_con = cur_con.getUConnection();
		synchronized (u_con) {
			u_con.dropElementInSequence(this, attrName, index);
			error = u_con.getRecentError();
		}

		checkError();
	}

	public synchronized String getOidString() throws SQLException {
		checkIsOpen();

		if (oid == null || oid.length != UConnection.OID_BYTE_SIZE)
			return "";

		return ("@" + UJCIUtil.bytes2int(oid, 0) + "|"
				+ UJCIUtil.bytes2short(oid, 4) + "|" + UJCIUtil.bytes2short(
				oid, 6));
	}

	public byte[] getOID() {
		return oid;
	}

	public synchronized String getTableName() throws SQLException {
		checkIsOpen();

		UConnection u_con = cur_con.getUConnection();
		String tablename;
		synchronized (u_con) {
			tablename = (String) u_con.oidCmd(this,
					UConnection.GET_CLASS_NAME_BY_OID);
		}
		return tablename;
	}

	public static CUBRIDOID getNewInstance(CUBRIDConnection con, String oidStr)
			throws SQLException {
		if (con == null || oidStr == null) {
			throw new IllegalArgumentException();
		}
		if (oidStr.charAt(0) != '@')
			throw new IllegalArgumentException();
		StringTokenizer oidStringArray = new StringTokenizer(oidStr, "|");
		try {
			int page = Integer
					.parseInt(oidStringArray.nextToken().substring(1));
			short slot = Short.parseShort(oidStringArray.nextToken());
			short vol = Short.parseShort(oidStringArray.nextToken());

			byte[] bOID = new byte[UConnection.OID_BYTE_SIZE];
			bOID[0] = ((byte) ((page >>> 24) & 0xFF));
			bOID[1] = ((byte) ((page >>> 16) & 0xFF));
			bOID[2] = ((byte) ((page >>> 8) & 0xFF));
			bOID[3] = ((byte) ((page >>> 0) & 0xFF));
			bOID[4] = ((byte) ((slot >>> 8) & 0xFF));
			bOID[5] = ((byte) ((slot >>> 0) & 0xFF));
			bOID[6] = ((byte) ((vol >>> 8) & 0xFF));
			bOID[7] = ((byte) ((vol >>> 0) & 0xFF));

			return new CUBRIDOID(con, bOID);
		} catch (NoSuchElementException e) {
			throw new IllegalArgumentException();
		}
	}

	private void close() throws SQLException {
		if (is_closed) {
			return;
		}
		is_closed = true;
		cur_con = null;
		oid = null;
	}

	private void checkIsOpen() throws SQLException {
		if (is_closed) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.oid_closed);
		}
	}

	private void checkError() throws SQLException {
		switch (error.getErrorCode()) {
		case UErrorCode.ER_NO_ERROR:
			break;
		case UErrorCode.ER_IS_CLOSED:
			close();
			throw new CUBRIDException(CUBRIDJDBCErrorCode.oid_closed);
		case UErrorCode.ER_INVALID_ARGUMENT:
			throw new IllegalArgumentException();
		default:
			throw new CUBRIDException(error);
		}
	}
}
