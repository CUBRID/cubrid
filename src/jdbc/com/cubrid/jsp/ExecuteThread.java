/*
 * Copyright (C) 2008 Search Solution Corporation
 * Copyright (C) 2016 CUBRID Corporation
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

package com.cubrid.jsp;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.math.BigDecimal;
import java.net.Socket;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.text.SimpleDateFormat;
import java.util.Calendar;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;

import com.cubrid.jsp.exception.ExecuteException;
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.jsp.value.DateValue;
import com.cubrid.jsp.value.DatetimeValue;
import com.cubrid.jsp.value.DoubleValue;
import com.cubrid.jsp.value.FloatValue;
import com.cubrid.jsp.value.IntValue;
import com.cubrid.jsp.value.LongValue;
import com.cubrid.jsp.value.NullValue;
import com.cubrid.jsp.value.NumericValue;
import com.cubrid.jsp.value.OidValue;
import com.cubrid.jsp.value.SetValue;
import com.cubrid.jsp.value.ShortValue;
import com.cubrid.jsp.value.StringValue;
import com.cubrid.jsp.value.TimeValue;
import com.cubrid.jsp.value.TimestampValue;
import com.cubrid.jsp.value.Value;

import cubrid.jdbc.driver.CUBRIDConnection;
import cubrid.jdbc.driver.CUBRIDConnectionDefault;
import cubrid.jdbc.driver.CUBRIDResultSet;
import cubrid.jdbc.jci.UConnection;
import cubrid.jdbc.jci.UJCIUtil;
import cubrid.sql.CUBRIDOID;

public class ExecuteThread extends Thread {
	private String charSet = System.getProperty("file.encoding");

	private static final int REQ_CODE_INVOKE_SP = 0x01;
	private static final int REQ_CODE_RESULT = 0x02;
	private static final int REQ_CODE_ERROR = 0x04;
	private static final int REQ_CODE_INTERNAL_JDBC = 0x08;
	private static final int REQ_CODE_DESTROY = 0x10;
	private static final int REQ_CODE_END = 0x20;

	private static final int REQ_CODE_UTIL_PING = 0xDE;
	private static final int REQ_CODE_UTIL_STATUS = 0xEE;
	private static final int REQ_CODE_UTIL_TERMINATE_THREAD = 0xFE;
	private static final int REQ_CODE_UTIL_TERMINATE_SERVER = 0xFF; // to shutdown javasp server

	/* DB Types */
	public static final int DB_NULL = 0;
	public static final int DB_INT = 1;
	public static final int DB_FLOAT = 2;
	public static final int DB_DOUBLE = 3;
	public static final int DB_STRING = 4;
	public static final int DB_OBJECT = 5;
	public static final int DB_SET = 6;
	public static final int DB_MULTISET = 7;
	public static final int DB_SEQUENCE = 8;
	public static final int DB_TIME = 10;
	public static final int DB_TIMESTAMP = 11;
	public static final int DB_DATE = 12;
	public static final int DB_MONETARY = 13;
	public static final int DB_SHORT = 18;
	public static final int DB_NUMERIC = 22;
	public static final int DB_CHAR = 25;
	public static final int DB_RESULTSET = 28;
	public static final int DB_BIGINT = 31;
	public static final int DB_DATETIME = 32;

	private Socket client;
	private CUBRIDConnectionDefault connection = null;
	private String threadName = null;

	private DataInputStream input;
	private DataOutputStream output;
	private ByteArrayOutputStream byteBuf = new ByteArrayOutputStream(1024);
	private DataOutputStream outBuf = new DataOutputStream(byteBuf);

	private AtomicInteger status = new AtomicInteger(ExecuteThreadStatus.IDLE.getValue());

	private StoredProcedure storedProcedure = null;

	ExecuteThread(Socket client) throws IOException {
		super();
		this.client = client;
		output = new DataOutputStream(new BufferedOutputStream(this.client.getOutputStream()));
	}

	public Socket getSocket() {
		return client;
	}

	public void closeJdbcConnection() throws IOException, SQLException {
		if (connection != null && compareStatus(ExecuteThreadStatus.CALL)) {
			connection.close();
			setStatus (ExecuteThreadStatus.INVOKE);
		}
	}

	public void closeSocket() {
		try {
			byteBuf.close();
			outBuf.close();
			output.close();
			client.close();
		} catch (IOException e) {
		}

		client = null;
		output = null;
		byteBuf = null;
		outBuf = null;
		connection = null;
		charSet = null;
	}

	public void setJdbcConnection(Connection con) {
		this.connection = (CUBRIDConnectionDefault) con;
	}

	public Connection getJdbcConnection() {
		return this.connection;
	}

	public void setStatus (Integer value) {
		this.status.set(value);
	}

	public void setStatus (ExecuteThreadStatus value) {
		this.status.set(value.getValue());
	}

	public Integer getStatus () {
		return status.get();
	}

	public boolean compareStatus (ExecuteThreadStatus value) {
		return (status.get() == value.getValue());
	}

	public void setCharSet(String conCharsetName) {
		this.charSet = conCharsetName;
	}

	public void run() {
		/* main routine handling stored procedure */
		int requestCode = -1;
		while (!Thread.interrupted()) {
			try {
				requestCode = listenCommand();
				switch (requestCode) {
				/* the following two request codes are for processing java stored procedure routine */
				case REQ_CODE_INVOKE_SP: {
					processStoredProcedure();
					break;
				}
				case REQ_CODE_DESTROY: {
					destroyJDBCResources();
					Thread.currentThread().interrupt();
					break;
				}

				/* the following request codes are for javasp utility */
				case REQ_CODE_UTIL_PING: {
					String ping = Server.getServerName();
					output.writeInt (ping.length());
					output.writeBytes (ping);
					output.flush();
					break;
				}
				case REQ_CODE_UTIL_STATUS: {
					String dbName = Server.getServerName();
					List<String> vm_args = Server.getJVMArguments();
					int length = getLengthtoSend(dbName) + 12;
					for (String arg : vm_args) {
						length += getLengthtoSend(arg) + 4;
					}
					output.writeInt (length);
					output.writeInt (Server.getServerPort());
					packAndSendRawString (dbName, output);

					output.writeInt (vm_args.size());
					for (String arg : vm_args) {
						packAndSendRawString (arg, output);
					}
					output.flush();
					break;
				}
				case REQ_CODE_UTIL_TERMINATE_THREAD: {
					Thread.currentThread().interrupt();
					break;
				}
				case REQ_CODE_UTIL_TERMINATE_SERVER: {
					Server.stop (0);
					break;
				}

				/* invalid request */
				default: {
					// throw new ExecuteException ("invalid request code: " + requestCode);
				}
				}
			} catch (Throwable e) {
				if (e instanceof IOException) {
					setStatus(ExecuteThreadStatus.END);
					/* 
					 * CAS disconnects socket
					 * 1) end of the procedure successfully by calling jsp_close_internal_connection ()
					 * 2) socket is in invalid status. we do not have to deal with it here.
					 */
					break;
				} else {
					try {
						closeJdbcConnection ();
					} catch (Exception e2) {
					}
					setStatus (ExecuteThreadStatus.ERROR);
					Throwable throwable = e;
					if (e instanceof InvocationTargetException) {
						throwable = ((InvocationTargetException) e).getTargetException();
					}
					Server.log(throwable);
					try {
						sendError(throwable.toString(), client);
					} catch (IOException e1) {
						Server.log(e1);
					}
				}
			}
		}
		closeSocket();
	}

	private int listenCommand () throws Exception {
		input = new DataInputStream(new BufferedInputStream(this.client.getInputStream()));
		setStatus (ExecuteThreadStatus.IDLE);
		return input.readInt();
	}

	private void processStoredProcedure () throws Exception {
		setStatus (ExecuteThreadStatus.PARSE);
		StoredProcedure procedure = makeStoredProcedure();
		Method m = procedure.getTarget().getMethod();

		if (threadName == null || threadName.equalsIgnoreCase (m.getName())) {
			threadName = m.getName();
			Thread.currentThread().setName(threadName);
		}

		Object[] resolved = procedure.checkArgs(procedure.getArgs());

		setStatus (ExecuteThreadStatus.INVOKE);
		Object result = m.invoke(null, resolved);

		/* close server-side JDBC connection */
		closeJdbcConnection();

		/* send results */
		setStatus (ExecuteThreadStatus.RESULT);
		Value resolvedResult = procedure.makeReturnValue(result);
		sendResult(resolvedResult, procedure);

		setStatus (ExecuteThreadStatus.IDLE);
	}

	private StoredProcedure makeStoredProcedure() throws Exception {
		int methodSigLength = input.readInt();
		byte[] methodSig = new byte[methodSigLength];
		input.readFully(methodSig);

		int paramCount = input.readInt();
		Value[] args = readArguments(input, paramCount);

		int returnType = input.readInt();

		int endCode = input.readInt();
		if (endCode != REQ_CODE_INVOKE_SP) {
			return null;
		}

		storedProcedure = new StoredProcedure(new String(methodSig), args, returnType);
		return storedProcedure;
	}

	private void destroyJDBCResources () throws SQLException, IOException {
		setStatus(ExecuteThreadStatus.DESTROY);

		if (connection != null)
		{
			output.writeInt(REQ_CODE_DESTROY);
			output.flush();
			connection.destroy();
			connection = null;
		}
		else
		{
			output.writeInt(REQ_CODE_END);
			output.flush();
		}
	}

	private Object toDbTypeValue(int dbType, Value result)
			throws TypeMismatchException {
		Object resolvedResult = null;

		if (result == null)
			return null;

		switch (dbType) {
		case DB_INT:
			resolvedResult = result.toIntegerObject();
			break;
		case DB_BIGINT:
			resolvedResult = result.toLongObject();
			break;
		case DB_FLOAT:
			resolvedResult = result.toFloatObject();
			break;
		case DB_DOUBLE:
		case DB_MONETARY:
			resolvedResult = result.toDoubleObject();
			break;
		case DB_CHAR:
		case DB_STRING:
			resolvedResult = result.toString();
			break;
		case DB_SET:
		case DB_MULTISET:
		case DB_SEQUENCE:
			resolvedResult = result.toObjectArray();
			break;
		case DB_TIME:
			resolvedResult = result.toTime();
			break;
		case DB_DATE:
			resolvedResult = result.toDate();
			break;
		case DB_TIMESTAMP:
			resolvedResult = result.toTimestamp();
			break;
		case DB_DATETIME:
			resolvedResult = result.toDatetime();
			break;
		case DB_SHORT:
			resolvedResult = result.toShortObject();
			break;
		case DB_NUMERIC:
			resolvedResult = result.toBigDecimal();
			break;
		case DB_OBJECT:
			resolvedResult = result.toOid();
			break;
		case DB_RESULTSET:
			resolvedResult = result.toResultSet();
			break;
		default:
			break;
		}

		return resolvedResult;
	}

	private void returnOutArgs(StoredProcedure sp, DataOutputStream dos)
			throws IOException, ExecuteException, TypeMismatchException {
		Value[] args = sp.getArgs();
		for (int i = 0; i < args.length; i++) {
			if (args[i].getMode() > Value.IN) {
				Value v = makeOutBingValue(sp, args[i].getResolved());
				sendValue(toDbTypeValue(args[i].getDbType(), v), dos, args[i]
						.getDbType());
			}
		}
	}

	private Value makeOutBingValue(StoredProcedure sp, Object object)
			throws ExecuteException {
		Object obj = null;
		if (object instanceof byte[]) {
			obj = new Byte(((byte[]) object)[0]);
		} else if (object instanceof short[]) {
			obj = new Short(((short[]) object)[0]);
		} else if (object instanceof int[]) {
			obj = new Integer(((int[]) object)[0]);
		} else if (object instanceof long[]) {
			obj = new Long(((long[]) object)[0]);
		} else if (object instanceof float[]) {
			obj = new Float(((float[]) object)[0]);
		} else if (object instanceof double[]) {
			obj = new Double(((double[]) object)[0]);
		} else if (object instanceof byte[][]) {
			obj = ((byte[][]) object)[0];
		} else if (object instanceof short[][]) {
			obj = ((short[][]) object)[0];
		} else if (object instanceof int[][]) {
			obj = ((int[][]) object)[0];
		} else if (object instanceof long[][]) {
			obj = ((long[][]) object)[0];
		} else if (object instanceof float[][]) {
			obj = ((float[][]) object)[0];
		} else if (object instanceof double[][]) {
			obj = ((double[][]) object)[0];
		} else if (object instanceof Object[]) {
			obj = ((Object[]) object)[0];
		}

		return sp.makeReturnValue(obj);
	}

	private void sendResult(Value result, StoredProcedure procedure) throws IOException, ExecuteException, TypeMismatchException {
		Object resolvedResult = null;
		if (result != null) {
			resolvedResult = toDbTypeValue(procedure.getReturnType(), result);
		}

		byteBuf.reset();

		sendValue(resolvedResult, outBuf, procedure.getReturnType());
		returnOutArgs(procedure, outBuf);
		outBuf.flush();

		output.writeInt(REQ_CODE_RESULT);
		output.writeInt(byteBuf.size() + 4);
		byteBuf.writeTo(output);
		output.writeInt(REQ_CODE_RESULT);
		output.flush();
	}

	public void sendCall() throws IOException {
		if (compareStatus(ExecuteThreadStatus.INVOKE)) {
			setStatus (ExecuteThreadStatus.CALL);
			output.writeInt(REQ_CODE_INTERNAL_JDBC);
			output.flush();
		}
	}

	private void sendError(String exception, Socket socket) throws IOException {
		byteBuf.reset();

		try {
			sendValue(new Integer(1), outBuf, DB_INT);
			sendValue(exception, outBuf, DB_STRING);
		} catch (ExecuteException e) {
			// ignore, never happened
		}

		outBuf.flush();
		output.writeInt(REQ_CODE_ERROR);
		output.writeInt(byteBuf.size() + 4);
		byteBuf.writeTo(output);
		output.writeInt(REQ_CODE_ERROR);
		output.flush();
	}

	private Value[] readArguments(DataInputStream dis, int paramCount)
			throws IOException, TypeMismatchException, SQLException {
		Value[] args = new Value[paramCount];

		for (int i = 0; i < paramCount; i++) {
			int mode = dis.readInt();
			int dbType = dis.readInt();
			int paramType = dis.readInt();
			int paramSize = dis.readInt();

			Value arg = readArgument(dis, paramSize, paramType, mode, dbType);
			args[i] = (arg);
		}

		return args;
	}

	private Value[] readArgumentsForSet(DataInputStream dis, int paramCount)
			throws IOException, TypeMismatchException, SQLException {
		Value[] args = new Value[paramCount];

		for (int i = 0; i < paramCount; i++) {
			int paramType = dis.readInt();
			int paramSize = dis.readInt();
			Value arg = readArgument(dis, paramSize, paramType, Value.IN, 0);
			args[i] = (arg);
		}

		return args;
	}

	private Value readArgument(DataInputStream dis, int paramSize,
			int paramType, int mode, int dbType) throws IOException,
	TypeMismatchException, SQLException {
		Value arg = null;
		switch (paramType) {
		case DB_SHORT:
			// assert paramSize == 4
			arg = new ShortValue((short) dis.readInt(), mode, dbType);
			break;
		case DB_INT:
			// assert paramSize == 4
			arg = new IntValue(dis.readInt(), mode, dbType);
			break;
		case DB_BIGINT:
			// assert paramSize == 8
			arg = new LongValue(dis.readLong(), mode, dbType);
			break;
		case DB_FLOAT:
			// assert paramSize == 4
			arg = new FloatValue(dis.readFloat(), mode, dbType);
			break;
		case DB_DOUBLE:
		case DB_MONETARY:
			// assert paramSize == 8
			arg = new DoubleValue(dis.readDouble(), mode, dbType);
			break;
		case DB_NUMERIC: {
			byte[] paramValue = new byte[paramSize];
			dis.readFully(paramValue);

			int i;
			for (i = 0; i < paramValue.length; i++) {
				if (paramValue[i] == 0)
					break;
			}

			byte[] strValue = new byte[i];
			System.arraycopy(paramValue, 0, strValue, 0, i);

			arg = new NumericValue(new String(strValue), mode, dbType);
		}
		break;
		case DB_CHAR:
		case DB_STRING:
			// assert paramSize == n
		{
			byte[] paramValue = new byte[paramSize];
			dis.readFully(paramValue);

			int i;
			for (i = 0; i < paramValue.length; i++) {
				if (paramValue[i] == 0)
					break;
			}

			byte[] strValue = new byte[i];
			System.arraycopy(paramValue, 0, strValue, 0, i);
			arg = new StringValue(new String(strValue), mode, dbType);
		}
		break;
		case DB_DATE:
			// assert paramSize == 3
		{
			int year = dis.readInt();
			int month = dis.readInt();
			int day = dis.readInt();

			arg = new DateValue(year, month, day, mode, dbType);
		}
		break;
		case DB_TIME:
			// assert paramSize == 3
		{
			int hour = dis.readInt();
			int min = dis.readInt();
			int sec = dis.readInt();
			Calendar cal = Calendar.getInstance();
			cal.set(0, 0, 0, hour, min, sec);

			arg = new TimeValue(hour, min, sec, mode, dbType);
		}
		break;
		case DB_TIMESTAMP:
			// assert paramSize == 6
		{
			int year = dis.readInt();
			int month = dis.readInt();
			int day = dis.readInt();
			int hour = dis.readInt();
			int min = dis.readInt();
			int sec = dis.readInt();
			Calendar cal = Calendar.getInstance();
			cal.set(year, month, day, hour, min, sec);

			arg = new TimestampValue(year, month, day, hour, min, sec, mode,
					dbType);
		}
		break;
		case DB_DATETIME:
			// assert paramSize == 7
		{
			int year = dis.readInt();
			int month = dis.readInt();
			int day = dis.readInt();
			int hour = dis.readInt();
			int min = dis.readInt();
			int sec = dis.readInt();
			int msec = dis.readInt();
			Calendar cal = Calendar.getInstance();
			cal.set(year, month, day, hour, min, sec);

			arg = new DatetimeValue(year, month, day, hour, min, sec, msec,
					mode, dbType);
		}
		break;
		case DB_SET:
		case DB_MULTISET:
		case DB_SEQUENCE: {
			int nCol = dis.readInt();
			// System.out.println(nCol);
			arg = new SetValue(readArgumentsForSet(dis, nCol), mode, dbType);
		}
		break;
		case DB_OBJECT: {
			int page = dis.readInt();
			short slot = (short) dis.readInt();
			short vol = (short) dis.readInt();

			byte[] bOID = new byte[UConnection.OID_BYTE_SIZE];
			bOID[0] = ((byte) ((page >>> 24) & 0xFF));
			bOID[1] = ((byte) ((page >>> 16) & 0xFF));
			bOID[2] = ((byte) ((page >>> 8) & 0xFF));
			bOID[3] = ((byte) ((page >>> 0) & 0xFF));
			bOID[4] = ((byte) ((slot >>> 8) & 0xFF));
			bOID[5] = ((byte) ((slot >>> 0) & 0xFF));
			bOID[6] = ((byte) ((vol >>> 8) & 0xFF));
			bOID[7] = ((byte) ((vol >>> 0) & 0xFF));

			arg = new OidValue(bOID, mode, dbType);
		}
		break;
		case DB_NULL:
			arg = new NullValue(mode, dbType);
			break;
		default:
			// unknown type
			break;
		}
		return arg;
	}

	private void sendValue(Object result, DataOutputStream dos, int ret_type)
			throws IOException, ExecuteException {
		if (result == null) {
			dos.writeInt(DB_NULL);
		} else if (result instanceof Short) {
			dos.writeInt(DB_SHORT);
			dos.writeInt(((Short) result).intValue());
		} else if (result instanceof Integer) {
			dos.writeInt(DB_INT);
			dos.writeInt(((Integer) result).intValue());
		} else if (result instanceof Long) {
			dos.writeInt(DB_BIGINT);
			dos.writeLong(((Long) result).longValue());
		} else if (result instanceof Float) {
			dos.writeInt(DB_FLOAT);
			dos.writeFloat(((Float) result).floatValue());
		} else if (result instanceof Double) {
			dos.writeInt(ret_type);
			dos.writeDouble(((Double) result).doubleValue());
		} else if (result instanceof BigDecimal) {
			dos.writeInt(DB_NUMERIC);
			packAndSendString(((BigDecimal) result).toString(), dos);
		} else if (result instanceof String) {
			dos.writeInt(DB_STRING);
			packAndSendString((String) result, dos);
		} else if (result instanceof java.sql.Date) {
			dos.writeInt(DB_DATE);
			packAndSendString(result.toString(), dos);
		} else if (result instanceof java.sql.Time) {
			dos.writeInt(DB_TIME);
			packAndSendString(result.toString(), dos);
		} else if (result instanceof java.sql.Timestamp) {
			dos.writeInt(ret_type);

			if (ret_type == DB_DATETIME) {
				packAndSendString(result.toString(), dos);
			} else {
				SimpleDateFormat formatter = new SimpleDateFormat(
						"yyyy-MM-dd HH:mm:ss");
				packAndSendString(formatter.format(result), dos);
			}
		} else if (result instanceof CUBRIDOID) {
			dos.writeInt(DB_OBJECT);
			byte[] oid = ((CUBRIDOID) result).getOID();
			dos.writeInt(UJCIUtil.bytes2int(oid, 0));
			dos.writeInt(UJCIUtil.bytes2short(oid, 4));
			dos.writeInt(UJCIUtil.bytes2short(oid, 6));
		} else if (result instanceof ResultSet) {
			dos.writeInt(DB_RESULTSET);
			dos.writeInt(((CUBRIDResultSet) result).getServerHandle());
		} else if (result instanceof int[]) {
			int length = ((int[]) result).length;
			Integer[] array = new Integer[length];
			for (int i = 0; i < array.length; i++) {
				array[i] = new Integer(((int[]) result)[i]);
			}
			sendValue(array, dos, ret_type);
		} else if (result instanceof short[]) {
			int length = ((short[]) result).length;
			Short[] array = new Short[length];
			for (int i = 0; i < array.length; i++) {
				array[i] = new Short(((short[]) result)[i]);
			}
			sendValue(array, dos, ret_type);
		} else if (result instanceof float[]) {
			int length = ((float[]) result).length;
			Float[] array = new Float[length];
			for (int i = 0; i < array.length; i++) {
				array[i] = new Float(((float[]) result)[i]);
			}
			sendValue(array, dos, ret_type);
		} else if (result instanceof double[]) {
			int length = ((double[]) result).length;
			Double[] array = new Double[length];
			for (int i = 0; i < array.length; i++) {
				array[i] = new Double(((double[]) result)[i]);
			}
			sendValue(array, dos, ret_type);
		} else if (result instanceof Object[]) {
			dos.writeInt(ret_type);
			Object[] arr = (Object[]) result;

			dos.writeInt(arr.length);
			for (int i = 0; i < arr.length; i++) {
				sendValue(arr[i], dos, ret_type);
			}
		} else
			;
	}
	
	private int getLengthtoSend(String str)	throws IOException {
		byte b[] = str.getBytes();

		int len = b.length + 1;
		
		int bits = len & 3;
		int pad = 0;

		if (bits != 0)
			pad = 4 - bits;

		return len + pad;
	}
	
	private void packAndSendRawString(String str, DataOutputStream dos) throws IOException {
		byte b[] = str.getBytes();

		int len = b.length + 1;
		int bits = len & 3;
		int pad = 0;

		if (bits != 0)
			pad = 4 - bits;

		dos.writeInt(len + pad);
		dos.write(b);
		for (int i = 0; i <= pad; i++) {
			dos.writeByte(0);
		}
	}

	private void packAndSendString(String str, DataOutputStream dos)
			throws IOException {
		byte b[] = str.getBytes(this.charSet);

		int len = b.length + 1;
		int bits = len & 3;
		int pad = 0;

		if (bits != 0)
			pad = 4 - bits;

		dos.writeInt(len + pad);
		dos.write(b);
		for (int i = 0; i <= pad; i++) {
			dos.writeByte(0);
		}
	}
}
