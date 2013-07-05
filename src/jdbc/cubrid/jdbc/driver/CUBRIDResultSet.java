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

import java.io.Closeable;
import java.io.IOException;
import java.io.InputStream;
import java.io.Reader;
import java.io.UnsupportedEncodingException;
import java.math.BigDecimal;
import java.net.URL;
import java.sql.Array;
import java.sql.Blob;
import java.sql.Clob;
import java.sql.Date;
import java.sql.NClob;
import java.sql.Ref;
import java.sql.ResultSet;
import java.sql.ResultSetMetaData;
import java.sql.RowId;
import java.sql.SQLException;
import java.sql.SQLWarning;
import java.sql.SQLXML;
import java.sql.Statement;
import java.sql.Time;
import java.sql.Timestamp;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;

import cubrid.jdbc.jci.UColumnInfo;
import cubrid.jdbc.jci.UError;
import cubrid.jdbc.jci.UErrorCode;
import cubrid.jdbc.jci.UStatement;
import cubrid.sql.CUBRIDOID;

/**
 * Title: CUBRID JDBC Driver Description:
 * 
 * @version 2.0
 */

public class CUBRIDResultSet implements ResultSet {
	public boolean complete_on_close;

	private CUBRIDConnection con;
	private CUBRIDStatement stmt;
	protected UStatement u_stmt;
	protected int number_of_rows;
	private int current_row;
	protected UColumnInfo[] column_info;
	protected HashMap<String, Integer> col_name_to_index;
	protected boolean is_closed;
	private boolean was_null;
	private boolean close_u_stmt_on_close;
	protected UError error;
	private CUBRIDResultSetMetaData meta_data;
	protected ArrayList<Object> streams;

	private int type;
	private int concurrency;

	private boolean is_scrollable;
	private boolean is_updatable;
	private boolean is_sensitive;
	private boolean is_holdable;

	private int fetch_direction;
	private int fetch_size;

	private boolean inserting;
	private boolean[] updatable;
	private boolean[] updated;
	private Object[] updates;
	private int number_of_updates;
	private String main_table_name;

	protected CUBRIDResultSet(CUBRIDConnection c, CUBRIDStatement s, int t,
			int concur, boolean holdable) throws SQLException {
		con = c;
		stmt = s;
		u_stmt = s.u_stmt;
		number_of_rows = u_stmt.getExecuteResult();
		current_row = -1;
		column_info = u_stmt.getColumnInfo();
		col_name_to_index = u_stmt.getColumnNameToIndexMap();
		is_closed = false;
		was_null = false;
		complete_on_close = false;
		close_u_stmt_on_close = false;
		meta_data = null;
		streams = new ArrayList<Object>();

		type = t;
		concurrency = concur;
		is_scrollable = t != TYPE_FORWARD_ONLY;
		is_updatable = concur == CONCUR_UPDATABLE;
		is_sensitive = t == TYPE_SCROLL_SENSITIVE;
		is_holdable = false;
		if(holdable && con.u_con.supportHoldableResult()) {
			is_holdable = true;
		}

		fetch_direction = s.getFetchDirection();
		u_stmt.setFetchDirection(fetch_direction);

		fetch_size = s.getFetchSize();
		u_stmt.setFetchSize(fetch_size);

		inserting = false;

		if (is_updatable) {
			updatable = new boolean[column_info.length];
			updated = new boolean[column_info.length];
			updates = new Object[column_info.length];
			main_table_name = null;
			for (int i = 0; i < column_info.length; i++) {
				updatable[i] = column_info[i].getRealColumnName().length() > 0;
				if (updatable[i]) {
					main_table_name = column_info[i].getClassName();
				}
			}
			clearCurrentRow();
		}
	}

	public CUBRIDResultSet(UStatement s) {
		con = null;
		stmt = null;
		u_stmt = s;
		number_of_rows = 1;
		current_row = -1;
		if (u_stmt != null) {
			column_info = u_stmt.getColumnInfo();
			col_name_to_index = u_stmt.getColumnNameToIndexMap();
		}
		is_closed = false;
		was_null = false;
		complete_on_close = false;
		close_u_stmt_on_close = true;
		streams = new ArrayList<Object>();

		type = TYPE_FORWARD_ONLY;
		concurrency = CONCUR_READ_ONLY;
		is_scrollable = false;
		is_updatable = false;
		is_sensitive = false;
		is_holdable = false;

		fetch_direction = FETCH_FORWARD;
		fetch_size = 0;

		inserting = false;
	}

	/*
	 * java.sql.ResultSet interface
	 */

	public boolean next() throws SQLException {
		checkIsOpen();
		
		if (u_stmt == null)
			return false;
		try {
			synchronized (con) {
				synchronized (stmt) {
					synchronized (this) {
						checkIsOpen();
						clearCurrentRow();

						current_row++;
						if (current_row >= number_of_rows) {
							current_row = number_of_rows;
							return false;
						}
						
						if (u_stmt.isFetchCompleted(current_row)) {
							return false;
						}

						move();
						inserting = false;
						return AfterNext();
					}
				}
			}
		} catch (NullPointerException e) {
			synchronized (this) {
				checkIsOpen();
				clearCurrentRow();

				current_row++;
				if (current_row >= number_of_rows) {
					current_row = number_of_rows;
					return false;
				}

				move();
				inserting = false;
				return AfterNext();
			}
		}
	}

	private boolean AfterNext() throws SQLException {
		if (u_stmt == null) {
			return false;
		}

		synchronized (u_stmt) {
			u_stmt.fetch();
			error = u_stmt.getRecentError();
		}

		if (error.getJdbcErrorCode() == UErrorCode.CAS_ER_NO_MORE_DATA) {
			return false;
		}
		return true;
	}

	public void close() throws SQLException {
		try {
			synchronized (con) {
				synchronized (stmt) {
					synchronized (this) {
						if (is_closed) {
							return;
						}
						is_closed = true;

						clearCurrentRow();
						if (stmt.getResultSetHoldability() == ResultSet.HOLD_CURSORS_OVER_COMMIT) {
							u_stmt.closeCursor();
						}
						if (complete_on_close) {
							stmt.complete();
						}
						if (close_u_stmt_on_close) {
							u_stmt.close();
						}

						streams = null;
						stmt = null;
						u_stmt.closeResult();
						if (!u_stmt.isReturnable()) {
							u_stmt = null;
						}
						column_info = null;
						col_name_to_index = null;
						error = null;
						is_holdable = false;
					}
				}
			}
		} catch (NullPointerException e) {
		}
	}

	public synchronized boolean wasNull() throws SQLException {
		checkIsOpen();
		return was_null;
	}

	public synchronized String getString(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		Object obj;
		synchronized (u_stmt) {
			obj = u_stmt.getObject(columnIndex - 1);
			error = u_stmt.getRecentError();
		}
		if (obj != null && obj instanceof Clob) {
			Clob clob = (Clob) obj;
			int length;
			if (clob.length() > (long) Integer.MAX_VALUE) {
				length = Integer.MAX_VALUE;
			} else {
				length = (int) clob.length();
			}
			return clob.getSubString(1, length);
		}

		String value;
		synchronized (u_stmt) {
			value = u_stmt.getString(columnIndex - 1);
			error = u_stmt.getRecentError();
		}

		checkGetXXXError();
		return value;
	}

	public synchronized boolean getBoolean(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		boolean value;
		synchronized (u_stmt) {
			value = u_stmt.getBoolean(columnIndex - 1);
			error = u_stmt.getRecentError();
		}

		checkGetXXXError();
		return value;
	}

	public synchronized byte getByte(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		byte value;
		synchronized (u_stmt) {
			value = u_stmt.getByte(columnIndex - 1);
			error = u_stmt.getRecentError();
		}

		checkGetXXXError();
		return value;
	}

	public synchronized short getShort(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		short value;
		synchronized (u_stmt) {
			value = u_stmt.getShort(columnIndex - 1);
			error = u_stmt.getRecentError();
		}

		checkGetXXXError();
		return value;
	}

	public synchronized int getInt(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		int value;
		synchronized (u_stmt) {
			value = u_stmt.getInt(columnIndex - 1);
			error = u_stmt.getRecentError();
		}

		checkGetXXXError();
		return value;
	}

	public synchronized long getLong(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		long value;
		synchronized (u_stmt) {
			value = u_stmt.getLong(columnIndex - 1);
			error = u_stmt.getRecentError();
		}

		checkGetXXXError();
		return value;
	}

	public synchronized float getFloat(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		float value;
		synchronized (u_stmt) {
			value = u_stmt.getFloat(columnIndex - 1);
			error = u_stmt.getRecentError();
		}

		checkGetXXXError();
		return value;
	}

	public synchronized double getDouble(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		double value;
		synchronized (u_stmt) {
			value = u_stmt.getDouble(columnIndex - 1);
			error = u_stmt.getRecentError();
		}

		checkGetXXXError();
		return value;
	}

	public BigDecimal getBigDecimal(int columnIndex, int scale)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	public synchronized byte[] getBytes(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		Object obj;
		synchronized (u_stmt) {
			obj = u_stmt.getObject(columnIndex - 1);
			error = u_stmt.getRecentError();
		}
		if (obj != null && obj instanceof Blob) {
			Blob blob = (Blob) obj;
			int length;
			if (blob.length() > (long) Integer.MAX_VALUE) {
				length = Integer.MAX_VALUE;
			} else {
				length = (int) blob.length();
			}
			return blob.getBytes(1, length);
		}

		byte[] value;
		synchronized (u_stmt) {
			value = u_stmt.getBytes(columnIndex - 1);
			error = u_stmt.getRecentError();
		}

		checkGetXXXError();
		return value;
	}

	public synchronized Date getDate(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		Date value;
		synchronized (u_stmt) {
			value = u_stmt.getDate(columnIndex - 1);
			error = u_stmt.getRecentError();
		}

		checkGetXXXError();
		return value;
	}

	public synchronized Time getTime(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		Time value;
		synchronized (u_stmt) {
			value = u_stmt.getTime(columnIndex - 1);
			error = u_stmt.getRecentError();
		}

		checkGetXXXError();
		return value;
	}

	public synchronized Timestamp getTimestamp(int columnIndex)
			throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		Timestamp value;
		synchronized (u_stmt) {
			value = u_stmt.getTimestamp(columnIndex - 1);
			error = u_stmt.getRecentError();
		}

		checkGetXXXError();
		return value;
	}

	public synchronized InputStream getAsciiStream(int columnIndex)
			throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		Object obj;
		synchronized (u_stmt) {
			obj = u_stmt.getObject(columnIndex - 1);
			error = u_stmt.getRecentError();
		}
		if (obj != null && obj instanceof Clob) {
			Clob clob = (Clob) obj;
			InputStream stream = clob.getAsciiStream();
			addStream(stream);
			return stream;
		}

		String str;
		synchronized (u_stmt) {
			str = u_stmt.getString(columnIndex - 1);
			error = u_stmt.getRecentError();
		}
		checkGetXXXError();

		if (str == null) {
			return null;
		}

		InputStream stream = new CUBRIDInputStream(str.getBytes());
		addStream(stream);
		return stream;
	}

	public InputStream getUnicodeStream(int columnIndex) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	public synchronized InputStream getBinaryStream(int columnIndex)
			throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		Object obj;
		synchronized (u_stmt) {
			obj = u_stmt.getObject(columnIndex - 1);
			error = u_stmt.getRecentError();
		}
		if (obj != null && obj instanceof Blob) {
			Blob blob = (Blob) obj;
			InputStream stream = blob.getBinaryStream();
			addStream(stream);
			return stream;
		}

		byte[] bytes;
		synchronized (u_stmt) {
			bytes = u_stmt.getBytes(columnIndex - 1);
			error = u_stmt.getRecentError();
		}

		checkGetXXXError();

		if (bytes == null) {
			return null;
		}

		InputStream stream = new CUBRIDInputStream(bytes);
		addStream(stream);
		return stream;
	}

	public synchronized String getString(String columnName) throws SQLException {
		return getString(findColumn(columnName));
	}

	public synchronized boolean getBoolean(String columnName)
			throws SQLException {
		return getBoolean(findColumn(columnName));
	}

	public synchronized byte getByte(String columnName) throws SQLException {
		return getByte(findColumn(columnName));
	}

	public synchronized short getShort(String columnName) throws SQLException {
		return getShort(findColumn(columnName));
	}

	public synchronized int getInt(String columnName) throws SQLException {
		return getInt(findColumn(columnName));
	}

	public synchronized long getLong(String columnName) throws SQLException {
		return getLong(findColumn(columnName));
	}

	public synchronized float getFloat(String columnName) throws SQLException {
		return getFloat(findColumn(columnName));
	}

	public synchronized double getDouble(String columnName) throws SQLException {
		return getDouble(findColumn(columnName));
	}

	public BigDecimal getBigDecimal(String columnName, int scale)
			throws SQLException {
		throw new UnsupportedOperationException();
	}

	public synchronized byte[] getBytes(String columnName) throws SQLException {
		return getBytes(findColumn(columnName));
	}

	public synchronized Date getDate(String columnName) throws SQLException {
		return getDate(findColumn(columnName));
	}

	public synchronized Time getTime(String columnName) throws SQLException {
		return getTime(findColumn(columnName));
	}

	public synchronized Timestamp getTimestamp(String columnName)
			throws SQLException {
		return getTimestamp(findColumn(columnName));
	}

	public synchronized InputStream getAsciiStream(String columnName)
			throws SQLException {
		return getAsciiStream(findColumn(columnName));
	}

	public InputStream getUnicodeStream(String columnName) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	public synchronized InputStream getBinaryStream(String columnName)
			throws SQLException {
		return getBinaryStream(findColumn(columnName));
	}

	public synchronized SQLWarning getWarnings() throws SQLException {
		checkIsOpen();
		return null;
	}

	public synchronized void clearWarnings() throws SQLException {
		checkIsOpen();
	}

	public synchronized String getCursorName() throws SQLException {
		checkIsOpen();
		return "";
	}

	public synchronized ResultSetMetaData getMetaData() throws SQLException {
		checkIsOpen();

		if (meta_data == null) {
			meta_data = new CUBRIDResultSetMetaData(u_stmt.getColumnInfo());
		}

		return meta_data;
	}

	public synchronized Object getObject(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		Object value;
		synchronized (u_stmt) {
			value = u_stmt.getObject(columnIndex - 1);
			error = u_stmt.getRecentError();
		}

		checkGetXXXError();
		return value;
	}

	public synchronized Object getObject(String columnName) throws SQLException {
		return getObject(findColumn(columnName));
	}

	public synchronized int findColumn(String columnName) throws SQLException {
		checkIsOpen();

		Integer index = col_name_to_index.get(columnName.toLowerCase());
		if (index == null) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_column_name, null);
		}

		return index.intValue() + 1;
	}

	public synchronized Reader getCharacterStream(int columnIndex)
			throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		Object obj;
		synchronized (u_stmt) {
			obj = u_stmt.getObject(columnIndex - 1);
			error = u_stmt.getRecentError();
		}
		if (obj != null && obj instanceof Clob) {
			Clob clob = (Clob) obj;
			Reader stream = clob.getCharacterStream();
			addStream(stream);
			return stream;
		}

		String str;
		synchronized (u_stmt) {
			str = u_stmt.getString(columnIndex - 1);
			error = u_stmt.getRecentError();
		}
		checkGetXXXError();

		if (str == null) {
			return null;
		}

		byte[] b = str.getBytes();
		Reader stream = null;
		try {
			stream = new CUBRIDReader(new String(b, "ISO-8859-1"));
		} catch (UnsupportedEncodingException e) {
		}
		addStream(stream);
		return stream;
	}

	public synchronized Reader getCharacterStream(String columnName)
			throws SQLException {
		return getCharacterStream(findColumn(columnName));
	}

	public synchronized BigDecimal getBigDecimal(int columnIndex)
			throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		BigDecimal value;
		synchronized (u_stmt) {
			value = u_stmt.getBigDecimal(columnIndex - 1);
			error = u_stmt.getRecentError();
		}

		checkGetXXXError();
		return value;
	}

	public synchronized BigDecimal getBigDecimal(String columnName)
			throws SQLException {
		return getBigDecimal(findColumn(columnName));
	}

	public synchronized boolean isBeforeFirst() throws SQLException {
		checkIsOpen();
		checkIsScrollable();
		if (number_of_rows == 0) {
			return false;
		}
		return current_row == -1;
	}

	public synchronized boolean isAfterLast() throws SQLException {
		checkIsOpen();
		checkIsScrollable();
		if (number_of_rows == 0) {
			return false;
		}
		return current_row == number_of_rows;
	}

	public synchronized boolean isFirst() throws SQLException {
		checkIsOpen();
		checkIsScrollable();
		if (number_of_rows == 0) {
			return false;
		}
		return current_row == 0;
	}

	public synchronized boolean isLast() throws SQLException {
		checkIsOpen();
		checkIsScrollable();
		if (number_of_rows == 0) {
			return false;
		}
		return current_row == number_of_rows - 1;
	}

	public synchronized void beforeFirst() throws SQLException {
		checkIsOpen();
		checkIsScrollable();
		clearCurrentRow();
		current_row = -1;
		inserting = false;
	}

	public synchronized void afterLast() throws SQLException {
		checkIsOpen();
		checkIsScrollable();
		clearCurrentRow();
		current_row = number_of_rows;
		inserting = false;
	}

	public synchronized boolean first() throws SQLException {
		checkIsOpen();
		checkIsScrollable();
		clearCurrentRow();
		inserting = false;
		current_row = 0;
		if (number_of_rows <= 0) {
			return false;
		}
		move();
		return true;
	}

	public synchronized boolean last() throws SQLException {
		checkIsOpen();
		checkIsScrollable();
		clearCurrentRow();
		inserting = false;
		current_row = number_of_rows - 1;
		if (current_row < 0) {
			current_row = -1;
			return false;
		}
		move();
		return true;
	}

	public synchronized int getRow() throws SQLException {
		checkIsOpen();
		if (current_row > -1 && current_row < number_of_rows) {
			return current_row + 1;
		} else {
			return 0;
		}
	}

	public synchronized boolean absolute(int row) throws SQLException {
		checkIsOpen();
		checkIsScrollable();
		clearCurrentRow();

		if (row == 0) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.argument_zero, null);
		}

		if (row > 0) {
			current_row = row - 1;
		} else {
			current_row = number_of_rows + row;
		}

		inserting = false;

		if (current_row < 0) {
			current_row = -1;
			return false;
		}

		if (current_row >= number_of_rows) {
			current_row = number_of_rows;
			return false;
		}

		move();
		return true;
	}

	public synchronized boolean relative(int rows) throws SQLException {
		checkIsOpen();
		checkIsScrollable();
		clearCurrentRow();

		if (current_row == -1 || current_row == number_of_rows) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_row, null);
		}

		current_row += rows;
		inserting = false;

		if (current_row < 0) {
			current_row = -1;
			return false;
		}

		if (current_row >= number_of_rows) {
			current_row = number_of_rows;
			return false;
		}

		move();
		return true;
	}

	public synchronized boolean previous() throws SQLException {
		checkIsOpen();
		checkIsScrollable();
		clearCurrentRow();

		current_row--;
		inserting = false;

		if (current_row < 0) {
			current_row = -1;
			return false;
		}

		move();
		return true;
	}

	public synchronized void setFetchDirection(int direction)
			throws SQLException {
		checkIsOpen();

		if (!is_scrollable) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.non_scrollable, null);
		}

		if (direction != FETCH_FORWARD && direction != FETCH_REVERSE
				&& direction != FETCH_UNKNOWN) {
			throw new IllegalArgumentException();
		}

		fetch_direction = direction;

		synchronized (u_stmt) {
			u_stmt.setFetchDirection(direction);
			error = u_stmt.getRecentError();
		}

		switch (error.getErrorCode()) {
		case UErrorCode.ER_NO_ERROR:
			break;
		default:
			throw con.createCUBRIDException(error);
		}
	}

	public synchronized int getFetchDirection() throws SQLException {
		checkIsOpen();
		return fetch_direction;
	}

	public synchronized void setFetchSize(int rows) throws SQLException {
		checkIsOpen();

		if (rows < 0) {
			throw new IllegalArgumentException();
		}

		fetch_size = rows;

		synchronized (u_stmt) {
			u_stmt.setFetchSize(rows);
			error = u_stmt.getRecentError();
		}

		switch (error.getErrorCode()) {
		case UErrorCode.ER_NO_ERROR:
			break;
		default:
			throw con.createCUBRIDException(error);
		}
	}

	public synchronized int getFetchSize() throws SQLException {
		checkIsOpen();
		return fetch_size;
	}

	public synchronized int getType() throws SQLException {
		checkIsOpen();
		return type;
	}

	public synchronized int getConcurrency() throws SQLException {
		checkIsOpen();
		return concurrency;
	}

	public synchronized boolean rowUpdated() throws SQLException {
		checkIsOpen();
		checkIsSensitive();
		checkRowIsValidForGet();
		return false;
	}

	public synchronized boolean rowInserted() throws SQLException {
		checkIsOpen();
		checkIsSensitive();
		checkRowIsValidForGet();
		return false;
	}

	public synchronized boolean rowDeleted() throws SQLException {
		checkIsOpen();
		checkIsSensitive();
		checkRowIsValidForGet();

		boolean b = false;
		synchronized (u_stmt) {
			u_stmt.fetch();
			b = u_stmt.cursorIsInstance(current_row);
			error = u_stmt.getRecentError();
		}

		switch (error.getErrorCode()) {
		case UErrorCode.ER_NO_ERROR:
			break;
		default:
			throw con.createCUBRIDException(error);
		}

		return !b;
	}

	public synchronized void updateNull(int columnIndex) throws SQLException {
		updateValue(columnIndex, null);
	}

	public synchronized void updateBoolean(int columnIndex, boolean x)
			throws SQLException {
		updateValue(columnIndex, new Boolean(x));
	}

	public synchronized void updateByte(int columnIndex, byte x)
			throws SQLException {
		updateValue(columnIndex, new Byte(x));
	}

	public synchronized void updateShort(int columnIndex, short x)
			throws SQLException {
		updateValue(columnIndex, new Short(x));
	}

	public synchronized void updateInt(int columnIndex, int x)
			throws SQLException {
		updateValue(columnIndex, new Integer(x));
	}

	public synchronized void updateLong(int columnIndex, long x)
			throws SQLException {
		updateValue(columnIndex, new Long(x));
	}

	public synchronized void updateFloat(int columnIndex, float x)
			throws SQLException {
		updateValue(columnIndex, new Float(x));
	}

	public synchronized void updateDouble(int columnIndex, double x)
			throws SQLException {
		updateValue(columnIndex, new Double(x));
	}

	public synchronized void updateBigDecimal(int columnIndex, BigDecimal x)
			throws SQLException {
		updateValue(columnIndex, x);
	}

	public synchronized void updateString(int columnIndex, String x)
			throws SQLException {
		updateValue(columnIndex, x);
	}

	public synchronized void updateBytes(int columnIndex, byte[] x)
			throws SQLException {
		updateValue(columnIndex, x);
	}

	public synchronized void updateDate(int columnIndex, Date x)
			throws SQLException {
		updateValue(columnIndex, x);
	}

	public synchronized void updateTime(int columnIndex, Time x)
			throws SQLException {
		updateValue(columnIndex, x);
	}

	public synchronized void updateTimestamp(int columnIndex, Timestamp x)
			throws SQLException {
		updateValue(columnIndex, x);
	}

	public synchronized void updateAsciiStream(int columnIndex, InputStream x,
			int length) throws SQLException {
		checkIsOpen();
		checkIsUpdatable();
		checkRowIsValidForUpdate();
		checkColumnIsValid(columnIndex);
		checkColumnIsUpdatable(columnIndex);

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

		if (len == -1) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.empty_inputstream, null);
		}
		updateValue(columnIndex, new String(value, 0, len));
	}

	public synchronized void updateBinaryStream(int columnIndex, InputStream x,
			int length) throws SQLException {
		checkIsOpen();
		checkIsUpdatable();
		checkRowIsValidForUpdate();
		checkColumnIsValid(columnIndex);
		checkColumnIsUpdatable(columnIndex);

		if (length < 0) {
			throw new IllegalArgumentException();
		}

		byte[] value = new byte[length];
		int len = 0;
		try {
			len = x.read(value);
		} catch (IOException e) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.ioexception_in_stream, null);
		}

		if (len == -1) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.empty_inputstream, null);
		}

		byte[] value2 = new byte[len];
		for (int i = 0; i < len; i++) {
			value2[i] = value[i];
		}

		updateValue(columnIndex, value2);
	}

	public synchronized void updateCharacterStream(int columnIndex, Reader x,
			int length) throws SQLException {
		checkIsOpen();
		checkIsUpdatable();
		checkRowIsValidForUpdate();
		checkColumnIsValid(columnIndex);
		checkColumnIsUpdatable(columnIndex);

		if (length < 0) {
			throw new IllegalArgumentException();
		}

		char[] value = new char[length];
		int len = 0;
		try {
			len = x.read(value);
		} catch (IOException e) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.ioexception_in_stream, e);
		}

		if (len == -1) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.empty_reader, null);
		}

		try {
			updateValue(columnIndex, new String(value, 0, len)
					.getBytes("ISO-8859-1"));
		} catch (UnsupportedEncodingException e) {
		}
	}

	public synchronized void updateObject(int columnIndex, Object x, int scale)
			throws SQLException {
		try {
			updateObject(columnIndex, new BigDecimal(((Number) x).toString())
					.setScale(scale));
		} catch (SQLException e) {
			throw e;
		} catch (Exception e) {
			updateObject(columnIndex, x);
		}
	}

	public synchronized void updateObject(int columnIndex, Object x)
			throws SQLException {
		updateValue(columnIndex, x);
	}

	public synchronized void updateNull(String columnName) throws SQLException {
		updateNull(findColumn(columnName));
	}

	public synchronized void updateBoolean(String columnName, boolean x)
			throws SQLException {
		updateBoolean(findColumn(columnName), x);
	}

	public synchronized void updateByte(String columnName, byte x)
			throws SQLException {
		updateByte(findColumn(columnName), x);
	}

	public synchronized void updateShort(String columnName, short x)
			throws SQLException {
		updateShort(findColumn(columnName), x);
	}

	public synchronized void updateInt(String columnName, int x)
			throws SQLException {
		updateInt(findColumn(columnName), x);
	}

	public synchronized void updateLong(String columnName, long x)
			throws SQLException {
		updateLong(findColumn(columnName), x);
	}

	public synchronized void updateFloat(String columnName, float x)
			throws SQLException {
		updateFloat(findColumn(columnName), x);
	}

	public synchronized void updateDouble(String columnName, double x)
			throws SQLException {
		updateDouble(findColumn(columnName), x);
	}

	public synchronized void updateBigDecimal(String columnName, BigDecimal x)
			throws SQLException {
		updateBigDecimal(findColumn(columnName), x);
	}

	public synchronized void updateString(String columnName, String x)
			throws SQLException {
		updateString(findColumn(columnName), x);
	}

	public synchronized void updateBytes(String columnName, byte[] x)
			throws SQLException {
		updateBytes(findColumn(columnName), x);
	}

	public synchronized void updateDate(String columnName, Date x)
			throws SQLException {
		updateDate(findColumn(columnName), x);
	}

	public synchronized void updateTime(String columnName, Time x)
			throws SQLException {
		updateTime(findColumn(columnName), x);
	}

	public synchronized void updateTimestamp(String columnName, Timestamp x)
			throws SQLException {
		updateTimestamp(findColumn(columnName), x);
	}

	public synchronized void updateAsciiStream(String columnName,
			InputStream x, int length) throws SQLException {
		updateAsciiStream(findColumn(columnName), x, length);
	}

	public synchronized void updateBinaryStream(String columnName,
			InputStream x, int length) throws SQLException {
		updateBinaryStream(findColumn(columnName), x, length);
	}

	public synchronized void updateCharacterStream(String columnName,
			Reader reader, int length) throws SQLException {
		updateCharacterStream(findColumn(columnName), reader, length);
	}

	public synchronized void updateObject(String columnName, Object x, int scale)
			throws SQLException {
		updateObject(findColumn(columnName), x, scale);
	}

	public synchronized void updateObject(String columnName, Object x)
			throws SQLException {
		updateObject(findColumn(columnName), x);
	}

	public synchronized void insertRow() throws SQLException {
		try {
			synchronized (con) {
				synchronized (this) {
					checkIsOpen();
					checkIsUpdatable();
					if (!inserting) {
						throw con.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_row, null);
					}

					if (main_table_name == null) {
						return;
					}

					String sql = "insert into [" + main_table_name + "] (";

					boolean first = true;
					for (int i = 0; i < column_info.length; i++) {
						if (updated[i] == false)
							continue;
						if (!first)
							sql += ",";
						first = false;
						sql += "[" + column_info[i].getRealColumnName() + "]";
					}

					sql += ") values (";

					first = true;
					for (int i = 0; i < column_info.length; i++) {
						if (updated[i] == false)
							continue;
						if (!first)
							sql += ",";
						first = false;
						sql += valueToString(updates[i]);
					}

					sql += ")";

					UStatement t_u_stmt = con.prepare(sql, (byte) 0);
					t_u_stmt.execute(false, 0, 0, false, false, false, false,
							false, false, null, 0);

					error = t_u_stmt.getRecentError();
					t_u_stmt.close();
					if (error.getErrorCode() != UErrorCode.ER_NO_ERROR)
						throw con.createCUBRIDException(CUBRIDJDBCErrorCode.insertion_query_fail, null);
				}
			}
		} catch (NullPointerException e) {
			checkIsOpen();
		}
	}

	public synchronized void updateRow() throws SQLException {
		checkIsOpen();
		checkIsUpdatable();
		checkRowIsValidForGet();

		int[] indices = new int[number_of_updates];
		Object[] values = new Object[number_of_updates];

		int j = 0;
		for (int i = 0; i < column_info.length; i++) {
			if (!updated[i])
				continue;
			indices[j] = i;
			values[j++] = updates[i];
		}

		u_stmt.updateRows(current_row, indices, values);
		error = u_stmt.getRecentError();

		switch (error.getErrorCode()) {
		case UErrorCode.ER_NO_ERROR:
			break;
		default:
			throw con.createCUBRIDException(error);
		}

		if (is_sensitive) {
			refreshRow();
		} else {
			clearCurrentRow();
		}
	}

	public synchronized void deleteRow() throws SQLException {
		checkIsOpen();
		checkIsUpdatable();
		checkRowIsValidForGet();

		synchronized (u_stmt) {
			u_stmt.fetch();
			u_stmt.deleteCursor(current_row);
			error = u_stmt.getRecentError();
		}

		switch (error.getErrorCode()) {
		case UErrorCode.ER_NO_ERROR:
			break;
		default:
			throw con.createCUBRIDException(error);
		}
	}

	public synchronized void refreshRow() throws SQLException {
		checkIsOpen();
		checkIsSensitive();
		checkRowIsValidForGet();

		clearCurrentRow();

		u_stmt.reFetch();
		error = u_stmt.getRecentError();

		switch (error.getErrorCode()) {
		case UErrorCode.ER_NO_ERROR:
			break;
		default:
			throw con.createCUBRIDException(error);
		}
	}

	public synchronized void cancelRowUpdates() throws SQLException {
		checkIsOpen();
		checkIsUpdatable();
		checkRowIsValidForUpdate();

		number_of_updates = 0;
		for (int i = 0; i < updates.length; i++) {
			updates[i] = null;
			updated[i] = false;
		}
	}

	public synchronized void moveToInsertRow() throws SQLException {
		checkIsOpen();
		checkIsUpdatable();
		clearCurrentRow();
		inserting = true;
	}

	public synchronized void moveToCurrentRow() throws SQLException {
		checkIsOpen();
		checkIsUpdatable();
		clearCurrentRow();
		inserting = false;
	}

	public synchronized Statement getStatement() throws SQLException {
		checkIsOpen();
		return stmt;
	}

	public Object getObject(int i, Map<String, Class<?>> map) throws SQLException {
		throw new UnsupportedOperationException();
	}

	public Ref getRef(int i) throws SQLException {
		throw new UnsupportedOperationException();
	}

	public synchronized Blob getBlob(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		Blob value;
		synchronized (u_stmt) {
			value = u_stmt.getBlob(columnIndex - 1);
			error = u_stmt.getRecentError();
		}

		checkGetXXXError();
		return value;
	}

	public Clob getClob(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		Clob value;
		synchronized (u_stmt) {
			value = u_stmt.getClob(columnIndex - 1);
			error = u_stmt.getRecentError();
		}

		checkGetXXXError();
		return value;
	}

	public Array getArray(int i) throws SQLException {
		throw new UnsupportedOperationException();
	}

	public Object getObject(String colName, Map<String, Class<?>> map) throws SQLException {
		throw new UnsupportedOperationException();
	}

	public Ref getRef(String colName) throws SQLException {
		throw new UnsupportedOperationException();
	}

	public Blob getBlob(String colName) throws SQLException {
		return (getBlob(findColumn(colName)));
	}

	public Clob getClob(String colName) throws SQLException {
		return (getClob(findColumn(colName)));
	}

	public Array getArray(String colName) throws SQLException {
		throw new UnsupportedOperationException();
	}

	public synchronized Date getDate(int columnIndex, Calendar cal)
			throws SQLException {
		return getDate(columnIndex);
	}

	public synchronized Date getDate(String columnName, Calendar cal)
			throws SQLException {
		return getDate(columnName);
	}

	public synchronized Time getTime(int columnIndex, Calendar cal)
			throws SQLException {
		return getTime(columnIndex);
	}

	public synchronized Time getTime(String columnName, Calendar cal)
			throws SQLException {
		return getTime(columnName);
	}

	public synchronized Timestamp getTimestamp(int columnIndex, Calendar cal)
			throws SQLException {
		return getTimestamp(columnIndex);
	}

	public synchronized Timestamp getTimestamp(String columnName, Calendar cal)
			throws SQLException {
		return getTimestamp(columnName);
	}

	// 3.0
	public synchronized URL getURL(int columnIndex) throws SQLException {
		throw new UnsupportedOperationException();
	}

	public synchronized URL getURL(String columnName) throws SQLException {
		throw new UnsupportedOperationException();
	}

	public synchronized void updateArray(int columnIndex, Array x)
			throws SQLException {
		throw new UnsupportedOperationException();
	}

	public synchronized void updateArray(String columnName, Array x)
			throws SQLException {
		throw new UnsupportedOperationException();
	}

	public synchronized void updateBlob(int columnIndex, Blob x)
			throws SQLException {
		updateValue(columnIndex, x);
	}

	public synchronized void updateBlob(String columnName, Blob x)
			throws SQLException {
		updateValue(findColumn(columnName), x);
	}

	public synchronized void updateClob(int columnIndex, Clob x)
			throws SQLException {
		updateValue(columnIndex, x);
	}

	public synchronized void updateClob(String columnName, Clob x)
			throws SQLException {
		updateValue(findColumn(columnName), x);
	}

	public synchronized void updateRef(int columnIndex, Ref x)
			throws SQLException {
		throw new UnsupportedOperationException();
	}

	public synchronized void updateRef(String columnName, Ref x)
			throws SQLException {
		throw new UnsupportedOperationException();
	}

	// 3.0

	public synchronized CUBRIDOID getOID(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		CUBRIDOID value;
		synchronized (u_stmt) {
			value = u_stmt.getColumnOID(columnIndex - 1);
			error = u_stmt.getRecentError();
		}

		checkGetXXXError();
		return value;
	}

	public synchronized CUBRIDOID getOID(String columnName) throws SQLException {
		return getOID(findColumn(columnName));
	}

	public synchronized Object getCollection(int columnIndex)
			throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		Object value;
		synchronized (u_stmt) {
			value = u_stmt.getCollection(columnIndex - 1);
			error = u_stmt.getRecentError();
		}

		checkGetXXXError();
		return value;
	}

	public synchronized Object getCollection(String columnName)
			throws SQLException {
		return getCollection(findColumn(columnName));
	}

	/**
	 * Returns a <code>CUBRIDOID</code> object that represents the OID of the
	 * current cursor.
	 * 
	 * @return the <code>CUBRIDOID</code> object of the current cursor if OID is
	 *         included, <code>null</code> otherwise.
	 * @exception SQLException
	 *                if <code>this</code> object is closed.
	 * @exception SQLException
	 *                if the current cursor is on beforeFirst, afterLast or the
	 *                insert row
	 * @exception SQLException
	 *                if a database access error occurs
	 */

	public synchronized CUBRIDOID getOID() throws SQLException {
		checkIsOpen();
		checkRowIsValidForGet();
		CUBRIDOID oid = null;
		synchronized (u_stmt) {
			oid = u_stmt.getCursorOID();
			error = u_stmt.getRecentError();
		}
		switch (error.getErrorCode()) {
		case UErrorCode.ER_NO_ERROR:
			return oid;
		case UErrorCode.ER_OID_IS_NOT_INCLUDED:
			return null;
		default:
			throw con.createCUBRIDException(error);
		}
	}

	private void checkIsOpen() throws SQLException {
		if (is_closed) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.result_set_closed, null);
		}
	}

	private void checkIsUpdatable() throws SQLException {
		if (!is_updatable) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.non_updatable, null);
		}
	}

	private void checkIsSensitive() throws SQLException {
		if (!is_sensitive) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.non_sensitive, null);
		}
	}

	private void checkIsScrollable() throws SQLException {
		if (!is_scrollable) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.non_scrollable, null);
		}
	}

	private void checkRowIsValidForGet() throws SQLException {
		if (current_row == number_of_rows || current_row == -1 || inserting) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_row, null);
		}
	}

	private void checkRowIsValidForUpdate() throws SQLException {
		if ((current_row == number_of_rows || current_row == -1) && !inserting) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_row, null);
		}
	}

	private void checkColumnIsValid(int columnIndex) throws SQLException {
		if (columnIndex < 1 || columnIndex > column_info.length) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.invalid_index, null);
		}
	}

	private void checkColumnIsUpdatable(int columnIndex) throws SQLException {
		if (updatable[columnIndex - 1] == false) {
			throw con.createCUBRIDException(CUBRIDJDBCErrorCode.non_updatable_column, null);
		}
	}

	private void beforeGetValue(int columnIndex) throws SQLException {
		checkRowIsValidForGet();
		checkColumnIsValid(columnIndex);

		synchronized (u_stmt) {
			u_stmt.fetch();
			error = u_stmt.getRecentError();
		}

		switch (error.getErrorCode()) {
		case UErrorCode.ER_NO_ERROR:
			break;
		default:
			throw con.createCUBRIDException(error);
		}
	}

	private void checkGetXXXError() throws SQLException {
		switch (error.getErrorCode()) {
		case UErrorCode.ER_NO_ERROR:
			was_null = false;
			break;
		case UErrorCode.ER_WAS_NULL:
			was_null = true;
			break;
		default:
			throw con.createCUBRIDException(error);
		}
	}

	private void updateValue(int columnIndex, Object value) throws SQLException {
		checkIsOpen();
		checkIsUpdatable();
		checkRowIsValidForUpdate();
		checkColumnIsValid(columnIndex);
		checkColumnIsUpdatable(columnIndex);

		updates[columnIndex - 1] = value;
		if (updated[columnIndex - 1] == false) {
			updated[columnIndex - 1] = true;
			number_of_updates++;
		}
	}

	protected void clearCurrentRow() throws SQLException {
		Iterator<Object> iter = streams.iterator();
		try {
			while (iter.hasNext()) {
				Object stream = iter.next();
				if (stream instanceof Closeable) {
					((Closeable) stream).close();
				}
				iter.remove();
			}
		} catch (IOException e) {
		}

		if (is_updatable) {
			number_of_updates = 0;
			for (int i = 0; i < updates.length; i++) {
				updates[i] = null;
				updated[i] = false;
			}
		}
	}

	private void move() throws SQLException {
		synchronized (u_stmt) {
			u_stmt.moveCursor(current_row, UStatement.CURSOR_SET);
			error = u_stmt.getRecentError();
		}

		switch (error.getErrorCode()) {
		case UErrorCode.ER_NO_ERROR:
			break;
		default:
			throw con.createCUBRIDException(error);
		}
	}

	private void addStream(Object s) throws SQLException {
		streams.add(s);
	}

	private String valueToString(Object value) throws SQLException {
		if (value == null) {
			return "null";
		}

		String strvalue = null;
		if (value instanceof java.sql.Time) {
			java.text.SimpleDateFormat format = new java.text.SimpleDateFormat(
					"HH:mm:ss");
			strvalue = "'" + format.format((java.util.Date) value) + "'";
		} else if (value instanceof java.sql.Date) {
			java.text.SimpleDateFormat format = new java.text.SimpleDateFormat(
					"MM/dd/yyyy");
			strvalue = "'" + format.format((java.util.Date) value) + "'";
		} else if (value instanceof java.sql.Timestamp) {
			java.text.SimpleDateFormat format = new java.text.SimpleDateFormat(
					"MM/dd/yyyy HH:mm:ss");
			strvalue = "'" + format.format((java.util.Date) value) + "'";
		} else if (value instanceof CUBRIDOID) {
			strvalue = "'" + ((CUBRIDOID) value).getOidString() + "'";
		} else if (value instanceof byte[]) {
			byte[] v = (byte[]) value;
			strvalue = "";
			for (int i = v.length - 1; i >= 0; i--) {
				int t = v[i] + 256;
				for (int j = 0; j < 2; j++) {
					if (t % 16 < 10)
						strvalue = (t % 16) + strvalue;
					if (t % 16 == 10)
						strvalue = "A" + strvalue;
					if (t % 16 == 11)
						strvalue = "B" + strvalue;
					if (t % 16 == 12)
						strvalue = "C" + strvalue;
					if (t % 16 == 13)
						strvalue = "D" + strvalue;
					if (t % 16 == 14)
						strvalue = "E" + strvalue;
					if (t % 16 == 15)
						strvalue = "F" + strvalue;
					t /= 16;
				}
			}
			strvalue = "X'" + strvalue + "'";
		} else if (value instanceof String) {
			strvalue = "'" + value.toString() + "'";
		} else if (value instanceof Boolean) {
			strvalue = "B'";
			strvalue += ((Boolean) value).booleanValue() ? "1" : "0";
			strvalue += "'";
		} else {
			strvalue = value.toString();
		}

		return strvalue;
	}

	public int getServerHandle() {
		if (u_stmt == null || !u_stmt.isReturnable())
			return 0;

		return u_stmt.getServerHandle();
	}

	public void setReturnable() {
		if (!is_closed)
			u_stmt.setReturnable();
	}

	/* JDK 1.6 */
	public int getHoldability() throws SQLException {
		checkIsOpen();
		
		return (is_holdable ? ResultSet.HOLD_CURSORS_OVER_COMMIT :
				ResultSet.CLOSE_CURSORS_AT_COMMIT);
	}

	/* JDK 1.6 */
	public Reader getNCharacterStream(int columnIndex) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public Reader getNCharacterStream(String columnLabel) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public NClob getNClob(int columnIndex) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public NClob getNClob(String columnLabel) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public String getNString(int columnIndex) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public String getNString(String columnLabel) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public RowId getRowId(int columnIndex) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public RowId getRowId(String columnLabel) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public SQLXML getSQLXML(int columnIndex) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public SQLXML getSQLXML(String columnLabel) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public boolean isClosed() throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateAsciiStream(int columnIndex, InputStream x)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateAsciiStream(String columnLabel, InputStream x)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateAsciiStream(int columnIndex, InputStream x, long length)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateAsciiStream(String columnLabel, InputStream x, long length)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateBinaryStream(int columnIndex, InputStream x)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateBinaryStream(String columnLabel, InputStream x)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateBinaryStream(int columnIndex, InputStream x, long length)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateBinaryStream(String columnLabel, InputStream x,
			long length) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateBlob(int columnIndex, InputStream inputStream)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateBlob(String columnLabel, InputStream inputStream)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateBlob(int columnIndex, InputStream inputStream, long length)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateBlob(String columnLabel, InputStream inputStream,
			long length) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateCharacterStream(int columnIndex, Reader x)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateCharacterStream(String columnLabel, Reader reader)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateCharacterStream(int columnIndex, Reader x, long length)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateCharacterStream(String columnLabel, Reader reader,
			long length) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateClob(int columnIndex, Reader reader) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateClob(String columnLabel, Reader reader)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateClob(int columnIndex, Reader reader, long length)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateClob(String columnLabel, Reader reader, long length)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateNCharacterStream(int columnIndex, Reader x)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateNCharacterStream(String columnLabel, Reader reader)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateNCharacterStream(int columnIndex, Reader x, long length)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateNCharacterStream(String columnLabel, Reader reader,
			long length) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateNClob(int columnIndex, NClob clob) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateNClob(String columnLabel, NClob clob) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateNClob(int columnIndex, Reader reader) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateNClob(String columnLabel, Reader reader)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateNClob(int columnIndex, Reader reader, long length)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateNClob(String columnLabel, Reader reader, long length)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateNString(int columnIndex, String string)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateNString(String columnLabel, String string)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateRowId(int columnIndex, RowId x) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateRowId(String columnLabel, RowId x) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateSQLXML(int columnIndex, SQLXML xmlObject)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public void updateSQLXML(String columnLabel, SQLXML xmlObject)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public boolean isWrapperFor(Class<?> iface) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.6 */
	public <T> T unwrap(Class<T> iface) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.7 */
	public <T> T getObject(int columnIndex, Class<T> type) throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

	/* JDK 1.7 */
	public <T> T getObject(String columnLabel, Class<T> type)
			throws SQLException {
		throw new java.lang.UnsupportedOperationException();
	}

}
