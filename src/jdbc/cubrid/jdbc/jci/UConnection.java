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

/**
 * Title:        CUBRID Java Client Interface<p>
 * Description:  CUBRID Java Client Interface<p>
 * @version 2.0
 */

package cubrid.jdbc.jci;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.Socket;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.StringTokenizer;
import java.util.Vector;

import javax.transaction.xa.Xid;

import cubrid.jdbc.driver.CUBRIDConnection;
import cubrid.jdbc.driver.CUBRIDDriver;
import cubrid.jdbc.driver.CUBRIDException;
import cubrid.jdbc.driver.CUBRIDJdbcInfoTable;
import cubrid.jdbc.driver.CUBRIDXid;
import cubrid.jdbc.driver.ConnectionProperties;
import cubrid.jdbc.log.BasicLogger;
import cubrid.jdbc.log.Log;
import cubrid.sql.CUBRIDOID;

public class UConnection {
	public final static byte DBMS_CUBRID = 1;

	/* prepare flags */
	public final static byte PREPARE_INCLUDE_OID = 0x01;
	public final static byte PREPARE_UPDATABLE = 0x02;
	public final static byte PREPARE_QUERY_INFO = 0x04;
	public final static byte PREPARE_HOLDABLE = 0x08;
	public final static byte PREPARE_CALL = 0x40;

	public final static byte DROP_BY_OID = 1, IS_INSTANCE = 2,
			GET_READ_LOCK_BY_OID = 3, GET_WRITE_LOCK_BY_OID = 4,
			GET_CLASS_NAME_BY_OID = 5;

	public final static int OID_BYTE_SIZE = 8;

	/*
	 * The followings are also defined in broker/cas_protocol.h
	 */
	private final static String magicString = "CUBRK";
	private final static byte CAS_CLIENT_JDBC = 3;
	/* Current protocol version */
	private final static byte CAS_PROTOCOL_VERSION = 0x01;
	private final static byte CAS_PROTO_INDICATOR = 0x40;
	private final static byte CAS_PROTO_VER_MASK = 0x3F;

	@SuppressWarnings("unused")
	private final static byte GET_COLLECTION_VALUE = 1,
			GET_SIZE_OF_COLLECTION = 2, DROP_ELEMENT_IN_SET = 3,
			ADD_ELEMENT_TO_SET = 4, DROP_ELEMENT_IN_SEQUENCE = 5,
			INSERT_ELEMENT_INTO_SEQUENCE = 6, PUT_ELEMENT_ON_SEQUENCE = 7;
	@SuppressWarnings("unused")
	private final static int DB_PARAM_ISOLATION_LEVEL = 1,
			DB_PARAM_LOCK_TIMEOUT = 2, DB_PARAM_AUTO_COMMIT = 4;

	/* end_tran constants */
	private final static byte END_TRAN_COMMIT = 1;
	private final static byte END_TRAN_ROLLBACK = 2;

	private final static int LOCK_TIMEOUT_NOT_USED = -2;
	private final static int LOCK_TIMEOUT_INFINITE = -1;

	private final static int SOCKET_TIMEOUT = 5000;

	/* casinfo */
	private final static byte CAS_INFO_STATUS_INACTIVE = 0;
	private final static byte CAS_INFO_STATUS_ACTIVE = 1;

	private final static int CAS_INFO_SIZE = 4;

	/* casinfo field def */
	private final static int CAS_INFO_STATUS = 0;
	private final static int CAS_INFO_RESERVED_1 = 1;
	private final static int CAS_INFO_RESERVED_2 = 2;
	private final static int CAS_INFO_RESERVED_3 = 3;

	private final static int BROKER_INFO_SIZE = 8;
	private final static int BROKER_INFO_DBMS_TYPE = 0;
	private final static int BROKER_INFO_KEEP_CONNECTION = 1;
	private final static int BROKER_INFO_STATEMENT_POOLING = 2;
	@SuppressWarnings("unused")
	private final static int BROKER_INFO_CCI_PCONNECT = 3;
	private final static int BROKER_INFO_PROTO_VERSION = 4;
	private final static int BROKER_INFO_RESERVED1 = 5;
	private final static int BROKER_INFO_RESERVED2 = 6;
	/* For backward compatibility */
	private final static int BROKER_INFO_MAJOR_VERSION = BROKER_INFO_PROTO_VERSION;
	private final static int BROKER_INFO_MINOR_VERSION = BROKER_INFO_RESERVED1;
	private final static int BROKER_INFO_PATCH_VERSION = BROKER_INFO_RESERVED2;

	public static final String ZERO_DATETIME_BEHAVIOR_CONVERT_TO_NULL = "convertToNull";
	public static final String ZERO_DATETIME_BEHAVIOR_EXCEPTION = "exception";
	public static final String ZERO_DATETIME_BEHAVIOR_ROUND = "round";

	UOutputBuffer outBuffer;
	CUBRIDConnection cubridcon;

	boolean update_executed; /* for result cache */

	private OutputStream out;
	private UTimedInputStream in;
	private boolean needReconnection;
	private UTimedDataInputStream input;
	private DataOutputStream output;
	private String CASIp;
	private int CASPort;
	private int processId;
	private Socket client;
	private UError errorHandler;
	private Vector<UStatement> transactionList;
	private boolean isClosed = false;
	private byte[] dbInfo;
	private int lastIsolationLevel;
	private int lastLockTimeout = LOCK_TIMEOUT_NOT_USED;
	private boolean lastAutoCommit = true;
	private String dbname, user, passwd, url;
	private ArrayList<String> altHosts = null;
	private int connectedHostId = 0;
	// jci 3.0
	private byte[] broker_info = null;
	private byte[] casinfo = null;
	private int brokerVersion = 0;

	private boolean isServerSideJdbc = false;
	private byte[] checkCasMsg = null;
	boolean skip_checkcas;
	boolean need_checkcas;
	Vector<UStatement> pooled_ustmts;
	Vector<Integer> deferred_close_handle;
	Object curThread;

	private UUrlCache url_cache = null;
	private boolean isAutoCommitBySelf = false;

	private static byte[] driverInfo;

	private ConnectionProperties connectionProperties = new ConnectionProperties();
	private long lastRCTime = 0;
	private int sessionId = 0;

	private Log log;

	static {
		driverInfo = new byte[10];
		UJCIUtil.copy_byte(driverInfo, 0, 5, magicString);
		driverInfo[5] = CAS_CLIENT_JDBC;
		driverInfo[6] = CAS_PROTO_INDICATOR | CAS_PROTOCOL_VERSION;
		driverInfo[7] = driverInfo[8] = 0; /* reserved */
	}

	/*
	 * the normal constructor of the class UConnection
	 */

	UConnection(String ip, int port, String dbname, String user, String passwd,
			String url) throws CUBRIDException {
		CASIp = ip;
		CASPort = port;
		this.dbname = dbname;
		this.user = user;
		this.passwd = passwd;
		this.url = url;
		update_executed = false;

		needReconnection = true;

		String version = getDatabaseProductVersion();
		UError cpErr = errorHandler;
		endTransaction(true);
		if (version == null) {
			throw new CUBRIDException(cpErr);
		}
	}

	UConnection(ArrayList<String> altHostList, String dbname, String user,
			String passwd, String url) throws CUBRIDException {
		try {
			setAltHosts(altHostList);
		} catch (CUBRIDException e) {
			throw e;
		}
		this.dbname = dbname;
		this.user = user;
		this.passwd = passwd;
		this.url = url;
		update_executed = false;

		needReconnection = true;

		String version = getDatabaseProductVersion();
		UError cpErr = errorHandler;
		endTransaction(true);
		if (version == null) {
			throw new CUBRIDException(cpErr);
		}
	}

	/*
	 * the constructor of the class UConnection for class UStatement method
	 * cancel
	 */

	UConnection(String ip, int port) throws UJciException {
		errorHandler = new UError();
		CASIp = ip;
		CASPort = port;
		initConnection(ip, port, true);
		needReconnection = false;
	}

	UConnection(Socket socket, Object curThread) throws CUBRIDException {
		errorHandler = new UError();
		try {
			initConnection(socket);
			needReconnection = false;
			casinfo = new byte[CAS_INFO_SIZE];
			casinfo[CAS_INFO_STATUS] = CAS_INFO_STATUS_ACTIVE;
			isServerSideJdbc = true;
			lastAutoCommit = false;
			this.curThread = curThread;
			UJCIUtil.invoke("com.cubrid.jsp.ExecuteThread", "setCharSet",
					new Class[] { String.class }, this.curThread,
					new Object[] { connectionProperties.getCharSet() });
		} catch (UJciException e) {
			e.toUError(errorHandler);
			throw new CUBRIDException(errorHandler);
		}
	}

	private void initConnection(Socket socket) throws UJciException {
		try {
			client = socket;
			client.setTcpNoDelay(true);
			if (!UJCIUtil.isServerSide()) {
				client.setSoTimeout(SOCKET_TIMEOUT);
			}

			out = client.getOutputStream();
			output = new DataOutputStream(client.getOutputStream());
			output.writeInt(0x08);
			out.flush();
			output.flush();

			in = new UTimedInputStream(client.getInputStream(), CASIp, CASPort);
			input = new UTimedDataInputStream(client.getInputStream(), CASIp,
					CASPort);
		} catch (IOException e) {
			throw createJciException(UErrorCode.ER_CONNECTION);
		}
	}

	public void setAltHosts(ArrayList<String> altHostList)
			throws CUBRIDException {
		if (altHostList.size() < 1) {
			throw new CUBRIDException(UErrorCode.ER_INVALID_ARGUMENT);
		}

		this.altHosts = altHostList;

		String hostPort = altHosts.get(0);
		int pos = hostPort.indexOf(':');
		if (pos < 0) {
			CASIp = hostPort.substring(0);
		} else {
			CASIp = hostPort.substring(0, pos);
		}
		if (pos > 0) {
			CASPort = Integer.valueOf(hostPort.substring(pos + 1)).intValue();
		} else {
			CASPort = CUBRIDDriver.default_port;
		}
	}

	public int getQueryTimeout() {
		return connectionProperties.getQueryTimeout();
	}

	public void setCharset(String newCharsetName) {
		if (UJCIUtil.isServerSide() && isServerSideJdbc) {
			UJCIUtil.invoke("com.cubrid.jsp.ExecuteThread", "setCharSet",
					new Class[] { String.class }, this.curThread,
					new Object[] { newCharsetName });
		}
	}

	public String getCharset() {
		return connectionProperties.getCharSet();
	}

	public void setZeroDateTimeBehavior(String behavior) throws CUBRIDException {
		if (UJCIUtil.isServerSide() && isServerSideJdbc) {
			UJCIUtil.invoke("com.cubrid.jsp.ExecuteThread",
					"setZeroDateTimeBehavior", new Class[] { String.class },
					this.curThread, new Object[] { behavior });
		}
	}

	public String getZeroDateTimeBehavior() {
		return connectionProperties.getZeroDateTimeBehavior();
	}

	public boolean getLogSlowQuery() {
	    	return connectionProperties.getLogSlowQueris();
	}

	synchronized public void addElementToSet(CUBRIDOID oid,
			String attributeName, Object value) {
		errorHandler = new UError();
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return;
		}
		try {
			manageElementOfSet(oid, attributeName, value,
					UConnection.ADD_ELEMENT_TO_SET);
		} catch (UJciException e) {
			e.toUError(errorHandler);
			return;
		} catch (IOException e) {
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
			return;
		}
	}

	synchronized public UBatchResult batchExecute(String batchSqlStmt[]) {
		errorHandler = new UError();
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return null;
		}
		if (batchSqlStmt == null) {
			errorHandler.setErrorCode(UErrorCode.ER_INVALID_ARGUMENT);
			return null;
		}
		try {
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return null;

			outBuffer.newRequest(out, UFunctionCode.EXECUTE_BATCH_STATEMENT);
			outBuffer.addByte(getAutoCommit() ? (byte) 1 : (byte) 0);

			for (int i = 0; i < batchSqlStmt.length; i++) {
				if (batchSqlStmt[i] != null)
					outBuffer.addStringWithNull(batchSqlStmt[i]);
				else
					outBuffer.addNull();
			}

			UInputBuffer inBuffer;
			inBuffer = send_recv_msg();

			int result;
			UBatchResult batchResult = new UBatchResult(inBuffer.readInt());
			for (int i = 0; i < batchResult.getResultNumber(); i++) {
				batchResult.setStatementType(i, inBuffer.readByte());
				result = inBuffer.readInt();
				if (result < 0)
					batchResult.setResultError(i, result, inBuffer.readString(
							inBuffer.readInt(), UJCIManager.sysCharsetName));
				else {
					batchResult.setResult(i, result);
					// jci 3.0
					inBuffer.readInt();
					inBuffer.readShort();
					inBuffer.readShort();
				}
			}

			transactionList.add(null);
			update_executed = true;
			return batchResult;
		} catch (UJciException e) {
			e.toUError(errorHandler);
		} catch (IOException e) {
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		}
		return null;
	}

	synchronized public void close() {
		errorHandler = new UError();
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return;
		}
		// jci 3.0
		clearTransactionList();
		transactionList = null;
		if (client != null) {
			disconnect();
		}
		/*
		 * jci 2.x if (transactionList != null && transactionList.size() > 0)
		 * endTransaction(false);
		 */

		if (!isServerSideJdbc) {
			try {
				if (client != null)
					clientSocketClose();
			} catch (IOException e) {
				errorHandler.setErrorMessage(UErrorCode.ER_COMMUNICATION,
						e.getMessage() + "in close");
			}
		}
		// System.gc();
		// UJCIManager.deleteInList(this);
		isClosed = true;
	}

	synchronized public void dropElementInSequence(CUBRIDOID oid,
			String attributeName, int index) {
		errorHandler = new UError();
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return;
		}
		try {
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return;

			outBuffer.newRequest(out, UFunctionCode.RELATED_TO_COLLECTION);
			outBuffer.addByte(UConnection.DROP_ELEMENT_IN_SEQUENCE);
			outBuffer.addOID(oid);
			outBuffer.addInt(index);
			if (attributeName == null)
				outBuffer.addNull();
			else
				outBuffer.addStringWithNull(attributeName);

			send_recv_msg();
		} catch (UJciException e) {
			e.toUError(errorHandler);
		} catch (IOException e) {
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		}
	}

	synchronized public void dropElementInSet(CUBRIDOID oid,
			String attributeName, Object value) {
		errorHandler = new UError();
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return;
		}
		try {
			manageElementOfSet(oid, attributeName, value,
					UConnection.DROP_ELEMENT_IN_SET);
		} catch (UJciException e) {
			e.toUError(errorHandler);
			return;
		} catch (IOException e) {
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
			return;
		}
	}

	synchronized public void endTransaction(boolean type) {
		errorHandler = new UError();

		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return;
		}

		if (needReconnection == true)
			return;

		try {
			clearTransactionList();
			if (client != null
					&& getCASInfoStatus() != CAS_INFO_STATUS_INACTIVE) {
				checkReconnect();
				if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
					return;

				if (getCASInfoStatus() == CAS_INFO_STATUS_ACTIVE) {
					if (UJCIUtil.isConsoleDebug()) {
						if (!lastAutoCommit || isAutoCommitBySelf
								|| type == false) {
							// this is ok;
						} else {
							// we need check
							throw new Exception("Check It Out!");
						}
					}
					outBuffer.newRequest(out, UFunctionCode.END_TRANSACTION);
					outBuffer.addByte((type == true) ? END_TRAN_COMMIT
							: END_TRAN_ROLLBACK);

					send_recv_msg();
					if (lastAutoCommit) {
						turnOffAutoCommitBySelf();
					}
				}
			}
		} catch (UJciException e) {
			e.toUError(errorHandler);
		} catch (IOException e) {
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		} catch (Exception e) {
			errorHandler.setErrorMessage(UErrorCode.ER_UNKNOWN, e.getMessage());
		}

		/*
		 * if (transactionList == null || transactionList.size() == 0)
		 * errorHandler.clear();
		 */

		boolean keepConnection = brokerInfoKeepConnection();
		long currentTime = System.currentTimeMillis() / 1000;
		int reconnectTime = connectionProperties.getReconnectTime();
		if (connectedHostId > 0 && lastRCTime != 0 && reconnectTime > 0
				&& currentTime - lastRCTime > reconnectTime) {
			keepConnection = false;
			lastRCTime = currentTime;
		}

		if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR
				|| keepConnection == false) // jci 3.0
		{
			if (type == false)
				errorHandler.clear();

			try {
				clientSocketClose();
			} catch (IOException e) {
			}
			needReconnection = true;
		}

		casinfo[CAS_INFO_STATUS] = CAS_INFO_STATUS_INACTIVE;
		update_executed = false;
	}

	synchronized public OutputStream getOutputStream() {
		return out;
	}

	synchronized public UStatement getByOID(CUBRIDOID oid,
			String[] attributeName) {
		UStatement returnValue = null;

		errorHandler = new UError();
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return null;
		}
		try {
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return null;

			outBuffer.newRequest(out, UFunctionCode.GET_BY_OID);
			outBuffer.addOID(oid);
			for (int i = 0; attributeName != null && i < attributeName.length; i++) {
				if (attributeName[i] != null)
					outBuffer.addStringWithNull(attributeName[i]);
				else
					outBuffer.addNull();
			}

			UInputBuffer inBuffer;
			inBuffer = send_recv_msg();

			returnValue = new UStatement(this, oid, attributeName, inBuffer);
		} catch (UJciException e) {
			e.toUError(errorHandler);
			return null;
		} catch (IOException e) {
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
			return null;
		}
		if (returnValue.getRecentError().getErrorCode() != UErrorCode.ER_NO_ERROR) {
			errorHandler.copyValue(returnValue.getRecentError());
			return null;
		}
		transactionList.add(returnValue);
		return returnValue;
	}

	synchronized public String getDatabaseProductVersion() {
		errorHandler = new UError();
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return null;
		}
		try {
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return null;

			outBuffer.newRequest(out, UFunctionCode.GET_DB_VERSION);
			outBuffer.addByte(getAutoCommit() ? (byte) 1 : (byte) 0);

			UInputBuffer inBuffer;
			inBuffer = send_recv_msg();

			return inBuffer.readString(inBuffer.remainedCapacity(),
					UJCIManager.sysCharsetName);
		} catch (UJciException e) {
			e.toUError(errorHandler);
		} catch (IOException e) {
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		}
		return null;
	}

	synchronized public int getIsolationLevel() {
		errorHandler = new UError();

		if (UJCIUtil.isMMDB()) {
			return CUBRIDIsolationLevel.TRAN_COMMIT_CLASS_UNCOMMIT_INSTANCE;
		} else {
			if (isClosed == true) {
				errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
				return CUBRIDIsolationLevel.TRAN_UNKNOWN_ISOLATION;
			}
			try {
				checkReconnect();
				if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
					return CUBRIDIsolationLevel.TRAN_UNKNOWN_ISOLATION;

				outBuffer.newRequest(out, UFunctionCode.GET_DB_PARAMETER);
				outBuffer.addInt(DB_PARAM_ISOLATION_LEVEL);

				UInputBuffer inBuffer;
				inBuffer = send_recv_msg();

				return inBuffer.readInt();
			} catch (UJciException e) {
				e.toUError(errorHandler);
			} catch (IOException e) {
				errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
			}
			return CUBRIDIsolationLevel.TRAN_UNKNOWN_ISOLATION;
		}
	}

	public UError getRecentError() {
		return errorHandler;
	}

	synchronized public String getQueryplanOnly(String sql) {
		String ret_val;

		if (sql == null)
			return null;

		errorHandler = new UError();
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return null;
		}

		try {
			checkReconnect();
			outBuffer.newRequest(UFunctionCode.GET_QUERY_INFO);
			outBuffer.addInt(0);
			outBuffer.addByte(UStatement.QUERY_INFO_PLAN);
			outBuffer.addStringWithNull(sql);

			UInputBuffer inBuffer;
			inBuffer = send_recv_msg();

			ret_val = inBuffer.readString(inBuffer.remainedCapacity(),
					connectionProperties.getCharSet());
		} catch (UJciException e) {
			e.toUError(errorHandler);
			return null;
		} catch (IOException e) {
			if (errorHandler.getErrorCode() != UErrorCode.ER_CONNECTION)
				errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
			return null;
		}

		if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR) {
			return null;
		}

		return ret_val;
	}

	synchronized public UStatement getSchemaInfo(int type, String arg1,
			String arg2, byte flag) {
		UStatement returnValue = null;

		errorHandler = new UError();
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return null;
		}
		if (type < USchType.SCH_MIN || type > USchType.SCH_MAX) {
			errorHandler.setErrorCode(UErrorCode.ER_SCHEMA_TYPE);
			return null;
		}
		if (flag < 0 || flag > 3) {
			errorHandler.setErrorCode(UErrorCode.ER_ILLEGAL_FLAG);
			return null;
		}
		try {
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return null;

			outBuffer.newRequest(out, UFunctionCode.GET_SCHEMA_INFO);
			outBuffer.addInt(type);
			if (arg1 == null)
				outBuffer.addNull();
			else
				outBuffer.addStringWithNull(arg1);
			if (arg2 == null)
				outBuffer.addNull();
			else
				outBuffer.addStringWithNull(arg2);
			outBuffer.addByte(flag);

			UInputBuffer inBuffer;
			inBuffer = send_recv_msg();

			returnValue = new UStatement(this, arg1, arg2, type, inBuffer);
		} catch (UJciException e) {
			e.toUError(errorHandler);
			return null;
		} catch (IOException e) {
			if (errorHandler.getErrorCode() != UErrorCode.ER_CONNECTION)
				errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
			return null;
		}
		if (returnValue.getRecentError().getErrorCode() != UErrorCode.ER_NO_ERROR) {
			errorHandler.copyValue(returnValue.getRecentError());
			return null;
		}
		// transactionList.add(returnValue);
		return returnValue;
	}

	synchronized public int getSizeOfCollection(CUBRIDOID oid,
			String attributeName) {
		errorHandler = new UError();
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return 0;
		}
		try {
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return 0;

			outBuffer.newRequest(out, UFunctionCode.RELATED_TO_COLLECTION);
			outBuffer.addByte(UConnection.GET_SIZE_OF_COLLECTION);
			outBuffer.addOID(oid);
			if (attributeName == null)
				outBuffer.addNull();
			else
				outBuffer.addStringWithNull(attributeName);

			UInputBuffer inBuffer;
			inBuffer = send_recv_msg();

			return inBuffer.readInt();
		} catch (UJciException e) {
			e.toUError(errorHandler);
		} catch (IOException e) {
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		}
		return 0;
	}

	synchronized public void insertElementIntoSequence(CUBRIDOID oid,
			String attributeName, int index, Object value) {
		errorHandler = new UError();
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return;
		}
		try {
			manageElementOfSequence(oid, attributeName, index, value,
					UConnection.INSERT_ELEMENT_INTO_SEQUENCE);
		} catch (UJciException e) {
			e.toUError(errorHandler);
			return;
		} catch (IOException e) {
			if (errorHandler.getErrorCode() != UErrorCode.ER_CONNECTION)
				errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
			return;
		}
	}

	public boolean isClosed() {
		return isClosed;
	}

	synchronized public UStatement prepare(String sqlStatement, byte prepareFlag) {
		return (prepare(sqlStatement, prepareFlag, false));
	}

	synchronized public UStatement prepare(String sqlStatement,
			byte prepareFlag, boolean recompile_flag) {
		UStatement returnValue = null;

		errorHandler = new UError();
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return null;
		}
		try {
			skip_checkcas = true;
			need_checkcas = false;

			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return null;

			outBuffer.newRequest(out, UFunctionCode.PREPARE);
			outBuffer.addStringWithNull(sqlStatement);
			outBuffer.addByte(prepareFlag);
			outBuffer.addByte(getAutoCommit() ? (byte) 1 : (byte) 0);

			while (deferred_close_handle.isEmpty() != true) {
				Integer close_handle = (Integer) deferred_close_handle
						.remove(0);
				outBuffer.addInt(close_handle.intValue());
			}

			UInputBuffer inBuffer;
			inBuffer = send_recv_msg();

			if (recompile_flag)
				returnValue = new UStatement(this, inBuffer, true,
						sqlStatement, prepareFlag);
			else
				returnValue = new UStatement(this, inBuffer, false,
						sqlStatement, prepareFlag);
		} catch (UJciException e) {
			e.toUError(errorHandler);
		} catch (IOException e) {
			if (errorHandler.getErrorCode() != UErrorCode.ER_CONNECTION)
				errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		} finally {
			skip_checkcas = false;
		}

		if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR) {
			if (errorHandler.getJdbcErrorCode() == -111)
				need_checkcas = true;
			if (need_checkcas) {
				if (check_cas() == false) {
					try {
						clientSocketClose();
					} catch (Exception e) {
					}
				}
				return (prepare(sqlStatement, prepareFlag, recompile_flag));
			} else {
				return null;
			}
		}

		if (returnValue.getRecentError().getErrorCode() != UErrorCode.ER_NO_ERROR) {
			errorHandler.copyValue(returnValue.getRecentError());
			return null;
		}
		transactionList.add(returnValue);
		pooled_ustmts.add(returnValue);
		return returnValue;
	}

	synchronized public void putByOID(CUBRIDOID oid, String attributeName[],
			Object values[]) {
		errorHandler = new UError();
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return;
		}
		if (attributeName == null && values == null) {
			errorHandler.setErrorCode(UErrorCode.ER_INVALID_ARGUMENT);
			return;
		}
		try {
			UPutByOIDParameter putParameter = null;

			if (values != null)
				putParameter = new UPutByOIDParameter(attributeName, values);

			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return;

			outBuffer.newRequest(out, UFunctionCode.PUT_BY_OID);
			outBuffer.addOID(oid);
			if (putParameter != null)
				putParameter.writeParameter(outBuffer);

			send_recv_msg();
			if (getAutoCommit()) {
				turnOnAutoCommitBySelf();
			}
		} catch (UJciException e) {
			e.toUError(errorHandler);
		} catch (IOException e) {
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		}
	}

	synchronized public void putElementInSequence(CUBRIDOID oid,
			String attributeName, int index, Object value) {
		errorHandler = new UError();
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return;
		}
		try {
			manageElementOfSequence(oid, attributeName, index, value,
					UConnection.PUT_ELEMENT_ON_SEQUENCE);
		} catch (UJciException e) {
			e.toUError(errorHandler);
			return;
		} catch (IOException e) {
			if (errorHandler.getErrorCode() != UErrorCode.ER_CONNECTION)
				errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
			return;
		}
	}

	synchronized public void setIsolationLevel(int level) {
		errorHandler = new UError();

		if (!UJCIUtil.isMMDB()) {
			if (isClosed == true) {
				errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
				return;
			}
			if (level < CUBRIDIsolationLevel.TRAN_MIN
					|| level > CUBRIDIsolationLevel.TRAN_MAX) {
				errorHandler.setErrorCode(UErrorCode.ER_ISO_TYPE);
				return;
			}
			try {
				checkReconnect();
				if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
					return;

				outBuffer.newRequest(out, UFunctionCode.SET_DB_PARAMETER);
				outBuffer.addInt(DB_PARAM_ISOLATION_LEVEL);
				outBuffer.addInt(level);

				send_recv_msg();

				lastIsolationLevel = level;
			} catch (UJciException e) {
				e.toUError(errorHandler);
			} catch (IOException e) {
				errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
			}
		}
	}

	synchronized public void setLockTimeout(int timeout) {
		errorHandler = new UError();

		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return;
		}

		try {
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return;

			outBuffer.newRequest(out, UFunctionCode.SET_DB_PARAMETER);
			outBuffer.addInt(DB_PARAM_LOCK_TIMEOUT);
			outBuffer.addInt(timeout);

			send_recv_msg();

			if (timeout < 0)
				lastLockTimeout = LOCK_TIMEOUT_INFINITE;
			else
				lastLockTimeout = timeout;
		} catch (UJciException e) {
			e.toUError(errorHandler);
		} catch (IOException e) {
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		}
	}

	/*
	 * 3.0 synchronized public void savepoint(int mode, String name) {
	 * errorHandler = new UError(); if (isClosed == true) {
	 * errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED); return; }
	 * 
	 * try { checkReconnect(); if (errorHandler.getErrorCode() !=
	 * UErrorCode.ER_NO_ERROR) return;
	 * 
	 * outBuffer.newRequest(out, UFunctionCode.SAVEPOINT);
	 * outBuffer.addByte(mode); outBuffer.addStringWithNull(name);
	 * 
	 * UInputBuffer inBuffer; inBuffer = send_recv_msg(); } catch (UJciException
	 * e) { e.toUError(errorHandler); } catch (IOException e) {
	 * errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION); } }
	 */

	public byte getCASInfoStatus() {
		if (casinfo == null) {
			return (byte) CAS_INFO_STATUS_INACTIVE;
		}
		return casinfo[CAS_INFO_STATUS];
	}

	public byte[] getCASInfo() {
		return casinfo;
	}

	public void setCASInfo(byte[] casinfo) {
		this.casinfo = casinfo;
	}

	public byte getDbmsType() {
		// jci 3.0
		if (broker_info == null)
			return DBMS_CUBRID;
		return broker_info[BROKER_INFO_DBMS_TYPE];

		/*
		 * jci 2.x return DBMS_CUBRID;
		 */
	}

	public boolean isConnectedToCubrid() {
		return getDbmsType() == DBMS_CUBRID ? true : false;
	}

	public boolean brokerInfoStatementPooling() {
		if (broker_info == null)
			return false;

		if (broker_info[BROKER_INFO_STATEMENT_POOLING] == (byte) 1)
			return true;
		else
			return false;
	}

	synchronized public void xa_endTransaction(Xid xid, boolean type) {
		errorHandler = new UError();

		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return;
		}

		try {
			clearTransactionList();
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return;

			outBuffer.newRequest(out, UFunctionCode.XA_END_TRAN);
			outBuffer.addXid(xid);
			outBuffer.addByte((type == true) ? END_TRAN_COMMIT
					: END_TRAN_ROLLBACK);

			send_recv_msg();
		} catch (Exception e) {
			errorHandler.setErrorCode(UErrorCode.ER_UNKNOWN);
		} finally {
			try {
				clientSocketClose();
				needReconnection = true;
			} catch (IOException e) {
			}
		}
	}

	synchronized public void xa_prepare(Xid xid) {
		errorHandler = new UError();

		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return;
		}

		try {
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return;

			outBuffer.newRequest(out, UFunctionCode.XA_PREPARE);
			outBuffer.addXid(xid);

			send_recv_msg();
		} catch (Exception e) {
			errorHandler.setErrorCode(UErrorCode.ER_UNKNOWN);
		}
	}

	synchronized public Xid[] xa_recover() {
		errorHandler = new UError();

		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return null;
		}

		try {
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return null;

			outBuffer.newRequest(out, UFunctionCode.XA_RECOVER);

			UInputBuffer inBuffer;
			inBuffer = send_recv_msg();

			int num_xid = inBuffer.getResCode();

			CUBRIDXid[] xid;
			xid = new CUBRIDXid[num_xid];
			for (int i = 0; i < num_xid; i++) {
				xid[i] = inBuffer.readXid();
			}
			return xid;
		} catch (Exception e) {
			errorHandler.setErrorCode(UErrorCode.ER_UNKNOWN);
			return null;
		}
	}

	public void setCUBRIDConnection(CUBRIDConnection con) {
		cubridcon = con;
		lastIsolationLevel = CUBRIDIsolationLevel.TRAN_UNKNOWN_ISOLATION;
		lastLockTimeout = LOCK_TIMEOUT_NOT_USED;
	}

	public CUBRIDConnection getCUBRIDConnection() {
		return cubridcon;
	}

	private static void printCasInfo(byte[] prev, byte[] curr) {
		if (prev != null) {
			String fmt = "[PREV : %d, RECV : %d], [preffunc : %d, recvfunc : %d], [REQ: %d], [JID: %d]";
			String msg = String.format(fmt, prev[0], curr[0], prev[1], curr[1],
					prev[2], curr[3]);
			CUBRIDDriver.printDebug(msg);
		}
	}

	synchronized public boolean check_cas() {
		if (isClosed == true)
			return true;
		if (client == null || needReconnection == true)
			return true;

		if (skip_checkcas) {
			need_checkcas = true;
			return true;
		}

		try {
			synchronized (in) {
				if (checkCasMsg == null) {
					int msgSize = 1;
					checkCasMsg = new byte[9];
					checkCasMsg[0] = (byte) ((msgSize >>> 24) & 0xFF);
					checkCasMsg[1] = (byte) ((msgSize >>> 16) & 0xFF);
					checkCasMsg[2] = (byte) ((msgSize >>> 8) & 0xFF);
					checkCasMsg[3] = (byte) ((msgSize >>> 0) & 0xFF);
					checkCasMsg[4] = (byte) casinfo[CAS_INFO_STATUS];
					checkCasMsg[5] = (byte) casinfo[CAS_INFO_RESERVED_1];
					checkCasMsg[6] = (byte) casinfo[CAS_INFO_RESERVED_2];
					checkCasMsg[7] = (byte) casinfo[CAS_INFO_RESERVED_3];
					checkCasMsg[8] = UFunctionCode.CHECK_CAS;
				} else {
					checkCasMsg[4] = (byte) casinfo[CAS_INFO_STATUS];
					checkCasMsg[5] = (byte) casinfo[CAS_INFO_RESERVED_1];
					checkCasMsg[6] = (byte) casinfo[CAS_INFO_RESERVED_2];
					checkCasMsg[7] = (byte) casinfo[CAS_INFO_RESERVED_3];
				}

				byte[] prev_casinfo = casinfo;
				output.write(checkCasMsg);
				int res = input.readInt();
				byte[] l_casinfo = new byte[CAS_INFO_SIZE];
				input.readByte(l_casinfo);
				casinfo = l_casinfo;

				if (UJCIUtil.isConsoleDebug()) {
					printCasInfo(prev_casinfo, casinfo);
				}

				if (res == 0)
					return true;
				if (res < 4)
					return false;
				res = input.readInt();
				if (res < 0)
					return false;
			}
		} catch (IOException e) {
			return false;
		}

		return true;
	}

	synchronized public boolean check_cas(String msg) {
		try {
			outBuffer.newRequest(out, UFunctionCode.CHECK_CAS);
			outBuffer.addStringWithNull(msg);
			send_recv_msg();
		} catch (Exception e) {
			return false;
		}

		return true;
	}

	synchronized public void reset_connection() {
		try {
			if (client != null)
				client.close();
		} catch (Exception e) {
		}

		client = null;
		needReconnection = true;
	}

	synchronized public Object oidCmd(CUBRIDOID oid, byte cmd) {
		errorHandler = new UError();
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return null;
		}
		try {
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return null;

			outBuffer.newRequest(out, UFunctionCode.RELATED_TO_OID);
			outBuffer.addByte(cmd);
			outBuffer.addOID(oid);

			UInputBuffer inBuffer;
			inBuffer = send_recv_msg();

			int res_code;
			res_code = inBuffer.getResCode();

			if (cmd == IS_INSTANCE) {
				if (res_code == 1)
					return oid;
			} else if (cmd == GET_CLASS_NAME_BY_OID) {
				return inBuffer.readString(inBuffer.remainedCapacity(),
					connectionProperties.getCharSet());
			}
		} catch (UJciException e) {
			e.toUError(errorHandler);
		} catch (IOException e) {
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		}

		return null;
	}

	synchronized public byte[] lobNew(int lob_type) {
		errorHandler = new UError();
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return null;
		}
		try {
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return null;

			outBuffer.newRequest(out, UFunctionCode.NEW_LOB);
			outBuffer.addInt(lob_type);

			UInputBuffer inBuffer;
			inBuffer = send_recv_msg();

			int res_code;
			res_code = inBuffer.getResCode();
			if (res_code < 0) {
				errorHandler.setErrorCode(UErrorCode.ER_UNKNOWN);
				return null;
			}

			byte[] packedLobHandle = new byte[res_code];
			inBuffer.readBytes(packedLobHandle);
			return packedLobHandle;
		} catch (UJciException e) {
			e.toUError(errorHandler);
		} catch (IOException e) {
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		} catch (Exception e) {
			errorHandler.setErrorCode(UErrorCode.ER_UNKNOWN);
		}
		return null;

	}

	synchronized public int lobWrite(byte[] packedLobHandle, long offset,
			byte[] buf, int start, int len) {
		errorHandler = new UError();
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return -1;
		}
		try {
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return -1;

			outBuffer.newRequest(out, UFunctionCode.WRITE_LOB);
			outBuffer.addBytes(packedLobHandle);
			outBuffer.addLong(offset);
			outBuffer.addBytes(buf, start, len);

			UInputBuffer inBuffer;
			inBuffer = send_recv_msg();

			int res_code;
			res_code = inBuffer.getResCode();
			if (res_code < 0) {
				errorHandler.setErrorCode(UErrorCode.ER_UNKNOWN);
			}
			return res_code;
		} catch (UJciException e) {
			e.toUError(errorHandler);
		} catch (IOException e) {
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		} catch (Exception e) {
			errorHandler.setErrorCode(UErrorCode.ER_UNKNOWN);
		}
		return -1;
	}

	synchronized public int lobRead(byte[] packedLobHandle, long offset,
			byte[] buf, int start, int len) {
		errorHandler = new UError();
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return -1;
		}
		try {
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return -1;

			outBuffer.newRequest(out, UFunctionCode.READ_LOB);
			outBuffer.addBytes(packedLobHandle);
			outBuffer.addLong(offset);
			outBuffer.addInt(len);

			UInputBuffer inBuffer;
			inBuffer = send_recv_msg();

			int res_code;
			res_code = inBuffer.getResCode();
			if (res_code < 0) {
				errorHandler.setErrorCode(UErrorCode.ER_UNKNOWN);
			} else {
				inBuffer.readBytes(buf, start, res_code);
			}

			return res_code;
		} catch (UJciException e) {
			e.toUError(errorHandler);
		} catch (IOException e) {
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		} catch (Exception e) {
			errorHandler.setErrorCode(UErrorCode.ER_UNKNOWN);
		}
		return -1;
	}

	synchronized public void setAutoCommit(boolean autoCommit) {
		if (!isServerSideJdbc) {
			if (lastAutoCommit != autoCommit) {
				lastAutoCommit = autoCommit;
			}
		}

		/*
		 * errorHandler = new UError(); if (isClosed == true){
		 * errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED); return; } try{
		 * checkReconnect(); if (errorHandler.getErrorCode() !=
		 * UErrorCode.ER_NO_ERROR) return; outBuffer.newRequest(out,
		 * UFunctionCode.SET_DB_PARAMETER);
		 * outBuffer.addInt(DB_PARAM_AUTO_COMMIT); outBuffer.addInt(autoCommit ?
		 * 1 : 0 ); UInputBuffer inBuffer; inBuffer = send_recv_msg();
		 * lastAutoCommit = autoCommit; }catch(UJciException e){
		 * e.toUError(errorHandler); }catch(IOException e){
		 * errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION); }
		 */
	}

	public boolean getAutoCommit() {
		return lastAutoCommit;
	}

	public int currentIsolationLevel() {
		return lastIsolationLevel;
	}

	void clientSocketClose() throws IOException {
		try {
			needReconnection = true;
			if (client != null) {
				client.setSoLinger(true, 0);
				client.close();
			}
			client = null;
		} catch (IOException e) {
		}
		clearPooledUStatements();
		deferred_close_handle.clear();
	}

	UInputBuffer send_recv_msg(boolean recv_result) throws UJciException,
			IOException {
		byte prev_casinfo[] = casinfo;
		outBuffer.sendData();
		/* set cas info to UConnection member variable and return InputBuffer */
		UInputBuffer inputBuffer = new UInputBuffer(in, this);

		if (UJCIUtil.isConsoleDebug()) {
			printCasInfo(prev_casinfo, casinfo);
		}
		return inputBuffer;
	}

	UInputBuffer send_recv_msg() throws UJciException, IOException {
		return (send_recv_msg(true));
	}

	void cancel() throws UJciException, IOException {
		UConnection conCancel = null;
		try {
			conCancel = new UConnection(CASIp, CASPort);

			if (protoVersionIsAbove(1) == true) {
				String cancelCommand = "QC";
				int localPort = client.getLocalPort();
				if (localPort < 0)
					localPort = 0;
				conCancel.output.write(cancelCommand.getBytes());
				conCancel.output.writeInt(processId);
				conCancel.output.writeShort((short) localPort);
				conCancel.output.writeShort(0); /* reserved */
			} else {
				String cancelCommand = "CANCEL";
				conCancel.output.write(cancelCommand.getBytes());
				conCancel.output.writeInt(processId);
			}

			conCancel.output.flush();

			int error = conCancel.input.readInt();

			if (error < 0) {
				int errorCode = conCancel.input.readInt();
				throw createJciException(UErrorCode.ER_DBMS, error, errorCode, null);
			}
		} finally {
			if (conCancel != null) {
				conCancel.clientSocketClose();
				conCancel.close();
			}
		}
	}

	UUrlCache getUrlCache() {
		if (url_cache == null) {
			UUrlHostKey key = new UUrlHostKey(CASIp, CASPort, dbname, user);
			url_cache = UJCIManager.getUrlCache(key);
		}
		return url_cache;
	}

	private void reconnectWorker() throws IOException, UJciException {
		if (UJCIUtil.isConsoleDebug()) {
			CUBRIDDriver.printDebug(String.format("Try Connect (%s,%d)", CASIp,
					CASPort));
		}
		initConnection(CASIp, CASPort, true);
		sendDriverInfo();
		whetherConnectOtherPortOrNot();
		connectDB();

		needReconnection = false;
		if (connectionProperties.getReconnectTime() > 0)
			lastRCTime = System.currentTimeMillis() / 1000;

		if (lastIsolationLevel != CUBRIDIsolationLevel.TRAN_UNKNOWN_ISOLATION)
			setIsolationLevel(lastIsolationLevel);
		if (lastLockTimeout != LOCK_TIMEOUT_NOT_USED)
			setLockTimeout(lastLockTimeout);
		/*
		 * if(!lastAutoCommit) setAutoCommit(lastAutoCommit);
		 */
	}

	private boolean setActiveHost(int hostId) throws UJciException {
		if (hostId >= altHosts.size())
			return false;

		String info = altHosts.get(hostId);
		setConnectInfo(info);
		return true;
	}

	private void reconnect() throws IOException, UJciException {
		String info = CUBRIDDriver.getLastConnectInfo(url);
		if (info != null) {
			setConnectInfo(info);
			try {
				reconnectWorker();
				return;
			} catch (Exception e) {
				// continue
			}
		}

		if (altHosts == null) {
			reconnectWorker();
		} else {
			for (int hostId = 0; hostId < altHosts.size(); hostId++) {
				try {
					setActiveHost(hostId);
					reconnectWorker();
					connectedHostId = hostId;
					return; // success to connect
				} catch (IOException e) {
					// continue to connect to next host
				} catch (UJciException e) {
					int errno = e.getJciError();
					if (errno == UErrorCode.ER_COMMUNICATION ||
					// errno == UErrorCode.ER_DBMS ||
							errno == UErrorCode.ER_CONNECTION) {
						// continue to connect to next host
					} else {
						throw e;
					}
				}
			}
			// failed to connect to neither hosts
			throw createJciException(UErrorCode.ER_CONNECTION);
		}
	}

	// jci 3.0
	private boolean brokerInfoKeepConnection() {
		if (broker_info == null)
			return false;

		if (broker_info[BROKER_INFO_KEEP_CONNECTION] == (byte) 1)
			return true;
		else
			return false;
	}

	private int makeBrokerVersion(int major, int minor, int patch) {
		int version = 0;
		if ((major < 0 || major > Byte.MAX_VALUE)
				|| (minor < 0 || minor > Byte.MAX_VALUE)
				|| (patch < 0 || patch > Byte.MAX_VALUE)) {
			return 0;
		}

		version = ((int) major << 24) | ((int) minor << 16)
				| ((int) patch << 8);
		return version;
	}

	private int makeProtoVersion(int ver) {
		return ((int) CAS_PROTO_INDICATOR << 24) | ver;
	}

	public int brokerInfoVersion() {
		return brokerVersion;
	}

	public boolean protoVersionIsAbove(int ver) {
		if (isServerSideJdbc()
				|| (brokerInfoVersion() >= makeProtoVersion(ver))) {
			return true;
		}
		return false;
	}

	private void connectDB() throws IOException, UJciException {
		UInputBuffer inBuffer;

		synchronized (output) {
			output.write(dbInfo);
			output.flush();

			synchronized (input) {
				synchronized (in) {
					inBuffer = new UInputBuffer(in, this);
					processId = inBuffer.getResCode();
					if (broker_info == null)
						broker_info = new byte[BROKER_INFO_SIZE];
					inBuffer.readBytes(broker_info);
					sessionId = inBuffer.readInt();
				}
			}
		}

		/* synchronize with broker_info */
		byte version = broker_info[BROKER_INFO_PROTO_VERSION];
		if ((version & CAS_PROTO_INDICATOR) == CAS_PROTO_INDICATOR) {
			brokerVersion = makeProtoVersion(version & CAS_PROTO_VER_MASK);
		} else {
			brokerVersion = makeBrokerVersion(
					(int) broker_info[BROKER_INFO_MAJOR_VERSION],
					(int) broker_info[BROKER_INFO_MINOR_VERSION],
					(int) broker_info[BROKER_INFO_PATCH_VERSION]);
		}
	}

	private String makeConnectInfo(String ip, int port) {
		return String.format("%s:%d", ip, port);
	}

	private void setConnectInfo(String info) throws UJciException {
		StringTokenizer st = new StringTokenizer(info, ":");
		if (st.countTokens() != 2) {
			throw createJciException(UErrorCode.ER_CONNECTION);
		}

		CASIp = st.nextToken();
		CASPort = Integer.valueOf(st.nextToken()).intValue();
	}

	private void initConnection(String ip, int port, boolean setInfo)
			throws UJciException {
		try {
			client = new Socket(InetAddress.getByName(ip), port);
			client.setTcpNoDelay(true);
			client.setSoTimeout(SOCKET_TIMEOUT);

			out = client.getOutputStream();
			output = new DataOutputStream(client.getOutputStream());
			out.flush();
			output.flush();

			in = new UTimedInputStream(client.getInputStream(), ip, CASPort);
			input = new UTimedDataInputStream(client.getInputStream(), ip,
					CASPort);

			if (setInfo) {
				CUBRIDDriver.setLastConnectInfo(url, makeConnectInfo(ip, port));
			}
		} catch (IOException e) {
		    	throw createJciException(UErrorCode.ER_CONNECTION);
		}
	}

	private void manageElementOfSequence(CUBRIDOID oid, String attributeName,
			int index, Object value, byte flag) throws UJciException,
			IOException {
		UAParameter aParameter;
		aParameter = new UAParameter(attributeName, value);

		checkReconnect();
		if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
			return;

		outBuffer.newRequest(out, UFunctionCode.RELATED_TO_COLLECTION);
		outBuffer.addByte(flag);
		outBuffer.addOID(oid);
		outBuffer.addInt(index);
		aParameter.writeParameter(outBuffer);

		send_recv_msg();
	}

	private void manageElementOfSet(CUBRIDOID oid, String attributeName,
			Object value, byte flag) throws UJciException, IOException {
		UAParameter aParameter;
		aParameter = new UAParameter(attributeName, value);

		checkReconnect();
		if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
			return;

		outBuffer.newRequest(out, UFunctionCode.RELATED_TO_COLLECTION);
		outBuffer.addByte(flag);
		outBuffer.addOID(oid);
		aParameter.writeParameter(outBuffer);

		send_recv_msg();
	}

	void checkReconnect() throws IOException, UJciException {
		if (dbInfo == null) {
			// see broker/cas_protocol.h
			// #define SRV_CON_DBNAME_SIZE 32
			// #define SRV_CON_DBUSER_SIZE 32
			// #define SRV_CON_DBPASSWD_SIZE 32
			// #define SRV_CON_DBSESS_ID_SIZE 20
			// #define SRV_CON_URL_SIZE 512
			// #define SRV_CON_DB_INFO_SIZE \
			// (SRV_CON_DBNAME_SIZE + SRV_CON_DBUSER_SIZE +
			// SRV_CON_DBPASSWD_SIZE + \
			// SRV_CON_URL_SIZE + SRV_CON_DBSESS_ID_SIZE)
			dbInfo = new byte[32 + 32 + 32 + 512 + 20];
			UJCIUtil.copy_byte(dbInfo, 0, 32, dbname);
			UJCIUtil.copy_byte(dbInfo, 32, 32, user);
			UJCIUtil.copy_byte(dbInfo, 64, 32, passwd);
			UJCIUtil.copy_byte(dbInfo, 96, 512, url);
		}
		// set the session id
		UJCIUtil.copy_byte(dbInfo, 608, 20, new Integer(sessionId).toString());

		if (outBuffer == null) {
			outBuffer = new UOutputBuffer(this);
		}

		if (transactionList == null) {
			transactionList = new Vector<UStatement>();
		}
		if (deferred_close_handle == null) {
			deferred_close_handle = new Vector<Integer>();
		}

		if (pooled_ustmts == null) {
			pooled_ustmts = new Vector<UStatement>();
		}

		if (!isServerSideJdbc) {
			if (getCASInfoStatus() == CAS_INFO_STATUS_INACTIVE
					&& check_cas() == false) {
				clientSocketClose();
			}

			if (needReconnection == true) {
				reconnect();
				if (UJCIUtil.isSendAppInfo()) {
					sendAppInfo();
				}
			}
		}
	}

	private void sendAppInfo() {
		String msg;
		msg = CUBRIDJdbcInfoTable.getValue();
		if (msg == null)
			return;
		check_cas(msg);
	}

	private void sendDriverInfo() throws IOException {
		synchronized (output) {
			output.write(driverInfo);
			output.flush();
		}
	}

	private void whetherConnectOtherPortOrNot() throws IOException,
			UJciException {
		int newPort = 0;

		synchronized (input) {
			newPort = input.readInt();
		}
		if (newPort < 0) {
			int err_code = input.readInt();
			throw createJciException(UErrorCode.ER_DBMS, newPort, err_code, null);
		} else if (newPort == 0)
			return;
		client.setSoLinger(true, 0);
		client.close();
		initConnection(CASIp, newPort, false);
	}

	public void closeSession() {
		try {
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return;
			outBuffer.newRequest(out, UFunctionCode.END_SESSION);
			send_recv_msg();
			sessionId = 0;
		} catch (Exception e) {
		}
	}

	// jci 3.0
	private void disconnect() {
		try {
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return;

			outBuffer.newRequest(out, UFunctionCode.CON_CLOSE);

			send_recv_msg();
		} catch (Exception e) {
		}
	}

	private void clearTransactionList() {
		if (transactionList == null)
			return;
		transactionList.clear();
	}

	// end jci 3.0

	private void clearPooledUStatements() {
		if (pooled_ustmts == null)
			return;

		while (pooled_ustmts.isEmpty() != true) {
			UStatement tmp_ustmt = (UStatement) pooled_ustmts.remove(0);
			if (tmp_ustmt != null)
				tmp_ustmt.close(false);
		}
	}

	public boolean isServerSideJdbc() {
		return isServerSideJdbc;
	}

	public void turnOnAutoCommitBySelf() {
		isAutoCommitBySelf = true;
	}

	public void turnOffAutoCommitBySelf() {
		isAutoCommitBySelf = false;
	}

	static void ping(String ip, int port, int socketTimeout) throws IOException {
		Socket clientSocket = null;
		DataOutputStream outputStream = null;
		DataInputStream inputStream = null;

		try {
			String ping_msg = "PING_TEST!";

			clientSocket = new Socket(InetAddress.getByName(ip), port);
			clientSocket.setTcpNoDelay(true);
			clientSocket.setSoTimeout(socketTimeout);

			outputStream = new DataOutputStream(clientSocket.getOutputStream());

			outputStream.write(ping_msg.getBytes());
			outputStream.flush();

			inputStream = new DataInputStream(clientSocket.getInputStream());
			inputStream.readInt();

		} catch (Exception e) {
			throw new IOException(e.getMessage());
		} finally {
			if (outputStream != null) {
				outputStream.close();
			}
			if (inputStream != null) {
				inputStream.close();
			}
			if (clientSocket != null) {
				clientSocket.close();
			}
		}
	}

    public void setConnectionProperties(ConnectionProperties connProperties) {
	this.connectionProperties = connProperties;
    }

    private Log getLogger() {
	if (log == null) {
	    log = new BasicLogger(connectionProperties.getLogFile());
	}
	return log;
    }

    public UJciException createJciException(int err) {
	UJciException e = new UJciException(err);
	if (connectionProperties == null || !connectionProperties.getLogOnException()) {
	    return e;
	}


	StringBuffer b = new StringBuffer();
	b.append("DUMP EXCEPTION\n");
	b.append("[JCI EXCEPTION]");

	synchronized (this) {
	    getLogger().logInfo(b.toString(), e);
	}
	return e;
    }

    public UJciException createJciException(int err, int indicator, int srv_err, String msg) {
	UJciException e = new UJciException(err, indicator, srv_err, msg);
	if (connectionProperties == null || !connectionProperties.getLogOnException()) {
	    return e;
	}


	StringBuffer b = new StringBuffer();
	b.append("DUMP EXCEPTION\n");
	b.append("[JCI EXCEPTION]");

	synchronized (this) {
	    getLogger().logInfo(b.toString(), e);
	}
	return e;
    }

    public CUBRIDException logCUBRIDException(CUBRIDException e) {
	if (connectionProperties == null || !connectionProperties.getLogOnException()) {
	    return e;
	}

	StringBuffer b = new StringBuffer();
	b.append("DUMP EXCEPTION\n");
	if (e instanceof CUBRIDException) {
	    b.append("[CUBRID EXCEPTION]");
	} else {
	    b.append("[EXCEPTION]");
	}

	synchronized (this) {
	    getLogger().logInfo(b.toString(), e);
	}
	return e;
    }

    private SimpleDateFormat dateFormat = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS");
    public void logSlowQuery(long begin, long end, String sql, UBindParameter p) {
	if (connectionProperties == null || connectionProperties.getLogSlowQueris() != true) {
	    return;
	}

	long elapsed = end - begin;
	if (connectionProperties.getSlowQueryThresholdMillis() > elapsed) {
	    return;
	}

	StringBuffer b = new StringBuffer();
	b.append("SLOW QUERY\n");
	b.append(String.format("[TIME]\nSTART: %s, ELAPSED: %d\n", dateFormat.format(new Date(begin)), elapsed));
	b.append("[SQL]\n").append(sql).append('\n');
	if (p != null) {
	    b.append("[BIND]\n");
	    for (int i = 0; i < p.values.length; i++) {
		if (i != 0)
		    b.append(", ");
		b.append(p.values[i].toString());
	    }
	    b.append('\n');
	}

	synchronized (this) {
	    getLogger().logInfo(b.toString());
	}
    }

}
