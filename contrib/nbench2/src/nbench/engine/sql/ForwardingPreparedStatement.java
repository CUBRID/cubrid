package nbench.engine.sql;

import java.io.InputStream;
import java.io.Reader;
import java.math.BigDecimal;
import java.net.URL;
import java.sql.Array;
import java.sql.Blob;
import java.sql.Clob;
import java.sql.Connection;
import java.sql.Date;
import java.sql.NClob;
import java.sql.ParameterMetaData;
import java.sql.PreparedStatement;
import java.sql.Ref;
import java.sql.ResultSet;
import java.sql.ResultSetMetaData;
import java.sql.RowId;
import java.sql.SQLException;
import java.sql.SQLWarning;
import java.sql.SQLXML;
import java.sql.Time;
import java.sql.Timestamp;
import java.util.Calendar;

import nbench.common.PerfLogIfs;
import nbench.common.PerfLogIfs.LogType;

public class ForwardingPreparedStatement implements PreparedStatement {
	private String name;
	private PreparedStatement impl;
	private SilentListener listener;

	private class SilentListener {
		private PerfLogIfs listener;

		SilentListener(PerfLogIfs listener) {
			this.listener = listener;
		}

		public void startLogItem(String s) {
			try {
				listener.startLogItem(System.currentTimeMillis(),
						LogType.QUERY, name + ":" + s);
			} catch (Exception e) {
				;
			}
		}

		public void endLogItem(String s) {
			try {
				listener.endLogItem(System.currentTimeMillis(), LogType.QUERY,
						name + ":" + s);
			} catch (Exception e) {
				;
			}
		}
	}

	public ForwardingPreparedStatement(String name, PreparedStatement impl,
			PerfLogIfs listener) {
		this.name = name;
		this.impl = impl;
		this.listener = new SilentListener(listener);
	}

	@Override
	public void addBatch() throws SQLException {
		impl.addBatch();
	}

	@Override
	public void clearParameters() throws SQLException {
		impl.clearParameters();
	}

	@Override
	public boolean execute() throws SQLException {
		listener.startLogItem("execute()");
		boolean res = impl.execute();
		listener.endLogItem("execute()");
		return res;
	}

	@Override
	public ResultSet executeQuery() throws SQLException {
		listener.startLogItem("executeQuery()");
		ResultSet res = impl.executeQuery();
		listener.endLogItem("executeQuery()");
		return res;
	}

	@Override
	public int executeUpdate() throws SQLException {
		listener.startLogItem("executeUpdate()");
		int res = impl.executeUpdate();
		listener.endLogItem("executeUpdate()");
		return res;
	}

	@Override
	public ResultSetMetaData getMetaData() throws SQLException {
		return impl.getMetaData();
	}

	@Override
	public ParameterMetaData getParameterMetaData() throws SQLException {
		return impl.getParameterMetaData();
	}

	@Override
	public void setArray(int arg0, Array arg1) throws SQLException {
		impl.setArray(arg0, arg1);
	}

	@Override
	public void setAsciiStream(int arg0, InputStream arg1) throws SQLException {
		impl.setAsciiStream(arg0, arg1);
	}

	@Override
	public void setAsciiStream(int arg0, InputStream arg1, int arg2)
			throws SQLException {
		impl.setAsciiStream(arg0, arg1, arg2);
	}

	@Override
	public void setAsciiStream(int arg0, InputStream arg1, long arg2)
			throws SQLException {
		impl.setAsciiStream(arg0, arg1, arg2);
	}

	@Override
	public void setBigDecimal(int arg0, BigDecimal arg1) throws SQLException {
		impl.setBigDecimal(arg0, arg1);
	}

	@Override
	public void setBinaryStream(int arg0, InputStream arg1) throws SQLException {
		impl.setBinaryStream(arg0, arg1);
	}

	@Override
	public void setBinaryStream(int arg0, InputStream arg1, int arg2)
			throws SQLException {
		impl.setBinaryStream(arg0, arg1, arg2);
	}

	@Override
	public void setBinaryStream(int arg0, InputStream arg1, long arg2)
			throws SQLException {
		impl.setBinaryStream(arg0, arg1, arg2);
	}

	@Override
	public void setBlob(int arg0, Blob arg1) throws SQLException {
		impl.setBlob(arg0, arg1);
	}

	@Override
	public void setBlob(int arg0, InputStream arg1) throws SQLException {
		impl.setBlob(arg0, arg1);
	}

	@Override
	public void setBlob(int arg0, InputStream arg1, long arg2)
			throws SQLException {
		impl.setBlob(arg0, arg1, arg2);
	}

	@Override
	public void setBoolean(int arg0, boolean arg1) throws SQLException {
		impl.setBoolean(arg0, arg1);
	}

	@Override
	public void setByte(int arg0, byte arg1) throws SQLException {
		impl.setByte(arg0, arg1);
	}

	@Override
	public void setBytes(int arg0, byte[] arg1) throws SQLException {
		impl.setBytes(arg0, arg1);
	}

	@Override
	public void setCharacterStream(int arg0, Reader arg1) throws SQLException {
		impl.setCharacterStream(arg0, arg1);
	}

	@Override
	public void setCharacterStream(int arg0, Reader arg1, int arg2)
			throws SQLException {
		impl.setCharacterStream(arg0, arg1, arg2);
	}

	@Override
	public void setCharacterStream(int arg0, Reader arg1, long arg2)
			throws SQLException {
		impl.setCharacterStream(arg0, arg1, arg2);
	}

	@Override
	public void setClob(int arg0, Clob arg1) throws SQLException {
		impl.setClob(arg0, arg1);
	}

	@Override
	public void setClob(int arg0, Reader arg1) throws SQLException {
		impl.setClob(arg0, arg1);
	}

	@Override
	public void setClob(int arg0, Reader arg1, long arg2) throws SQLException {
		impl.setClob(arg0, arg1, arg2);
	}

	@Override
	public void setDate(int arg0, Date arg1) throws SQLException {
		impl.setDate(arg0, arg1);
	}

	@Override
	public void setDate(int arg0, Date arg1, Calendar arg2) throws SQLException {
		impl.setDate(arg0, arg1, arg2);
	}

	@Override
	public void setDouble(int arg0, double arg1) throws SQLException {
		impl.setDouble(arg0, arg1);
	}

	@Override
	public void setFloat(int arg0, float arg1) throws SQLException {
		impl.setFloat(arg0, arg1);
	}

	@Override
	public void setInt(int arg0, int arg1) throws SQLException {
		impl.setInt(arg0, arg1);
	}

	@Override
	public void setLong(int arg0, long arg1) throws SQLException {
		impl.setLong(arg0, arg1);
	}

	@Override
	public void setNCharacterStream(int arg0, Reader arg1) throws SQLException {
		impl.setNCharacterStream(arg0, arg1);
	}

	@Override
	public void setNCharacterStream(int arg0, Reader arg1, long arg2)
			throws SQLException {
		impl.setNCharacterStream(arg0, arg1, arg2);
	}

	@Override
	public void setNClob(int arg0, NClob arg1) throws SQLException {
		impl.setNClob(arg0, arg1);
	}

	@Override
	public void setNClob(int arg0, Reader arg1) throws SQLException {
		impl.setNClob(arg0, arg1);
	}

	@Override
	public void setNClob(int arg0, Reader arg1, long arg2) throws SQLException {
		impl.setNClob(arg0, arg1, arg2);
	}

	@Override
	public void setNString(int arg0, String arg1) throws SQLException {
		impl.setNString(arg0, arg1);
	}

	@Override
	public void setNull(int arg0, int arg1) throws SQLException {
		impl.setNull(arg0, arg1);
	}

	@Override
	public void setNull(int arg0, int arg1, String arg2) throws SQLException {
		impl.setNull(arg0, arg1, arg2);
	}

	@Override
	public void setObject(int arg0, Object arg1) throws SQLException {
		impl.setObject(arg0, arg1);
	}

	@Override
	public void setObject(int arg0, Object arg1, int arg2) throws SQLException {
		impl.setObject(arg0, arg1, arg2);
	}

	@Override
	public void setObject(int arg0, Object arg1, int arg2, int arg3)
			throws SQLException {
		impl.setObject(arg0, arg1, arg2, arg3);
	}

	@Override
	public void setRef(int arg0, Ref arg1) throws SQLException {
		impl.setRef(arg0, arg1);
	}

	@Override
	public void setRowId(int arg0, RowId arg1) throws SQLException {
		impl.setRowId(arg0, arg1);
	}

	@Override
	public void setSQLXML(int arg0, SQLXML arg1) throws SQLException {
		impl.setSQLXML(arg0, arg1);
	}

	@Override
	public void setShort(int arg0, short arg1) throws SQLException {
		impl.setShort(arg0, arg1);
	}

	@Override
	public void setString(int arg0, String arg1) throws SQLException {
		impl.setString(arg0, arg1);
	}

	@Override
	public void setTime(int arg0, Time arg1) throws SQLException {
		impl.setTime(arg0, arg1);
	}

	@Override
	public void setTime(int arg0, Time arg1, Calendar arg2) throws SQLException {
		impl.setTime(arg0, arg1, arg2);
	}

	@Override
	public void setTimestamp(int arg0, Timestamp arg1) throws SQLException {
		impl.setTimestamp(arg0, arg1);
	}

	@Override
	public void setTimestamp(int arg0, Timestamp arg1, Calendar arg2)
			throws SQLException {
		impl.setTimestamp(arg0, arg1, arg2);
	}

	@Override
	public void setURL(int arg0, URL arg1) throws SQLException {
		impl.setURL(arg0, arg1);
	}

	@SuppressWarnings("deprecation")
	@Override
	public void setUnicodeStream(int arg0, InputStream arg1, int arg2)
			throws SQLException {
		impl.setUnicodeStream(arg0, arg1, arg2);
	}

	@Override
	public void addBatch(String arg0) throws SQLException {
		impl.addBatch(arg0);
	}

	@Override
	public void cancel() throws SQLException {
		impl.cancel();
	}

	@Override
	public void clearBatch() throws SQLException {
		impl.clearBatch();
	}

	@Override
	public void clearWarnings() throws SQLException {
		impl.clearWarnings();
	}

	@Override
	public void close() throws SQLException {
		impl.close();
	}

	@Override
	public boolean execute(String arg0) throws SQLException {
		listener.startLogItem("execute(String)");
		boolean res = impl.execute(arg0);
		listener.endLogItem("execute(String)");
		return res;
	}

	@Override
	public boolean execute(String arg0, int arg1) throws SQLException {
		listener.startLogItem("execute(String,int)");
		boolean res = impl.execute(arg0, arg1);
		listener.endLogItem("execute(String,int)");
		return res;
	}

	@Override
	public boolean execute(String arg0, int[] arg1) throws SQLException {
		listener.startLogItem("execute(String,int[])");
		boolean res = impl.execute(arg0, arg1);
		listener.endLogItem("execute(String,int[])");
		return res;
	}

	@Override
	public boolean execute(String arg0, String[] arg1) throws SQLException {
		listener.startLogItem("execute(String,String[])");
		boolean res = impl.execute(arg0, arg1);
		listener.endLogItem("execute(String,String[]");
		return res;
	}

	@Override
	public int[] executeBatch() throws SQLException {
		listener.startLogItem("executeBatch()");
		int[] res = impl.executeBatch();
		listener.endLogItem("executeBatch()");
		return res;
	}

	@Override
	public ResultSet executeQuery(String arg0) throws SQLException {
		listener.startLogItem("executeQuery(String)");
		ResultSet res = impl.executeQuery(arg0);
		listener.endLogItem("executeQuery(String)");
		return res;
	}

	@Override
	public int executeUpdate(String arg0) throws SQLException {
		listener.startLogItem("executeUpdate(String)");
		int res = impl.executeUpdate(arg0);
		listener.endLogItem("executeUpdate(String)");
		return res;
	}

	@Override
	public int executeUpdate(String arg0, int arg1) throws SQLException {
		listener.startLogItem("executeUpdate(String,int)");
		int res = impl.executeUpdate(arg0, arg1);
		listener.endLogItem("executeUpdate(String,int)");
		return res;
	}

	@Override
	public int executeUpdate(String arg0, int[] arg1) throws SQLException {
		listener.startLogItem("executeUpdate(String,int[])");
		int res = impl.executeUpdate(arg0, arg1);
		listener.endLogItem("executeUpdate(String,int[]");
		return res;
	}

	@Override
	public int executeUpdate(String arg0, String[] arg1) throws SQLException {
		listener.startLogItem("executeUpdate(String,String[])");
		int res = impl.executeUpdate(arg0, arg1);
		listener.endLogItem("executeUpdate(String,String[])");
		return res;
	}

	@Override
	public Connection getConnection() throws SQLException {
		return impl.getConnection();
	}

	@Override
	public int getFetchDirection() throws SQLException {
		return impl.getFetchDirection();
	}

	@Override
	public int getFetchSize() throws SQLException {
		return impl.getFetchSize();
	}

	@Override
	public ResultSet getGeneratedKeys() throws SQLException {
		return impl.getGeneratedKeys();
	}

	@Override
	public int getMaxFieldSize() throws SQLException {
		return impl.getMaxFieldSize();
	}

	@Override
	public int getMaxRows() throws SQLException {
		return impl.getMaxRows();
	}

	@Override
	public boolean getMoreResults() throws SQLException {
		return impl.getMoreResults();
	}

	@Override
	public boolean getMoreResults(int arg0) throws SQLException {
		return impl.getMoreResults(arg0);
	}

	@Override
	public int getQueryTimeout() throws SQLException {
		return impl.getQueryTimeout();
	}

	@Override
	public ResultSet getResultSet() throws SQLException {
		return impl.getResultSet();
	}

	@Override
	public int getResultSetConcurrency() throws SQLException {
		return impl.getResultSetConcurrency();
	}

	@Override
	public int getResultSetHoldability() throws SQLException {
		return impl.getResultSetHoldability();
	}

	@Override
	public int getResultSetType() throws SQLException {
		return impl.getResultSetType();
	}

	@Override
	public int getUpdateCount() throws SQLException {
		return impl.getUpdateCount();
	}

	@Override
	public SQLWarning getWarnings() throws SQLException {
		return impl.getWarnings();
	}

	@Override
	public boolean isClosed() throws SQLException {
		return impl.isClosed();
	}

	@Override
	public boolean isPoolable() throws SQLException {
		return impl.isPoolable();
	}

	@Override
	public void setCursorName(String arg0) throws SQLException {
		impl.setCursorName(arg0);
	}

	@Override
	public void setEscapeProcessing(boolean arg0) throws SQLException {
		impl.setEscapeProcessing(arg0);
	}

	@Override
	public void setFetchDirection(int arg0) throws SQLException {
		impl.setFetchDirection(arg0);
	}

	@Override
	public void setFetchSize(int arg0) throws SQLException {
		impl.setFetchSize(arg0);
	}

	@Override
	public void setMaxFieldSize(int arg0) throws SQLException {
		impl.setMaxFieldSize(arg0);
	}

	@Override
	public void setMaxRows(int arg0) throws SQLException {
		impl.setMaxRows(arg0);
	}

	@Override
	public void setPoolable(boolean arg0) throws SQLException {
		impl.setPoolable(arg0);
	}

	@Override
	public void setQueryTimeout(int arg0) throws SQLException {
		impl.setQueryTimeout(arg0);
	}

	@Override
	public boolean isWrapperFor(Class<?> arg0) throws SQLException {
		return impl.isWrapperFor(arg0);
	}

	@Override
	public <T> T unwrap(Class<T> arg0) throws SQLException {
		return impl.unwrap(arg0);
	}

}
