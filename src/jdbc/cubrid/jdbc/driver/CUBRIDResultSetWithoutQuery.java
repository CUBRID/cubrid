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
import java.io.Reader;
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
import java.util.Arrays;
import java.util.Calendar;
import java.util.Comparator;
import java.util.Iterator;
import java.util.Map;

import cubrid.jdbc.jci.UGetTypeConvertedValue;

/**
 * Title: CUBRID JDBC Driver Description:
 * 
 * @version 2.0
 */

class CUBRIDResultSetWithoutQuery implements ResultSet {
	int[] type;
	int[] precision;
	boolean[] nullable;
	String[] column_name;

	private int num_of_columns;
	private int num_of_rows;
	private int current_row;
	private boolean was_null;
	private boolean is_closed;
	private ArrayList<Object[]> rows;
	private CUBRIDResultSetMetaData meta_data;
	private ArrayList<InputStream> streams;
	private int fetch_size;

	protected CUBRIDResultSetWithoutQuery(int columns, int[] types,
			String[] colnames, boolean[] isnull, int[] precision) {
		if (precision == null) {
			precision = new int[columns];
			Arrays.fill(precision, (int) 0);
		}
		initialize(columns, types, colnames, isnull, precision);
	}

	private void initialize(int columns, int[] types, String[] colnames,
			boolean[] isnull, int[] precision) {
		this.num_of_columns = columns;
		this.type = types;
		this.precision = precision;
		this.nullable = isnull;
		this.column_name = colnames;
		this.current_row = -1;
		this.num_of_rows = 0;
		this.was_null = false;
		this.is_closed = false;
		this.rows = new ArrayList<Object[]>(10);
		this.meta_data = null;
		this.streams = new ArrayList<InputStream>();
		this.fetch_size = 0;
	}

	/*
	 * java.sql.ResultSet interface
	 */

	public synchronized boolean next() throws SQLException {
		checkIsOpen();
		clearAllStreams();
		current_row++;

		if (current_row == num_of_rows) {
			close();
			return false;
		}

		return true;
	}

	public synchronized void close() throws SQLException {
		if (is_closed)
			return;
		is_closed = true;
		clearAllStreams();
		type = null;
		precision = null;
		column_name = null;
		rows.clear();
		rows = null;
		meta_data = null;
	}

	public synchronized boolean wasNull() throws SQLException {
		checkIsOpen();
		return was_null;
	}

	public synchronized String getString(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		try {
			return UGetTypeConvertedValue.getString(((Object[]) rows
					.get(current_row))[columnIndex - 1]);
		} catch (Exception e) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.conversion_error);
		}
	}

	public synchronized boolean getBoolean(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		try {
			return UGetTypeConvertedValue.getBoolean(((Object[]) rows
					.get(current_row))[columnIndex - 1]);
		} catch (Exception e) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.conversion_error);
		}
	}

	public synchronized byte getByte(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		try {
			return UGetTypeConvertedValue.getByte(((Object[]) rows
					.get(current_row))[columnIndex - 1]);
		} catch (Exception e) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.conversion_error);
		}
	}

	public synchronized short getShort(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		try {
			return UGetTypeConvertedValue.getShort(((Object[]) rows
					.get(current_row))[columnIndex - 1]);
		} catch (Exception e) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.conversion_error);
		}
	}

	public synchronized int getInt(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		try {
			return UGetTypeConvertedValue.getInt(((Object[]) rows
					.get(current_row))[columnIndex - 1]);
		} catch (Exception e) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.conversion_error);
		}
	}

	public synchronized long getLong(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		try {
			return UGetTypeConvertedValue.getLong(((Object[]) rows
					.get(current_row))[columnIndex - 1]);
		} catch (Exception e) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.conversion_error);
		}
	}

	public synchronized float getFloat(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		try {
			return UGetTypeConvertedValue.getFloat(((Object[]) rows
					.get(current_row))[columnIndex - 1]);
		} catch (Exception e) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.conversion_error);
		}
	}

	public synchronized double getDouble(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		try {
			return UGetTypeConvertedValue.getDouble(((Object[]) rows
					.get(current_row))[columnIndex - 1]);
		} catch (Exception e) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.conversion_error);
		}
	}

	public BigDecimal getBigDecimal(int columnIndex, int scale)
			throws SQLException {
		throw new SQLException(new UnsupportedOperationException());
	}

	public synchronized byte[] getBytes(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		try {
			return UGetTypeConvertedValue.getBytes(((Object[]) rows
					.get(current_row))[columnIndex - 1]);
		} catch (Exception e) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.conversion_error);
		}
	}

	public synchronized Date getDate(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		try {
			return UGetTypeConvertedValue.getDate(((Object[]) rows
					.get(current_row))[columnIndex - 1]);
		} catch (Exception e) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.conversion_error);
		}
	}

	public synchronized Time getTime(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		try {
			return UGetTypeConvertedValue.getTime(((Object[]) rows
					.get(current_row))[columnIndex - 1]);
		} catch (Exception e) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.conversion_error);
		}
	}

	public synchronized Timestamp getTimestamp(int columnIndex)
			throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		try {
			return UGetTypeConvertedValue.getTimestamp(((Object[]) rows
					.get(current_row))[columnIndex - 1]);
		} catch (Exception e) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.conversion_error);
		}
	}

	public synchronized InputStream getAsciiStream(int columnIndex)
			throws SQLException {
		checkIsOpen();
		String str = getString(columnIndex);
		if (str == null)
			return null;
		InputStream stream = new CUBRIDInputStream(str.getBytes());
		addStream(stream);
		return stream;
	}

	public InputStream getUnicodeStream(int columnIndex) throws SQLException {
		throw new SQLException(new UnsupportedOperationException());
	}

	public synchronized InputStream getBinaryStream(int columnIndex)
			throws SQLException {
		checkIsOpen();
		byte[] b = getBytes(columnIndex);
		if (b == null)
			return null;
		InputStream stream = new CUBRIDInputStream(b);
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
		throw new SQLException(new UnsupportedOperationException());
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
		throw new SQLException(new UnsupportedOperationException());
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

	public String getCursorName() throws SQLException {
		throw new CUBRIDException(CUBRIDJDBCErrorCode.not_supported);
	}

	public synchronized ResultSetMetaData getMetaData() throws SQLException {
		checkIsOpen();
		if (meta_data == null) {
			meta_data = new CUBRIDResultSetMetaData(this);
		}
		return meta_data;
	}

	public synchronized Object getObject(int columnIndex) throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);
		return ((Object[]) rows.get(current_row))[columnIndex - 1];
	}

	public synchronized Object getObject(String columnName) throws SQLException {
		return getObject(findColumn(columnName));
	}

	public synchronized int findColumn(String columnName) throws SQLException {
		checkIsOpen();
		if (columnName == null) {
			throw new IllegalArgumentException();
		}
		int i;
		for (i = 0; i < this.column_name.length; i++) {
			if (column_name[i].equalsIgnoreCase(columnName))
				return i + 1;
		}
		throw new CUBRIDException(CUBRIDJDBCErrorCode.invalid_column_name);
	}

	public synchronized Reader getCharacterStream(int columnIndex)
			throws SQLException {
		checkIsOpen();
		/* TODO: Implement this java.sql.ResultSet method */
		throw new java.lang.UnsupportedOperationException(
				"Method getCharacterStream() not yet implemented.");
	}

	public synchronized Reader getCharacterStream(String columnName)
			throws SQLException {
		return getCharacterStream(findColumn(columnName));
	}

	public synchronized BigDecimal getBigDecimal(int columnIndex)
			throws SQLException {
		checkIsOpen();
		beforeGetValue(columnIndex);

		try {
			return UGetTypeConvertedValue.getBigDecimal(((Object[]) rows
					.get(current_row))[columnIndex - 1]);
		} catch (Exception e) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.conversion_error);
		}
	}

	public synchronized BigDecimal getBigDecimal(String columnName)
			throws SQLException {
		return getBigDecimal(findColumn(columnName));
	}

	public synchronized boolean isBeforeFirst() throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_scrollable);
	}

	public synchronized boolean isAfterLast() throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_scrollable);
	}

	public synchronized boolean isFirst() throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_scrollable);
	}

	public synchronized boolean isLast() throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_scrollable);
	}

	public synchronized void beforeFirst() throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_scrollable);
	}

	public synchronized void afterLast() throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_scrollable);
	}

	public synchronized boolean first() throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_scrollable);
	}

	public synchronized boolean last() throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_scrollable);
	}

	public synchronized int getRow() throws SQLException {
		checkIsOpen();
		if (current_row < num_of_rows && current_row >= 0) {
			return current_row + 1;
		}
		return 0;
	}

	public synchronized boolean absolute(int row) throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_scrollable);
	}

	public synchronized boolean relative(int rows) throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_scrollable);
	}

	public synchronized boolean previous() throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_scrollable);
	}

	public synchronized void setFetchDirection(int direction)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_scrollable);
	}

	public synchronized int getFetchDirection() throws SQLException {
		checkIsOpen();
		return FETCH_FORWARD;
	}

	public synchronized void setFetchSize(int rows) throws SQLException {
		checkIsOpen();
		if (rows < 0) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.invalid_value);
		}
		fetch_size = rows;
	}

	public synchronized int getFetchSize() throws SQLException {
		checkIsOpen();
		return fetch_size;
	}

	public synchronized int getType() throws SQLException {
		checkIsOpen();
		return TYPE_FORWARD_ONLY;
	}

	public synchronized int getConcurrency() throws SQLException {
		checkIsOpen();
		return CONCUR_READ_ONLY;
	}

	public synchronized boolean rowUpdated() throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_sensitive);
	}

	public synchronized boolean rowInserted() throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_sensitive);
	}

	public synchronized boolean rowDeleted() throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_sensitive);
	}

	public synchronized void updateNull(int columnIndex) throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateBoolean(int columnIndex, boolean x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateByte(int columnIndex, byte x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateShort(int columnIndex, short x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateInt(int columnIndex, int x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateLong(int columnIndex, long x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateFloat(int columnIndex, float x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateDouble(int columnIndex, double x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateBigDecimal(int columnIndex, BigDecimal x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateString(int columnIndex, String x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateBytes(int columnIndex, byte[] x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateDate(int columnIndex, Date x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateTime(int columnIndex, Time x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateTimestamp(int columnIndex, Timestamp x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateAsciiStream(int columnIndex, InputStream x,
			int length) throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateBinaryStream(int columnIndex, InputStream x,
			int length) throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateCharacterStream(int columnIndex, Reader x,
			int length) throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateObject(int columnIndex, Object x, int scale)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateObject(int columnIndex, Object x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateNull(String columnName) throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateBoolean(String columnName, boolean x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateByte(String columnName, byte x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateShort(String columnName, short x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateInt(String columnName, int x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateLong(String columnName, long x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateFloat(String columnName, float x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateDouble(String columnName, double x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateBigDecimal(String columnName, BigDecimal x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateString(String columnName, String x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateBytes(String columnName, byte[] x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateDate(String columnName, Date x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateTime(String columnName, Time x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateTimestamp(String columnName, Timestamp x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateAsciiStream(String columnName,
			InputStream x, int length) throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateBinaryStream(String columnName,
			InputStream x, int length) throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateCharacterStream(String columnName,
			Reader reader, int length) throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateObject(String columnName, Object x, int scale)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateObject(String columnName, Object x)
			throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void insertRow() throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void updateRow() throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void deleteRow() throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void refreshRow() throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_sensitive);
	}

	public synchronized void cancelRowUpdates() throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void moveToInsertRow() throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized void moveToCurrentRow() throws SQLException {
		checkIsOpen();
		throw new CUBRIDException(CUBRIDJDBCErrorCode.non_updatable);
	}

	public synchronized Statement getStatement() throws SQLException {
		checkIsOpen();
		return null;
	}

	public Object getObject(int i, Map<String, Class<?>> map) throws SQLException {
		throw new CUBRIDException(CUBRIDJDBCErrorCode.not_supported);
	}

	public Ref getRef(int i) throws SQLException {
		throw new CUBRIDException(CUBRIDJDBCErrorCode.not_supported);
	}

	public Blob getBlob(int i) throws SQLException {
		throw new CUBRIDException(CUBRIDJDBCErrorCode.not_supported);
	}

	public Clob getClob(int i) throws SQLException {
		throw new CUBRIDException(CUBRIDJDBCErrorCode.not_supported);
	}

	public Array getArray(int i) throws SQLException {
		throw new CUBRIDException(CUBRIDJDBCErrorCode.not_supported);
	}

	public synchronized Object getObject(String colName, Map<String, Class<?>> map)
			throws SQLException {
		throw new CUBRIDException(CUBRIDJDBCErrorCode.not_supported);
	}

	public Ref getRef(String colName) throws SQLException {
		throw new CUBRIDException(CUBRIDJDBCErrorCode.not_supported);
	}

	public Blob getBlob(String colName) throws SQLException {
		throw new CUBRIDException(CUBRIDJDBCErrorCode.not_supported);
	}

	public Clob getClob(String colName) throws SQLException {
		throw new CUBRIDException(CUBRIDJDBCErrorCode.not_supported);
	}

	public Array getArray(String colName) throws SQLException {
		throw new CUBRIDException(CUBRIDJDBCErrorCode.not_supported);
	}

	public synchronized Date getDate(int columnIndex, Calendar cal)
			throws SQLException {
		checkIsOpen();
		return getDate(columnIndex);
	}

	public synchronized Date getDate(String columnName, Calendar cal)
			throws SQLException {
		checkIsOpen();
		return getDate(columnName);
	}

	public synchronized Time getTime(int columnIndex, Calendar cal)
			throws SQLException {
		checkIsOpen();
		return getTime(columnIndex);
	}

	public synchronized Time getTime(String columnName, Calendar cal)
			throws SQLException {
		checkIsOpen();
		return getTime(columnName);
	}

	public synchronized Timestamp getTimestamp(int columnIndex, Calendar cal)
			throws SQLException {
		checkIsOpen();
		return getTimestamp(columnIndex);
	}

	public synchronized Timestamp getTimestamp(String columnName, Calendar cal)
			throws SQLException {
		checkIsOpen();
		return getTimestamp(columnName);
	}

	// 3.0
	public synchronized URL getURL(int columnIndex) throws SQLException {
		throw new CUBRIDException(CUBRIDJDBCErrorCode.not_supported);
	}

	public synchronized URL getURL(String columnName) throws SQLException {
		throw new CUBRIDException(CUBRIDJDBCErrorCode.not_supported);
	}

	public synchronized void updateArray(int columnIndex, Array x)
			throws SQLException {
		throw new CUBRIDException(CUBRIDJDBCErrorCode.not_supported);
	}

	public synchronized void updateArray(String columnName, Array x)
			throws SQLException {
		throw new CUBRIDException(CUBRIDJDBCErrorCode.not_supported);
	}

	public synchronized void updateBlob(int columnIndex, Blob x)
			throws SQLException {
		throw new CUBRIDException(CUBRIDJDBCErrorCode.not_supported);
	}

	public synchronized void updateBlob(String columnName, Blob x)
			throws SQLException {
		throw new CUBRIDException(CUBRIDJDBCErrorCode.not_supported);
	}

	public synchronized void updateClob(int columnIndex, Clob x)
			throws SQLException {
		throw new CUBRIDException(CUBRIDJDBCErrorCode.not_supported);
	}

	public synchronized void updateClob(String columnName, Clob x)
			throws SQLException {
		throw new CUBRIDException(CUBRIDJDBCErrorCode.not_supported);
	}

	public synchronized void updateRef(int columnIndex, Ref x)
			throws SQLException {
		throw new CUBRIDException(CUBRIDJDBCErrorCode.not_supported);
	}

	public synchronized void updateRef(String columnName, Ref x)
			throws SQLException {
		throw new CUBRIDException(CUBRIDJDBCErrorCode.not_supported);
	}

	// 3.0

	synchronized void sortTuples(Comparator<Object> com) {
		Object[] temp = rows.toArray();
		Arrays.sort(temp, com);
		for (int i = 0; i < temp.length; i++) {
			rows.set(i, (Object[]) temp[i]);
		}
	}

	synchronized void addTuple(Object[] tuple) throws SQLException {
		if (tuple.length != num_of_columns) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.invalid_tuple);
		}

		Object[] newtuple = new Object[tuple.length];
		for (int i = 0; i < tuple.length; i++) {
			newtuple[i] = tuple[i];
		}

		rows.add(newtuple);
		num_of_rows++;
	}

	private void checkIsOpen() throws SQLException {
		if (is_closed) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.result_set_closed);
		}
	}

	private void beforeGetValue(int col) throws SQLException {
		if (current_row < 0 || current_row >= num_of_rows) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.invalid_row);
		}

		if (col < 1 || col > num_of_columns) {
			throw new CUBRIDException(CUBRIDJDBCErrorCode.invalid_index);
		}

		was_null = ((Object[]) rows.get(current_row))[col - 1] == null;
	}

	private void addStream(InputStream s) throws SQLException {
		streams.add(s);
	}

	private void clearAllStreams() throws SQLException {
		Iterator<InputStream> iter = streams.iterator();
		try {
			while (iter.hasNext()) {
				((InputStream) iter.next()).close();
				iter.remove();
			}
			streams.clear();
		} catch (IOException e) {
		}
	}

	/* JDK 1.6 */
	public int getHoldability() throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public Reader getNCharacterStream(int columnIndex) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public Reader getNCharacterStream(String columnLabel) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public NClob getNClob(int columnIndex) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public NClob getNClob(String columnLabel) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public String getNString(int columnIndex) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public String getNString(String columnLabel) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public RowId getRowId(int columnIndex) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public RowId getRowId(String columnLabel) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public SQLXML getSQLXML(int columnIndex) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public SQLXML getSQLXML(String columnLabel) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public boolean isClosed() throws SQLException {
		return is_closed;
	}

	/* JDK 1.6 */
	public void updateAsciiStream(int columnIndex, InputStream x)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateAsciiStream(String columnLabel, InputStream x)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());

	}

	/* JDK 1.6 */
	public void updateAsciiStream(int columnIndex, InputStream x, long length)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());

	}

	/* JDK 1.6 */
	public void updateAsciiStream(String columnLabel, InputStream x, long length)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());

	}

	/* JDK 1.6 */
	public void updateBinaryStream(int columnIndex, InputStream x)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());

	}

	/* JDK 1.6 */
	public void updateBinaryStream(String columnLabel, InputStream x)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());

	}

	/* JDK 1.6 */
	public void updateBinaryStream(int columnIndex, InputStream x, long length)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());

	}

	/* JDK 1.6 */
	public void updateBinaryStream(String columnLabel, InputStream x,
			long length) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());

	}

	/* JDK 1.6 */
	public void updateBlob(int columnIndex, InputStream inputStream)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());

	}

	/* JDK 1.6 */
	public void updateBlob(String columnLabel, InputStream inputStream)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());

	}

	/* JDK 1.6 */
	public void updateBlob(int columnIndex, InputStream inputStream, long length)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());

	}

	/* JDK 1.6 */
	public void updateBlob(String columnLabel, InputStream inputStream,
			long length) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());

	}

	/* JDK 1.6 */
	public void updateCharacterStream(int columnIndex, Reader x)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());

	}

	/* JDK 1.6 */
	public void updateCharacterStream(String columnLabel, Reader reader)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());

	}

	/* JDK 1.6 */
	public void updateCharacterStream(int columnIndex, Reader x, long length)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());

	}

	/* JDK 1.6 */
	public void updateCharacterStream(String columnLabel, Reader reader,
			long length) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateClob(int columnIndex, Reader reader) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateClob(String columnLabel, Reader reader)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateClob(int columnIndex, Reader reader, long length)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateClob(String columnLabel, Reader reader, long length)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateNCharacterStream(int columnIndex, Reader x)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateNCharacterStream(String columnLabel, Reader reader)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateNCharacterStream(int columnIndex, Reader x, long length)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateNCharacterStream(String columnLabel, Reader reader,
			long length) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateNClob(int columnIndex, NClob clob) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateNClob(String columnLabel, NClob clob) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateNClob(int columnIndex, Reader reader) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateNClob(String columnLabel, Reader reader)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateNClob(int columnIndex, Reader reader, long length)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateNClob(String columnLabel, Reader reader, long length)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateNString(int columnIndex, String string)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateNString(String columnLabel, String string)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateRowId(int columnIndex, RowId x) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateRowId(String columnLabel, RowId x) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateSQLXML(int columnIndex, SQLXML xmlObject)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public void updateSQLXML(String columnLabel, SQLXML xmlObject)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public boolean isWrapperFor(Class<?> iface) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.6 */
	public <T> T unwrap(Class<T> iface) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.7 */
	public <T> T getObject(int columnIndex, Class<T> type) throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

	/* JDK 1.7 */
	public <T> T getObject(String columnLabel, Class<T> type)
			throws SQLException {
		throw new SQLException(new java.lang.UnsupportedOperationException());
	}

}
