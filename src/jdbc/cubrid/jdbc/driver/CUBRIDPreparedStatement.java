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

package cubrid.jdbc.driver;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.Reader;
import java.io.Writer;
import java.math.BigDecimal;
import java.net.URL;
import java.sql.Array;
import java.sql.Blob;
import java.sql.Clob;
import java.sql.Date;
import java.sql.NClob;
import java.sql.ParameterMetaData;
import java.sql.PreparedStatement;
import java.sql.Ref;
import java.sql.ResultSet;
import java.sql.ResultSetMetaData;
import java.sql.RowId;
import java.sql.SQLException;
import java.sql.SQLXML;
import java.sql.Statement;
import java.sql.Time;
import java.sql.Timestamp;
import java.util.Calendar;

import cubrid.jdbc.jci.CUBRIDCommandType;
import cubrid.jdbc.jci.UBatchResult;
import cubrid.jdbc.jci.UColumnInfo;
import cubrid.jdbc.jci.UErrorCode;
import cubrid.jdbc.jci.UStatement;
import cubrid.jdbc.jci.UShardInfo;
import cubrid.sql.CUBRIDOID;
import cubrid.sql.CUBRIDTimetz;
import cubrid.sql.CUBRIDTimestamptz;

/**
 * Title: CUBRID JDBC Driver Description:
 * 
 * @version 2.0
 */

public class CUBRIDPreparedStatement extends CUBRIDStatement implements
		PreparedStatement {
	protected int autoGeneratedKeys;

	private boolean first_result_type;

	protected CUBRIDPreparedStatement(CUBRIDConnection c, UStatement us, int t,
			int concur, int hold, int autoGeneratedKeys) {
		super(c, t, concur, hold);
		u_stmt = us;
		first_result_type = u_stmt.getSqlType();
		this.autoGeneratedKeys = autoGeneratedKeys;
	}

	/*
	 * java.sql.PreparedStatement interface
	 */

	public ResultSet executeQuery() throws SQLException {
		try {
			synchronized (con) {
				synchronized (this) {
				  	long begin = 0;

	    			setShardId(UShardInfo.SHARD_ID_INVALID);

				   	u_con.setBeginTime();
					if (u_con.getLogSlowQuery()) {
				   		begin = System.currentTimeMillis();
				  	}

					checkIsOpen();
					if (!completed) {
						complete();
					}
					checkIsOpen();
					if ((!first_result_type)
							&& (u_stmt.getCommandType() != CUBRIDCommandType.CUBRID_STMT_CALL_SP)) {
						throw con.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_query_type_for_executeQuery, null);
					}
					executeCore(false);
					getMoreResults();
					if (current_result_set != null)
						current_result_set.complete_on_close = true;
					if (u_con.getLogSlowQuery()) {
					    	long end = System.currentTimeMillis();
						u_con.logSlowQuery(begin, end, u_stmt.getQuery(), u_stmt.getBindParameter());
					}
					return current_result_set;
				}
			}
		} catch (NullPointerException e) {
		    	throw new CUBRIDException(CUBRIDJDBCErrorCode.prepared_statement_closed);
		}
	}

	public int executeUpdate() throws SQLException {
		try {
			synchronized (con) {
				synchronized (this) {
				   	long begin = 0;

	    			setShardId(UShardInfo.SHARD_ID_INVALID);

				   	u_con.setBeginTime();
				    if (u_con.getLogSlowQuery()) {
				   		begin = System.currentTimeMillis();
					}

					checkIsOpen();
					if (!completed) {
						complete();
					}
					checkIsOpen();
					if (first_result_type) {
						throw con.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_query_type_for_executeUpdate, null);
					}
					executeCore(false);
					getMoreResults();
					if (autoGeneratedKeys == Statement.RETURN_GENERATED_KEYS
							&& u_stmt.getCommandType() == CUBRIDCommandType.CUBRID_STMT_INSERT)
						MakeAutoGeneratedKeysResultSet();

					if (u_stmt.getCommandType() != CUBRIDCommandType.CUBRID_STMT_CALL_SP) {
						complete();
					}
					if (u_con.getLogSlowQuery()) {
					    	long end = System.currentTimeMillis();
						u_con.logSlowQuery(begin, end, u_stmt.getQuery(), u_stmt.getBindParameter());
					}
					return update_count;
				}
			}
		} catch (NullPointerException e) {
		    	throw new CUBRIDException(CUBRIDJDBCErrorCode.prepared_statement_closed);
		}
	}

	public synchronized void setNull(int parameterIndex, int sqlType)
			throws SQLException {
		checkIsOpen();
		synchronized (u_stmt) {
			u_stmt.bindNull(parameterIndex - 1);
			error = u_stmt.getRecentError();
		}
		checkBindError();
	}

	public synchronized void setBoolean(int parameterIndex, boolean x)
			throws SQLException {
		checkIsOpen();
		synchronized (u_stmt) {
			u_stmt.bind(parameterIndex - 1, x);
			error = u_stmt.getRecentError();
		}
		checkBindError();
	}

	public synchronized void setByte(int parameterIndex, byte x)
			throws SQLException {
		checkIsOpen();
		synchronized (u_stmt) {
			u_stmt.bind(parameterIndex - 1, x);
			error = u_stmt.getRecentError();
		}
		checkBindError();
	}

	public synchronized void setShort(int parameterIndex, short x)
			throws SQLException {
		checkIsOpen();
		synchronized (u_stmt) {
			u_stmt.bind(parameterIndex - 1, x);
			error = u_stmt.getRecentError();
		}
		checkBindError();
	}

	public synchronized void setInt(int parameterIndex, int x)
			throws SQLException {
		checkIsOpen();
		synchronized (u_stmt) {
			u_stmt.bind(parameterIndex - 1, x);
			error = u_stmt.getRecentError();
		}
		checkBindError();
	}

	public synchronized void setLong(int parameterIndex, long x)
			throws SQLException {
		checkIsOpen();
		synchronized (u_stmt) {
			u_stmt.bind(parameterIndex - 1, x);
			error = u_stmt.getRecentError();
		}
		checkBindError();
	}

	public synchronized void setFloat(int parameterIndex, float x)
			throws SQLException {
		checkIsOpen();
		synchronized (u_stmt) {
			u_stmt.bind(parameterIndex - 1, x);
			error = u_stmt.getRecentError();
		}
		checkBindError();
	}

	public synchronized void setDouble(int parameterIndex, double x)
			throws SQLException {
		checkIsOpen();
		synchronized (u_stmt) {
			u_stmt.bind(parameterIndex - 1, x);
			error = u_stmt.getRecentError();
		}
		checkBindError();
	}

	public synchronized void setBigDecimal(int parameterIndex, BigDecimal x)
			throws SQLException {
		checkIsOpen();
		synchronized (u_stmt) {
			u_stmt.bind(parameterIndex - 1, x);
			error = u_stmt.getRecentError();
		}
		checkBindError();
	}

	public synchronized void setString(int parameterIndex, String x)
			throws SQLException {
		checkIsOpen();
		synchronized (u_stmt) {
			u_stmt.bind(parameterIndex - 1, x);
			error = u_stmt.getRecentError();
		}
		checkBindError();
	}

	public synchronized void setBytes(int parameterIndex, byte[] x)
			throws SQLException {
		checkIsOpen();
		synchronized (u_stmt) {
			u_stmt.bind(parameterIndex - 1, x);
			error = u_stmt.getRecentError();
		}
		checkBindError();
	}

	public synchronized void setDate(int parameterIndex, Date x)
			throws SQLException {
		checkIsOpen();
		synchronized (u_stmt) {
			u_stmt.bind(parameterIndex - 1, x);
			error = u_stmt.getRecentError();
		}
		checkBindError();
	}

	public synchronized void setTime(int parameterIndex, Time x)
			throws SQLException {
		checkIsOpen();
		synchronized (u_stmt) {
			u_stmt.bind(parameterIndex - 1, x);
			error = u_stmt.getRecentError();
		}
		checkBindError();
	}

	public synchronized void setTimetz(int parameterIndex, CUBRIDTimetz x)
			throws SQLException {
		checkIsOpen();
		synchronized (u_stmt) {
			u_stmt.bind(parameterIndex - 1, x);
			error = u_stmt.getRecentError();
		}
		checkBindError();
	}

	public synchronized void setTimestamp(int parameterIndex, Timestamp x)
			throws SQLException {
		checkIsOpen();
		synchronized (u_stmt) {
			u_stmt.bind(parameterIndex - 1, x);
			error = u_stmt.getRecentError();
		}
		checkBindError();
	}

	public synchronized void setTimestamptz(int parameterIndex, CUBRIDTimestamptz x)
			throws SQLException {
		checkIsOpen();
		synchronized (u_stmt) {
			u_stmt.bind(parameterIndex - 1, x);
			error = u_stmt.getRecentError();
		}
		checkBindError();
	}

	public synchronized void setAsciiStream(int parameterIndex, InputStream x,
			int length) throws SQLException {
		checkIsOpen();

		if (x == null) {
			synchronized (u_stmt) {
				u_stmt.bind(parameterIndex - 1, x);
				error = u_stmt.getRecentError();
			}
			checkBindError();
			return;
		}

		if (length < 0) {
			throw new IllegalArgumentException();
		}

		byte[] value = new byte[length];
		int len = 0;

		try {
			len = x.read(value);
		} catch (IOException e) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.ioexception_in_stream, e);
		}

		synchronized (u_stmt) {
			u_stmt.bind(parameterIndex - 1, new String(value, 0, len));
			error = u_stmt.getRecentError();
		}

		checkBindError();
	}

	public void setUnicodeStream(int parameterIndex, InputStream x, int length)
			throws SQLException {
		throw new SQLException(new UnsupportedOperationException());
	}

	public synchronized void setBinaryStream(int parameterIndex, InputStream x,
			int length) throws SQLException {
		checkIsOpen();

		if (x == null) {
			synchronized (u_stmt) {
				u_stmt.bind(parameterIndex - 1, x);
				error = u_stmt.getRecentError();
			}
			checkBindError();
			return;
		}

		if (length < 0) {
			throw new IllegalArgumentException();
		}

		byte[] value = new byte[length];
		int len = 0;

		try {
			len = x.read(value);
		} catch (IOException e) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.ioexception_in_stream, e);
		}

		byte[] value2 = new byte[len];
		for (int i = 0; i < len; i++) {
			value2[i] = value[i];
		}

		synchronized (u_stmt) {
			u_stmt.bind(parameterIndex - 1, value2);
			error = u_stmt.getRecentError();
		}

		checkBindError();
	}

	public synchronized void clearParameters() throws SQLException {
		checkIsOpen();

		synchronized (u_stmt) {
			u_stmt.clearBind();
			error = u_stmt.getRecentError();
		}

		switch (error.getErrorCode()) {
		case UErrorCode.ER_NO_ERROR:
			break;
		default:
			throw con.createCUBRIDException(error);
		}
	}

	public synchronized void setObject(int parameterIndex, Object x,
			int targetSqlType, int scale) throws SQLException {
		if (x instanceof Blob) {
			setBlob(parameterIndex, (Blob) x);
			return;
		} else if (x instanceof Clob) {
			setClob(parameterIndex, (Clob) x);
			return;
		}

		checkIsOpen();

		synchronized (u_stmt) {
			if (targetSqlType == java.sql.Types.NUMERIC
					|| targetSqlType == java.sql.Types.DECIMAL) {
				Number n = null;
				try {
					n = (Number) x;
				} catch (Exception e) {
					u_stmt.bind(parameterIndex - 1, x);
				}
				u_stmt.bind(parameterIndex - 1, new BigDecimal(n.toString())
						.setScale(scale));
			} else {
				u_stmt.bind(parameterIndex - 1, x);
			}

			error = u_stmt.getRecentError();
		}

		checkBindError();
	}

	public synchronized void setObject(int parameterIndex, Object x,
			int targetSqlType) throws SQLException {
		checkIsOpen();
		
		setObject(parameterIndex, x);
	}

	public synchronized void setObject(int parameterIndex, Object x)
			throws SQLException {
		if (x instanceof Blob) {
			setBlob(parameterIndex, (Blob) x);
			return;
		} else if (x instanceof Clob) {
			setClob(parameterIndex, (Clob) x);
			return;
		}

		checkIsOpen();
		synchronized (u_stmt) {
			u_stmt.bind(parameterIndex - 1, x);
			error = u_stmt.getRecentError();
		}
		checkBindError();
	}

	public boolean execute() throws SQLException {
		try {
			synchronized (con) {
				synchronized (this) {
				   	long begin = 0;

	    			setShardId(UShardInfo.SHARD_ID_INVALID);

				   	u_con.setBeginTime();
				   	if (u_con.getLogSlowQuery()) {
				   		begin = System.currentTimeMillis();
				   	}

					checkIsOpen();
					if (!completed) {
						complete();
					}
					checkIsOpen();

					if (autoGeneratedKeys == Statement.RETURN_GENERATED_KEYS
							&& u_stmt.getCommandType() == CUBRIDCommandType.CUBRID_STMT_INSERT) {
						u_stmt.setAutoGeneratedKeys(true);
					}

					executeCore(true);
					getMoreResults();
					if (autoGeneratedKeys == Statement.RETURN_GENERATED_KEYS
							&& u_stmt.getCommandType() == CUBRIDCommandType.CUBRID_STMT_INSERT) {
						MakeAutoGeneratedKeysResultSet();
					}

					if (u_stmt.getNumQueriesExecuted() == 1) {
						if (current_result_set != null) {
							current_result_set.complete_on_close = true;
						}

						int cmdType = u_stmt.getCommandType();
						if (cmdType != CUBRIDCommandType.CUBRID_STMT_CALL_SP
								&& cmdType != CUBRIDCommandType.CUBRID_STMT_SELECT) {
							complete();
						}
					}

					if (u_con.getLogSlowQuery()) {
					    	long end = System.currentTimeMillis();
						u_con.logSlowQuery(begin, end, u_stmt.getQuery(), u_stmt.getBindParameter());
					}
					return first_result_type;
				}
			}
		} catch (NullPointerException e) {
		    	throw new CUBRIDException(CUBRIDJDBCErrorCode.prepared_statement_closed);
		}
	}

	public synchronized void addBatch() throws SQLException {
		checkIsOpen();

		synchronized (u_stmt) {
			u_stmt.addBatch();
			error = u_stmt.getRecentError();
		}

		switch (error.getErrorCode()) {
		case UErrorCode.ER_NO_ERROR:
			break;
		default:
			throw con.createCUBRIDException(error);
		}
	}

	public synchronized void setCharacterStream(int parameterIndex,
			Reader reader, int length) throws SQLException {
		checkIsOpen();

		if (reader == null) {
			synchronized (u_stmt) {
				u_stmt.bind(parameterIndex - 1, reader);
				error = u_stmt.getRecentError();
			}
			checkBindError();
			return;
		}

		if (length < 0) {
			throw new IllegalArgumentException();
		}

		char[] value = new char[length];
		int len = 0;

		try {
			len = reader.read(value);
		} catch (IOException e) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.ioexception_in_stream, e);
		}

		synchronized (u_stmt) {
			/*
			 * try { u_stmt.bindCharacterStream(parameterIndex-1, new
			 * String(value, 0, len).getBytes("ISO-8859-1")); } catch
			 * (UnsupportedEncodingException e) { }
			 */
			u_stmt.bind(parameterIndex - 1, new String(value, 0, len));
			error = u_stmt.getRecentError();
		}

		checkBindError();
	}

	public void setRef(int i, Ref x) throws SQLException {
		throw new SQLException(new UnsupportedOperationException());
	}

	public void setBlob(int parameterIndex, Blob x) throws SQLException {
		checkIsOpen();
		synchronized (u_stmt) {
			u_stmt.bindBlob(parameterIndex - 1, x);
			error = u_stmt.getRecentError();
		}
		checkBindError();
	}

	public void setClob(int parameterIndex, Clob x) throws SQLException {
		checkIsOpen();
		synchronized (u_stmt) {
			u_stmt.bindClob(parameterIndex - 1, x);
			error = u_stmt.getRecentError();
		}
		checkBindError();
	}

	/* JDK 1.6 */
	public void setBlob(int parameterIndex, InputStream inputStream)
			throws SQLException {
		if (inputStream == null) {
			setNull(parameterIndex, java.sql.Types.BLOB);
			return;
		}

		checkIsOpen();
		Blob blob = con.createBlob();
		OutputStream out = blob.setBinaryStream(1);
		try {
			((CUBRIDBufferedOutputStream) out).streamCopyFromInputStream(
					inputStream, Long.MAX_VALUE);
		} catch (IOException e) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.ioexception_in_stream, e);
		}

		setBlob(parameterIndex, blob);
	}

	/* JDK 1.6 */
	public void setBlob(int parameterIndex, InputStream inputStream, long length)
			throws SQLException {
		if (inputStream == null) {
			setNull(parameterIndex, java.sql.Types.BLOB);
			return;
		}

		checkIsOpen();
		Blob blob = con.createBlob();
		OutputStream out = blob.setBinaryStream(1);
		try {
			((CUBRIDBufferedOutputStream) out).streamCopyFromInputStream(
					inputStream, length);
		} catch (IOException e) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.ioexception_in_stream, e);
		}

		setBlob(parameterIndex, blob);
	}

	/* JDK 1.6 */
	public void setClob(int parameterIndex, Reader reader) throws SQLException {
		if (reader == null) {
			setNull(parameterIndex, java.sql.Types.CLOB);
			return;
		}

		checkIsOpen();
		Clob clob = con.createClob();
		Writer out = clob.setCharacterStream(1);
		try {
			((CUBRIDBufferedWriter) out).streamCopyFromReader(reader,
					Long.MAX_VALUE);
		} catch (IOException e) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.ioexception_in_stream, e);
		}

		setClob(parameterIndex, clob);
	}

	/* JDK 1.6 */
	public void setClob(int parameterIndex, Reader reader, long length)
			throws SQLException {
		if (reader == null) {
			setNull(parameterIndex, java.sql.Types.CLOB);
			return;
		}

		checkIsOpen();
		Clob clob = con.createClob();
		Writer out = clob.setCharacterStream(1);
		try {
			((CUBRIDBufferedWriter) out).streamCopyFromReader(reader, length);
		} catch (IOException e) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.ioexception_in_stream, e);
		}

		setClob(parameterIndex, clob);
	}

	public void setArray(int i, Array x) throws SQLException {
		throw new SQLException(new UnsupportedOperationException());
	}

	public synchronized ResultSetMetaData getMetaData() throws SQLException {
		checkIsOpen();

		UColumnInfo[] col_info = null;
		synchronized (u_stmt) {
			col_info = u_stmt.getColumnInfo();
			error = u_stmt.getRecentError();
		}

		switch (error.getErrorCode()) {
		case UErrorCode.ER_NO_ERROR:
			break;
		default:
			throw con.createCUBRIDException(error);
		}

		if (col_info.length == 0)
			return null;

		return new CUBRIDResultSetMetaData(col_info);
	}

	public synchronized void setDate(int parameterIndex, Date x, Calendar cal)
			throws SQLException {
		setDate(parameterIndex, x);
	}

	public synchronized void setTime(int parameterIndex, Time x, Calendar cal)
			throws SQLException {
		setTime(parameterIndex, x);
	}

	public synchronized void setTimetz(int parameterIndex, CUBRIDTimetz x,
			Calendar cal) throws SQLException {
		setTimetz(parameterIndex, x);
	}

	public synchronized void setTimestamp(int parameterIndex, Timestamp x,
			Calendar cal) throws SQLException {
		setTimestamp(parameterIndex, x);
	}

	public synchronized void setTimestamptz(int parameterIndex, CUBRIDTimestamptz x,
			Calendar cal) throws SQLException {
		setTimestamptz(parameterIndex, x);
	}

	public synchronized void setNull(int paramIndex, int sqlType,
			String typeName) throws SQLException {
		setNull(paramIndex, sqlType);
	}

	public void close() throws SQLException {
		try {
			synchronized (con) {
				synchronized (this) {
	    			setShardId(UShardInfo.SHARD_ID_INVALID);
					if (is_closed)
						return;
					
					complete();
					is_closed = true;

					if (u_stmt != null) {
						u_stmt.close();
						u_stmt = null;
					}

					con.removeStatement(this);
					con = null;
					u_con = null;
					error = null;
				}
			}
		} catch (NullPointerException e) {
		}
	}

	public synchronized void clearBatch() throws SQLException {
		checkIsOpen();

		synchronized (u_stmt) {
			u_stmt.clearBatch();
			error = u_stmt.getRecentError();
		}

		switch (error.getErrorCode()) {
		case UErrorCode.ER_NO_ERROR:
			break;
		default:
			throw con.createCUBRIDException(error);
		}
	}

	public int[] executeBatch() throws SQLException {
		try {
			synchronized (con) {
				synchronized (this) {
	    			setShardId(UShardInfo.SHARD_ID_INVALID);

					checkIsOpen();
					if (!u_stmt.hasBatch()) {
					    return new int[0];
					}
				   	u_con.setBeginTime();

					if (!completed)
						complete();
					checkIsOpen();
					u_stmt.setAutoCommit(u_con.getAutoCommit());
					UBatchResult results = u_stmt.executeBatch(query_timeout);
					setShardId(u_con.getShardId());

					error = u_stmt.getRecentError();
					switch (error.getErrorCode()) {
					case UErrorCode.ER_NO_ERROR:
						break;
					default:
						throw con.createCUBRIDException(error);
					}

					con.autoCommit();
					return (checkBatchResult(results));
				}
			}
		} catch (NullPointerException e) {
		    	throw new CUBRIDException(CUBRIDJDBCErrorCode.prepared_statement_closed);
		}
	}

	// 3.0
	public synchronized ParameterMetaData getParameterMetaData()
			throws SQLException {
		throw new SQLException(new UnsupportedOperationException());
		/*
		 * checkIsOpen();
		 * 
		 * UParameterInfo[] pram_info = null; synchronized (u_stmt) { pram_info
		 * = u_stmt.getParameterInfo(); error = u_stmt.getRecentError(); }
		 * 
		 * switch (error.getErrorCode()) { case UErrorCode.ER_NO_ERROR : break;
		 * default : throw con.createCUBRIDException(error); }
		 * 
		 * if (pram_info.length == 0) return null;
		 * 
		 * return new CUBRIDParameterMetaData(pram_info);
		 */
	}

	public synchronized void setURL(int index, URL x) throws SQLException {
		throw new SQLException(new UnsupportedOperationException());
	}

	// 3.0

	public synchronized void setOID(int parameterIndex, CUBRIDOID x)
			throws SQLException {
		checkIsOpen();

		synchronized (u_stmt) {
			u_stmt.bindOID(parameterIndex - 1, x);
			error = u_stmt.getRecentError();
		}

		checkBindError();
	}

	/**
	 * Sets the designated parameter to a Java <code>Object[]</code> value. The
	 * driver converts this to an <code>CUBRID COLLECTION</code> value when it
	 * sends it to the database.
	 * 
	 * @param parameterIndex
	 *            the first parameter is 1, the second is 2, ...
	 * @param x
	 *            the parameter
	 * @exception SQLException
	 *                if <code>this</code> object is closed.
	 * @exception IllegalArgumentException
	 *                if <code>x</code> is not <code>null</code> and all
	 *                attempts to cast <code>x</code> to one of the following
	 *                types fail.<BR>
	 *                <code>Boolean[], Integer[], Short[], Byte[], Float[], Double[],
	 * Date[], Time[], Timestamp[], String[], byte[][], CUBRIDOID[]</code>
	 * @exception SQLException
	 *                if <code>parameterIndex</code> is out of range.
	 * @exception SQLException
	 *                if a database access error occurs
	 */

	public synchronized void setCollection(int parameterIndex, Object[] x)
			throws SQLException {
		checkIsOpen();
		synchronized (u_stmt) {
			u_stmt.bindCollection(parameterIndex - 1, x);
			error = u_stmt.getRecentError();
		}
		checkBindError();
	}

	/**
	 * Executes an SQL <code>INSERT</code> statement in
	 * <code>this PreparedStatement</code> object and returns a
	 * <code>CUBRIDOID</code> object that represents the OID of the object
	 * inserted by the statement.
	 * 
	 * @return a <code>CUBRIDOID</code> object that represents the OID of the
	 *         object inserted by the statement.
	 * @exception SQLException
	 *                if <code>this</code> object is closed.
	 * @exception SQLException
	 *                if the statement in <code>this PreparedStatement</code>
	 *                object is not an SQL <code>INSERT</code> statement.
	 * @exception SQLException
	 *                if a database access error occurs
	 */

	public CUBRIDOID executeInsert() throws SQLException {
		try {
			synchronized (con) {
				synchronized (this) {
	    			setShardId(UShardInfo.SHARD_ID_INVALID);

				   	u_con.setBeginTime();
				  	checkIsOpen();
					if (!completed) {
						complete();
					}

					if (autoGeneratedKeys == Statement.RETURN_GENERATED_KEYS
							&& u_stmt.getCommandType() == CUBRIDCommandType.CUBRID_STMT_INSERT) {
						u_stmt.setAutoGeneratedKeys(true);
					}

					CUBRIDOID oid = executeInsertCore();
					complete();
					return oid;
				}
			}
		} catch (NullPointerException e) {
		    	throw new CUBRIDException(CUBRIDJDBCErrorCode.prepared_statement_closed);
		}
	}

	public boolean hasResultSet() {
		return first_result_type;
	}

	void complete() throws SQLException {
		if (completed) {
			return;
		}
		completed = true;

		if (current_result_set != null) {
			current_result_set.close();
			current_result_set = null;
		}

		result_info = null;

		if (autoGeneratedKeys == Statement.RETURN_GENERATED_KEYS) {
			con.setAutoGeneratedKeys(true);
		} else {
			con.setAutoGeneratedKeys(false);
		}

		con.autoCommit();
	}

	protected void checkIsOpen() throws SQLException {
		if (is_closed) {
			if (con != null) {
				throw con.createCUBRIDException(CUBRIDJDBCErrorCode.prepared_statement_closed, null);
			} else {
				throw new CUBRIDException(CUBRIDJDBCErrorCode.prepared_statement_closed, null);
			}
			
		}
	}

	protected void checkBindError() throws SQLException {
		switch (error.getErrorCode()) {
		case UErrorCode.ER_NO_ERROR:
			break;
		case UErrorCode.ER_BIND_INDEX:
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_index, null);
		case UErrorCode.ER_INVALID_ARGUMENT:
			throw new IllegalArgumentException();
		default:
			throw con.createCUBRIDException(error);
		}
	}

	/* JDK 1.6 */
	public void setBinaryStream(int parameterIndex, InputStream x)
			throws SQLException {
		// TODO: How to solve it? host variable bind problem
		// setBlob(parameterIndex, x);
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void setBinaryStream(int parameterIndex, InputStream x, long length)
			throws SQLException {
		// TODO: How to solve it? host variable bind problem
		// setBlob(parameterIndex, x, length);
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void setAsciiStream(int parameterIndex, InputStream x)
			throws SQLException {
		// TODO: How to solve it? host variable bind problem
		// setClob(parameterIndex, x);
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void setAsciiStream(int parameterIndex, InputStream x, long length)
			throws SQLException {
		// TODO: How to solve it? host variable bind problem
		// setClob(parameterIndex, x, length);
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void setCharacterStream(int parameterIndex, Reader reader)
			throws SQLException {
		// TODO: How to solve it? host variable bind problem
		// setClob(parameterIndex, reader);
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void setCharacterStream(int parameterIndex, Reader reader,
			long length) throws SQLException {
		// TODO: How to solve it? host variable bind problem
		// setClob(parameterIndex, reader, length);
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void setNCharacterStream(int parameterIndex, Reader value)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void setNCharacterStream(int parameterIndex, Reader value,
			long length) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void setNClob(int parameterIndex, NClob value) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void setNClob(int parameterIndex, Reader reader) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void setNClob(int parameterIndex, Reader reader, long length)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void setNString(int parameterIndex, String value)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void setRowId(int parameterIndex, RowId x) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void setSQLXML(int parameterIndex, SQLXML xmlObject)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

}
