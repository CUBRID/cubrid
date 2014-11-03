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

import java.io.DataOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.StringTokenizer;
import java.util.Vector;

import javax.transaction.xa.Xid;

import cubrid.jdbc.driver.CUBRIDConnection;
import cubrid.jdbc.driver.CUBRIDDriver;
import cubrid.jdbc.driver.CUBRIDException;
import cubrid.jdbc.driver.CUBRIDJDBCErrorCode;
import cubrid.jdbc.driver.CUBRIDJdbcInfoTable;
import cubrid.jdbc.driver.CUBRIDXid;
import cubrid.jdbc.driver.ConnectionProperties;
import cubrid.jdbc.log.BasicLogger;
import cubrid.jdbc.log.Log;
import cubrid.jdbc.net.BrokerHandler;
import cubrid.sql.CUBRIDOID;

public class UConnection {
	public final static byte DBMS_CUBRID = 1;
	public final static byte DBMS_MYSQL = 2;
	public final static byte DBMS_ORACLE = 3;
	public final static byte DBMS_PROXY_CUBRID = 4;
	public final static byte DBMS_PROXY_MYSQL = 5;
	public final static byte DBMS_PROXY_ORACLE = 6;

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

	// this value is defined in broker/cas_protocol.h
	private final static String magicString = "CUBRK";
	private final static byte CAS_CLIENT_JDBC = 3;

	public static final int PROTOCOL_V0 = 0;
	public static final int PROTOCOL_V1 = 1;
	public static final int PROTOCOL_V2 = 2;
	public static final int PROTOCOL_V3 = 3;
	public static final int PROTOCOL_V4 = 4;
	public static final int PROTOCOL_V5 = 5;
	public static final int PROTOCOL_V6 = 6;

	/* Current protocol version */
	private final static byte CAS_PROTOCOL_VERSION = PROTOCOL_V6;
	private final static byte CAS_PROTO_INDICATOR = 0x40;
	private final static byte CAS_PROTO_VER_MASK = 0x3F;
	private final static byte CAS_RENEWED_ERROR_CODE = (byte) 0x80;
	private final static byte CAS_SUPPORT_HOLDABLE_RESULT = (byte) 0x40;
	/* Do not remove and rename CAS_RECONNECT_WHEN_SERVER_DOWN */
	private final static byte CAS_RECONNECT_WHEN_SERVER_DOWN = (byte) 0x20;

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

    /* driver version */
	private final static int DRIVER_VERSION_MAX_SIZE = 20;

	/* casinfo */
	private final static byte CAS_INFO_STATUS_INACTIVE = 0;
	private final static byte CAS_INFO_STATUS_ACTIVE = 1;

	private final static int CAS_INFO_SIZE = 4;

	/* casinfo field def */
	private final static int CAS_INFO_STATUS = 0;
	private final static int CAS_INFO_RESERVED_1 = 1;
	private final static int CAS_INFO_RESERVED_2 = 2;
	private final static int CAS_INFO_ADDITIONAL_FLAG = 3;
	
	private final static byte CAS_INFO_FLAG_MASK_AUTOCOMMIT = 0x01;
	private final static byte CAS_INFO_FLAG_MASK_FORCE_OUT_TRAN = 0x02;
	private final static byte CAS_INFO_FLAG_MASK_NEW_SESSION_ID = 0x04;

	private final static int BROKER_INFO_SIZE = 8;
	private final static int BROKER_INFO_DBMS_TYPE = 0;
	private final static int BROKER_INFO_RESERVED4 = 1;
	private final static int BROKER_INFO_STATEMENT_POOLING = 2;
	private final static int BROKER_INFO_CCI_PCONNECT = 3;
	private final static int BROKER_INFO_PROTO_VERSION = 4;
	private final static int BROKER_INFO_FUNCTION_FLAG = 5;
	private final static int BROKER_INFO_RESERVED2 = 6;
	private final static int BROKER_INFO_RESERVED3 = 7;
	/* For backward compatibility */
	private final static int BROKER_INFO_MAJOR_VERSION = BROKER_INFO_PROTO_VERSION;
	private final static int BROKER_INFO_MINOR_VERSION = BROKER_INFO_FUNCTION_FLAG;
	private final static int BROKER_INFO_PATCH_VERSION = BROKER_INFO_RESERVED2;

	public static final String ZERO_DATETIME_BEHAVIOR_CONVERT_TO_NULL = "convertToNull";
	public static final String ZERO_DATETIME_BEHAVIOR_EXCEPTION = "exception";
	public static final String ZERO_DATETIME_BEHAVIOR_ROUND = "round";
	
	public static final String RESULT_WITH_CUBRID_TYPES_YES = "yes";
	public static final String RESULT_WITH_CUBRID_TYPES_NO = "no";
	
	public final static int SESSION_ID_SIZE = 20;
	
	public final static int MAX_QUERY_TIMEOUT = 2000000;
	public final static int MAX_CONNECT_TIMEOUT = 2000000;

	UOutputBuffer outBuffer;
	CUBRIDConnection cubridcon;

	boolean update_executed; /* for result cache */

	private boolean needReconnection;
	private UTimedDataInputStream input;
	private DataOutputStream output;
	public String CASIp = "";
	public int CASPort;
	public int processId;
	public int casId;
	private Socket client;
	private UError errorHandler;
	private boolean isClosed = false;
	private byte[] dbInfo;
	private int lastIsolationLevel;
	private int lastLockTimeout = LOCK_TIMEOUT_NOT_USED;
	private boolean lastAutoCommit = true;
	String dbname = "";
	String user = "";
	String passwd = "";
	String url = null;
	private ArrayList<String> altHosts = null;
	private int connectedHostId = 0;
	// jci 3.0
	private byte[] broker_info = null;
	private byte[] casinfo = null;
	private int brokerVersion = 0;

	private boolean isServerSideJdbc = false;
	boolean skip_checkcas = false;
	Vector<UStatement> pooled_ustmts;
	Vector<Integer> deferred_close_handle;
	Object curThread;

	private UUrlCache url_cache = null;
	private boolean isAutoCommitBySelf = false;

	public static byte[] driverInfo;

	private ConnectionProperties connectionProperties = new ConnectionProperties();
	private long lastFailureTime = 0;
	byte sessionId[] = createNullSession();
	int oldSessionId = 0;

	private Log log;
	private long beginTime;

	static {
		driverInfo = new byte[10];
		UJCIUtil.copy_bytes(driverInfo, 0, 5, magicString);
		driverInfo[5] = CAS_CLIENT_JDBC;
		driverInfo[6] = CAS_PROTO_INDICATOR | CAS_PROTOCOL_VERSION;
		driverInfo[7] = CAS_RENEWED_ERROR_CODE | CAS_SUPPORT_HOLDABLE_RESULT;
		driverInfo[8] = 0; // reserved
		driverInfo[9] = 0; // reserved
	}

	private int lastShardId = UShardInfo.SHARD_ID_INVALID;

    private int numShard = 0;
	UShardInfo[] shardInfo = null;

	/*
	 * the normal constructor of the class UConnection
	 */

	UConnection(String ip, int port, String dbname, String user, String passwd,
			String url) throws CUBRIDException {
		if (ip != null) {
			CASIp = ip;
		}
		CASPort = port;
		if (dbname != null) {
			this.dbname = dbname;
		}
		if (user != null) {
			this.user = user;
		}
		if (passwd != null) {
			this.passwd = passwd;
		}
		this.url = url;
		update_executed = false;

		needReconnection = true;
	    	errorHandler = new UError(this);
	}

	UConnection(ArrayList<String> altHostList, String dbname, String user,
			String passwd, String url) throws CUBRIDException {
		setAltHosts(altHostList);
		if (dbname != null) {
			this.dbname = dbname;
		}
		if (user != null) {
			this.user = user;
		}
		if (passwd != null) {
			this.passwd = passwd;
		}
		this.url = url;
		update_executed = false;

		needReconnection = true;
	    	errorHandler = new UError(this);
	}

	// This constructor is called on the server side.
	UConnection(Socket socket, Object curThread) throws CUBRIDException {
		errorHandler = new UError(this);
		try {
			client = socket;
			client.setTcpNoDelay(true);

			output = new DataOutputStream(client.getOutputStream());
			output.writeInt(0x08);
			output.flush();
			input = new UTimedDataInputStream(client.getInputStream(), CASIp, CASPort);

			needReconnection = false;
			casinfo = new byte[CAS_INFO_SIZE];
			casinfo[CAS_INFO_STATUS] = CAS_INFO_STATUS_ACTIVE;
			casinfo[CAS_INFO_RESERVED_1] = 0;
			casinfo[CAS_INFO_RESERVED_2] = 0;
			casinfo[CAS_INFO_ADDITIONAL_FLAG] = 0;
			
			/* create default broker info */
			broker_info = new byte[BROKER_INFO_SIZE];
			broker_info[BROKER_INFO_DBMS_TYPE] = DBMS_CUBRID;
			broker_info[BROKER_INFO_RESERVED4] = 0;
			broker_info[BROKER_INFO_STATEMENT_POOLING] = 1;
			broker_info[BROKER_INFO_CCI_PCONNECT] = 0;
			broker_info[BROKER_INFO_PROTO_VERSION] 
			            = CAS_PROTO_INDICATOR | CAS_PROTOCOL_VERSION;
			broker_info[BROKER_INFO_FUNCTION_FLAG] 
			            = CAS_RENEWED_ERROR_CODE | CAS_SUPPORT_HOLDABLE_RESULT;
			broker_info[BROKER_INFO_RESERVED2] = 0;
			broker_info[BROKER_INFO_RESERVED3] = 0;
			
			brokerVersion = makeProtoVersion(CAS_PROTOCOL_VERSION);
			
			isServerSideJdbc = true;
			lastAutoCommit = false;
			this.curThread = curThread;
			UJCIUtil.invoke("com.cubrid.jsp.ExecuteThread", "setCharSet",
					new Class[] { String.class }, this.curThread,
					new Object[] { connectionProperties.getCharSet() });
		} catch (IOException e) {
		    	UJciException je = new UJciException(UErrorCode.ER_CONNECTION);
		    	je.toUError(errorHandler);
			throw new CUBRIDException(errorHandler, e);
		}
	}

	public void tryConnect() throws CUBRIDException {
	    initLogger();
	    try {
		if (connectionProperties.getUseLazyConnection()) {
		    needReconnection = true;
		    return;
		}
		setBeginTime();
		checkReconnect();
		endTransaction(true);
	    } catch (UJciException e) {
		clientSocketClose();
		e.toUError(errorHandler);
		throw new CUBRIDException(errorHandler, e);
	    } catch (IOException e) {
		clientSocketClose();
		if (e instanceof SocketTimeoutException) {
		    throw new CUBRIDException(CUBRIDJDBCErrorCode.request_timeout, e);
		}
		throw new CUBRIDException(CUBRIDJDBCErrorCode.ioexception_in_stream, e);
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

	public void setResultWithCUBRIDTypes(String support) throws CUBRIDException {
		if (UJCIUtil.isServerSide() && isServerSideJdbc) {
			UJCIUtil.invoke("com.cubrid.jsp.ExecuteThread",
					"setResultWithCUBRIDTypes", new Class[] { String.class },
					this.curThread, new Object[] { support });
		}
	}
	
	public String getZeroDateTimeBehavior() {
		return connectionProperties.getZeroDateTimeBehavior();
	}
	
	public String getResultWithCUBRIDTypes() {
		return connectionProperties.getResultWithCUBRIDTypes();
	}

	public boolean getLogSlowQuery() {
	    	return connectionProperties.getLogSlowQueries();
	}

	synchronized public void addElementToSet(CUBRIDOID oid,
			String attributeName, Object value) {
		errorHandler = new UError(this);
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return;
		}
		try {
			manageElementOfSet(oid, attributeName, value,
					UConnection.ADD_ELEMENT_TO_SET);
		} catch (UJciException e) {
		    	logException(e);
			e.toUError(errorHandler);
			return;
		} catch (IOException e) {
		    	logException(e);
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
			return;
		}
	}

	synchronized public UBatchResult batchExecute(String batchSqlStmt[], int queryTimeout) {
		errorHandler = new UError(this);
		setShardId(UShardInfo.SHARD_ID_INVALID);

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

			outBuffer.newRequest(output, UFunctionCode.EXECUTE_BATCH_STATEMENT);
			outBuffer.addByte(getAutoCommit() ? (byte) 1 : (byte) 0);
			if (protoVersionIsAbove(UConnection.PROTOCOL_V4)) {
			    long remainingTime = getRemainingTime(queryTimeout * 1000);
			    if (queryTimeout > 0 && remainingTime <= 0) {
				throw createJciException(UErrorCode.ER_TIMEOUT);
			    }
			    outBuffer.addInt((int) remainingTime);
			}

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
				if (result < 0) {
					int err_code = inBuffer.readInt();
					batchResult.setResultError(i, err_code, inBuffer.readString(
							inBuffer.readInt(), UJCIManager.sysCharsetName));
				}
				else {
					batchResult.setResult(i, result);
					// jci 3.0
					inBuffer.readInt();
					inBuffer.readShort();
					inBuffer.readShort();
				}
			}

			if (protoVersionIsAbove(UConnection.PROTOCOL_V5)) {
				setShardId(inBuffer.readInt());
			}

			update_executed = true;
			return batchResult;
		} catch (UJciException e) {
		    	logException(e);
			e.toUError(errorHandler);
		} catch (IOException e) {
		    	logException(e);
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		}
		return null;
	}

	synchronized public void close() {
		errorHandler = new UError(this);
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return;
		}
		// jci 3.0
		if (client != null) {
			disconnect();
		}
		/*
		 * jci 2.x if (transactionList != null && transactionList.size() > 0)
		 * endTransaction(false);
		 */

		if (!isServerSideJdbc) {
			if (client != null) {
		    	    	clientSocketClose();
			}
		}
		// System.gc();
		// UJCIManager.deleteInList(this);
		isClosed = true;
	}

	synchronized public void dropElementInSequence(CUBRIDOID oid,
			String attributeName, int index) {
		errorHandler = new UError(this);
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return;
		}
		try {
		    	setBeginTime();
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return;

			outBuffer.newRequest(output, UFunctionCode.RELATED_TO_COLLECTION);
			outBuffer.addByte(UConnection.DROP_ELEMENT_IN_SEQUENCE);
			outBuffer.addOID(oid);
			outBuffer.addInt(index);
			if (attributeName == null)
				outBuffer.addNull();
			else
				outBuffer.addStringWithNull(attributeName);

			send_recv_msg();
		} catch (UJciException e) {
		    	logException(e);
			e.toUError(errorHandler);
		} catch (IOException e) {
		    	logException(e);
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		}
	}

	synchronized public void dropElementInSet(CUBRIDOID oid,
			String attributeName, Object value) {
		errorHandler = new UError(this);
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return;
		}
		try {
			manageElementOfSet(oid, attributeName, value,
					UConnection.DROP_ELEMENT_IN_SET);
		} catch (UJciException e) {
		    	logException(e);
			e.toUError(errorHandler);
			return;
		} catch (IOException e) {
		    	logException(e);
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
			return;
		}
	}

	synchronized public void endTransaction(boolean type) {
		errorHandler = new UError(this);

		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return;
		}

		if (needReconnection == true)
			return;

		try {
			if (client != null
					&& getCASInfoStatus() != CAS_INFO_STATUS_INACTIVE) {
			    	setBeginTime();
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
					outBuffer.newRequest(output, UFunctionCode.END_TRANSACTION);
					outBuffer.addByte((type == true) ? END_TRAN_COMMIT
							: END_TRAN_ROLLBACK);

					send_recv_msg();
					if (lastAutoCommit) {
						turnOffAutoCommitBySelf();
					}
				}
			}
		} catch (UJciException e) {
		    	logException(e);
			e.toUError(errorHandler);
		} catch (IOException e) {
		    	logException(e);
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		} catch (Exception e) {
		    	logException(e);
			errorHandler.setErrorMessage(UErrorCode.ER_UNKNOWN, e.getMessage());
		}

		/*
		 * if (transactionList == null || transactionList.size() == 0)
		 * errorHandler.clear();
		 */

		boolean keepConnection = true;
		long currentTime = System.currentTimeMillis() / 1000;
		int reconnectTime = connectionProperties.getReconnectTime();
		UUnreachableHostList unreachableHosts = UUnreachableHostList.getInstance();
		
		if (connectedHostId > 0 && lastFailureTime != 0 && reconnectTime > 0
				&& currentTime - lastFailureTime > reconnectTime) {
			if (!unreachableHosts.contains(altHosts.get(0))) {
				keepConnection = false;
				lastFailureTime = 0;
			}
		}

		if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR
				|| keepConnection == false) // jci 3.0
		{
			if (type == false) {
				errorHandler.clear();
			}

			clientSocketClose();
			needReconnection = true;
		}

		casinfo[CAS_INFO_STATUS] = CAS_INFO_STATUS_INACTIVE;
		update_executed = false;
	}

	synchronized public OutputStream getOutputStream() {
		return output;
	}

	synchronized public UStatement getByOID(CUBRIDOID oid,
			String[] attributeName) {
		UStatement returnValue = null;

		errorHandler = new UError(this);
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return null;
		}
		try {
		    	setBeginTime();
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return null;

			outBuffer.newRequest(output, UFunctionCode.GET_BY_OID);
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
		    	logException(e);
			e.toUError(errorHandler);
			return null;
		} catch (IOException e) {
		    	logException(e);
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
			return null;
		}
		if (returnValue.getRecentError().getErrorCode() != UErrorCode.ER_NO_ERROR) {
			errorHandler.copyValue(returnValue.getRecentError());
			return null;
		}
		return returnValue;
	}

	synchronized public String getDatabaseProductVersion() {
		errorHandler = new UError(this);
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return null;
		}
		try {
		    	setBeginTime();
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return null;

			outBuffer.newRequest(output, UFunctionCode.GET_DB_VERSION);
			outBuffer.addByte(getAutoCommit() ? (byte) 1 : (byte) 0);

			UInputBuffer inBuffer;
			inBuffer = send_recv_msg();

			return inBuffer.readString(inBuffer.remainedCapacity(),
					UJCIManager.sysCharsetName);
		} catch (UJciException e) {
		    	logException(e);
			e.toUError(errorHandler);
		} catch (IOException e) {
		    	logException(e);
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		}
		return null;
	}

	synchronized public int getIsolationLevel() {
		errorHandler = new UError(this);

		if (lastIsolationLevel != CUBRIDIsolationLevel.TRAN_UNKNOWN_ISOLATION) {
			return lastIsolationLevel;
		}

		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return CUBRIDIsolationLevel.TRAN_UNKNOWN_ISOLATION;
		}
		try {
			setBeginTime();
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return CUBRIDIsolationLevel.TRAN_UNKNOWN_ISOLATION;

			outBuffer.newRequest(output, UFunctionCode.GET_DB_PARAMETER);
			outBuffer.addInt(DB_PARAM_ISOLATION_LEVEL);

			UInputBuffer inBuffer;
			inBuffer = send_recv_msg();

			lastIsolationLevel = inBuffer.readInt();
			return lastIsolationLevel;
		} catch (UJciException e) {
			logException(e);
			e.toUError(errorHandler);
		} catch (IOException e) {
			logException(e);
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		}
		return CUBRIDIsolationLevel.TRAN_UNKNOWN_ISOLATION;
	}

	public UError getRecentError() {
		return errorHandler;
	}

	synchronized public String getQueryplanOnly(String sql) {
		String ret_val;

		if (sql == null)
			return null;

		errorHandler = new UError(this);
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return null;
		}

		try {
		    	setBeginTime();
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
		    	logException(e);
			e.toUError(errorHandler);
			return null;
		} catch (IOException e) {
		    	logException(e);
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
			String arg2, byte flag, int shard_id) {
		UStatement returnValue = null;

		errorHandler = new UError(this);
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
		    	setBeginTime();
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return null;

			outBuffer.newRequest(output, UFunctionCode.GET_SCHEMA_INFO);
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

			if (protoVersionIsAbove(PROTOCOL_V5)) {
				outBuffer.addInt(shard_id);
			}

			UInputBuffer inBuffer;
			inBuffer = send_recv_msg();

			returnValue = new UStatement(this, arg1, arg2, type, inBuffer);
		} catch (UJciException e) {
		    	logException(e);
			e.toUError(errorHandler);
			return null;
		} catch (IOException e) {
		    	logException(e);
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
		errorHandler = new UError(this);
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return 0;
		}
		try {
		    	setBeginTime();
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return 0;

			outBuffer.newRequest(output, UFunctionCode.RELATED_TO_COLLECTION);
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
		    	logException(e);
			e.toUError(errorHandler);
		} catch (IOException e) {
		    	logException(e);
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		}
		return 0;
	}

	synchronized public void insertElementIntoSequence(CUBRIDOID oid,
			String attributeName, int index, Object value) {
		errorHandler = new UError(this);
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return;
		}
		try {
			manageElementOfSequence(oid, attributeName, index, value,
					UConnection.INSERT_ELEMENT_INTO_SEQUENCE);
		} catch (UJciException e) {
		    	logException(e);
			e.toUError(errorHandler);
			return;
		} catch (IOException e) {
		    	logException(e);
			if (errorHandler.getErrorCode() != UErrorCode.ER_CONNECTION)
				errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
			return;
		}
	}

	public boolean isClosed() {
		return isClosed;
	}

    public boolean isErrorCommunication (int error) {
	switch (error) {
	case UErrorCode.ER_COMMUNICATION:
	case UErrorCode.ER_ILLEGAL_DATA_SIZE:
	case UErrorCode.CAS_ER_COMMUNICATION:
	    return true;
	default:
	    return false;
	}
    }
    
    public boolean isErrorToReconnect(int error) {
	if (isErrorCommunication(error)) {
	  return true;
	}

	switch (error) {
	case -111: // ER_TM_SERVER_DOWN_UNILATERALLY_ABORTED
	case -199: // ER_NET_SERVER_CRASHED
	case -224: // ER_OBJ_NO_CONNECT
	case -677: // ER_BO_CONNECT_FAILED
	    return true;
	default:
	    return false;
	}
    }

    private UStatement prepareInternal(String sql, byte flag, boolean recompile)
	    throws IOException, UJciException {
	errorHandler.clear();

	outBuffer.newRequest(output, UFunctionCode.PREPARE);
	outBuffer.addStringWithNull(sql);
	outBuffer.addByte(flag);
	outBuffer.addByte(getAutoCommit() ? (byte) 1 : (byte) 0);

	while (deferred_close_handle.isEmpty() != true) {
	    Integer close_handle = (Integer) deferred_close_handle.remove(0);
	    outBuffer.addInt(close_handle.intValue());
	}

	UInputBuffer inBuffer = send_recv_msg();
	UStatement stmt;
	if (recompile) {
	    stmt = new UStatement(this, inBuffer, true, sql, flag);
	} else {
	    stmt = new UStatement(this, inBuffer, false, sql, flag);
	}

	if (stmt.getRecentError().getErrorCode() != UErrorCode.ER_NO_ERROR) {
	    errorHandler.copyValue(stmt.getRecentError());
	    return null;
	}

	pooled_ustmts.add(stmt);
	
	return stmt;
    }

    synchronized public UStatement prepare(String sql, byte flag) {
	return prepare(sql, flag, false);
    }

    synchronized public UStatement prepare(String sql, byte flag,
	    boolean recompile) {
	errorHandler = new UError(this);
	if (isClosed) {
	    errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
	    return null;
	}

	UStatement stmt = null;
	boolean isFirstPrepareInTran = !isActive();

	skip_checkcas = true;

	// first
	try {
	    checkReconnect();
	    stmt = prepareInternal(sql, flag, recompile);
	    return stmt;
	} catch (UJciException e) {
	    logException(e);
	    e.toUError(errorHandler);
	} catch (IOException e) {
	    logException(e);
	    errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
	    errorHandler.setStackTrace(e.getStackTrace());
	} finally {
	    skip_checkcas = false;
	}

	if (isActive() && !isFirstPrepareInTran) {
	    return null;
	}

	// second loop
	while (isErrorToReconnect(errorHandler.getJdbcErrorCode())) {
	    if (!brokerInfoReconnectWhenServerDown()
		|| isErrorCommunication (errorHandler.getJdbcErrorCode())) {
		clientSocketClose();
	    }

	    try {
		errorHandler.clear();
		checkReconnect();
		if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR) {
		    return null;
		}
	    } catch (UJciException e) {
		logException(e);
		e.toUError(errorHandler);
		return null;
	    } catch (IOException e) {
		logException(e);
		errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		errorHandler.setStackTrace(e.getStackTrace());
		return null;
	    }

	    try {
		stmt = prepareInternal(sql, flag, recompile);
		return stmt;
	    } catch (UJciException e) {
		logException(e);
		e.toUError(errorHandler);
	    } catch (IOException e) {
		logException(e);
		errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		errorHandler.setStackTrace(e.getStackTrace());
	    }
	} 

	return null;
    }

	synchronized public void putByOID(CUBRIDOID oid, String attributeName[],
			Object values[]) {
		errorHandler = new UError(this);
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

			setBeginTime();
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return;

			outBuffer.newRequest(output, UFunctionCode.PUT_BY_OID);
			outBuffer.addOID(oid);
			if (putParameter != null)
				putParameter.writeParameter(outBuffer);

			send_recv_msg();
			if (getAutoCommit()) {
				turnOnAutoCommitBySelf();
			}
		} catch (UJciException e) {
		    	logException(e);
			e.toUError(errorHandler);
		} catch (IOException e) {
		    	logException(e);
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		}
	}

	synchronized public void putElementInSequence(CUBRIDOID oid,
			String attributeName, int index, Object value) {
		errorHandler = new UError(this);
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return;
		}
		try {
			manageElementOfSequence(oid, attributeName, index, value,
					UConnection.PUT_ELEMENT_ON_SEQUENCE);
		} catch (UJciException e) {
		    	logException(e);
			e.toUError(errorHandler);
			return;
		} catch (IOException e) {
		    	logException(e);
			if (errorHandler.getErrorCode() != UErrorCode.ER_CONNECTION)
				errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
			return;
		}
	}

	synchronized public void setIsolationLevel(int level) {
		errorHandler = new UError(this);

		if (lastIsolationLevel != CUBRIDIsolationLevel.TRAN_UNKNOWN_ISOLATION &&
		 	lastIsolationLevel == level) {
			return;
		}

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
			setBeginTime();
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return;

			outBuffer.newRequest(output, UFunctionCode.SET_DB_PARAMETER);
			outBuffer.addInt(DB_PARAM_ISOLATION_LEVEL);
			outBuffer.addInt(level);

			send_recv_msg();

			lastIsolationLevel = level;
		} catch (UJciException e) {
			logException(e);
			e.toUError(errorHandler);
		} catch (IOException e) {
			logException(e);
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		}
	}

	synchronized public void setLockTimeout(int timeout) {
		errorHandler = new UError(this);

		if (lastLockTimeout != LOCK_TIMEOUT_NOT_USED && 
			lastLockTimeout == timeout) {
			return;
		}

		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return;
		}

		try {
		    	setBeginTime();
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return;

			outBuffer.newRequest(output, UFunctionCode.SET_DB_PARAMETER);
			outBuffer.addInt(DB_PARAM_LOCK_TIMEOUT);
			outBuffer.addInt(timeout);

			send_recv_msg();

			if (timeout < 0)
				lastLockTimeout = LOCK_TIMEOUT_INFINITE;
			else
				lastLockTimeout = timeout;
		} catch (UJciException e) {
		    	logException(e);
			e.toUError(errorHandler);
		} catch (IOException e) {
		    	logException(e);
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		}
	}
	
	synchronized public int setCASChangeMode(int mode) {
		errorHandler = new UError(this);

		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return errorHandler.getJdbcErrorCode();
		}

		try {
		    	setBeginTime();
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR) {
				return errorHandler.getJdbcErrorCode();
			}

			outBuffer.newRequest(output, UFunctionCode.SET_CAS_CHANGE_MODE);
			outBuffer.addInt(mode);

			UInputBuffer inBuffer;
			inBuffer = send_recv_msg();
			
			return inBuffer.readInt();			
		} catch (UJciException e) {
		    	logException(e);
			e.toUError(errorHandler);
		} catch (IOException e) {
		    	logException(e);
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		}
		
		return errorHandler.getJdbcErrorCode();
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
	    byte dbms_type = getDbmsType();
		if (dbms_type == DBMS_CUBRID 
			|| dbms_type == DBMS_PROXY_CUBRID) {
			return true;
		}
		return false;
	}

	public boolean isConnectedToOracle() {
	    byte dbms_type = getDbmsType();
		if (dbms_type == DBMS_ORACLE 
			|| dbms_type == DBMS_PROXY_ORACLE) {
			return true;
		}
		return false;
	}

	public boolean isConnectedToProxy() {
	    byte dbms_type = getDbmsType();
		if (dbms_type == DBMS_PROXY_CUBRID 
			|| dbms_type == DBMS_PROXY_MYSQL 
			|| dbms_type == DBMS_PROXY_ORACLE) {
			return true;
		}
		return false;
	}

	public boolean brokerInfoStatementPooling() {
		if (broker_info == null)
			return false;

		if (broker_info[BROKER_INFO_STATEMENT_POOLING] == (byte) 1)
			return true;
		else
			return false;
	}

	public boolean brokerInfoRenewedErrorCode() {
	    if ((broker_info[BROKER_INFO_PROTO_VERSION] & CAS_PROTO_INDICATOR)
		    != CAS_PROTO_INDICATOR) {
		return false;
	    }

	    return (broker_info[BROKER_INFO_FUNCTION_FLAG] & CAS_RENEWED_ERROR_CODE)
	    	== CAS_RENEWED_ERROR_CODE;
	}
	
	public boolean brokerInfoSupportHoldableResult() {
		if (broker_info == null)
			return false;
			
	    return (broker_info[BROKER_INFO_FUNCTION_FLAG] & CAS_SUPPORT_HOLDABLE_RESULT)
	    	== CAS_SUPPORT_HOLDABLE_RESULT;
	}

	public boolean brokerInfoReconnectWhenServerDown() {
		if (broker_info == null)
			return false;
			
	    return (broker_info[BROKER_INFO_FUNCTION_FLAG] & CAS_RECONNECT_WHEN_SERVER_DOWN)
	    	== CAS_RECONNECT_WHEN_SERVER_DOWN;
	}

	public boolean supportHoldableResult() {
	    if (brokerInfoSupportHoldableResult()
				|| protoVersionIsSame(UConnection.PROTOCOL_V2)) {
		return true;
	    }

	    return false;
	}

	synchronized public void xa_endTransaction(Xid xid, boolean type) {
		errorHandler = new UError(this);

		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return;
		}

		try {
		    	setBeginTime();
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return;

			outBuffer.newRequest(output, UFunctionCode.XA_END_TRAN);
			outBuffer.addXid(xid);
			outBuffer.addByte((type == true) ? END_TRAN_COMMIT
					: END_TRAN_ROLLBACK);

			send_recv_msg();
		} catch (Exception e) {
			errorHandler.setErrorCode(UErrorCode.ER_UNKNOWN);
		} finally {
		    	clientSocketClose();
		    	needReconnection = true;
		}
	}

	synchronized public void xa_prepare(Xid xid) {
		errorHandler = new UError(this);

		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return;
		}

		try {
		    	setBeginTime();
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return;

			outBuffer.newRequest(output, UFunctionCode.XA_PREPARE);
			outBuffer.addXid(xid);

			send_recv_msg();
		} catch (Exception e) {
			errorHandler.setErrorCode(UErrorCode.ER_UNKNOWN);
		}
	}

	synchronized public Xid[] xa_recover() {
		errorHandler = new UError(this);

		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return null;
		}

		try {
		    	setBeginTime();
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return null;

			outBuffer.newRequest(output, UFunctionCode.XA_RECOVER);

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
			return true;
		}

		try {
			outBuffer.newRequest(output, UFunctionCode.CHECK_CAS);
			send_recv_msg();
		} catch (IOException e) {
		    	logException(e);
			return false;
		} catch (UJciException e) {
		    	logException(e);
			return false;
		}

		return true;
	}

	synchronized public boolean check_cas(String msg) {
		try {
			outBuffer.newRequest(output, UFunctionCode.CHECK_CAS);
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
		errorHandler = new UError(this);
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return null;
		}
		try {
		    	setBeginTime();
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return null;

			outBuffer.newRequest(output, UFunctionCode.RELATED_TO_OID);
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
		    	logException(e);
			e.toUError(errorHandler);
		} catch (IOException e) {
		    	logException(e);
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		}

		return null;
	}

	synchronized public byte[] lobNew(int lob_type) {
		errorHandler = new UError(this);
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return null;
		}
		try {
		    	setBeginTime();
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return null;

			outBuffer.newRequest(output, UFunctionCode.NEW_LOB);
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
		    	logException(e);
			e.toUError(errorHandler);
		} catch (IOException e) {
		    	logException(e);
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		} catch (Exception e) {
		    	logException(e);
			errorHandler.setErrorCode(UErrorCode.ER_UNKNOWN);
		}
		return null;

	}

	synchronized public int lobWrite(byte[] packedLobHandle, long offset,
			byte[] buf, int start, int len) {
		errorHandler = new UError(this);
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return -1;
		}
		try {
		    	setBeginTime();
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return -1;

			outBuffer.newRequest(output, UFunctionCode.WRITE_LOB);
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
		    	logException(e);
			e.toUError(errorHandler);
		} catch (IOException e) {
		    	logException(e);
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		} catch (Exception e) {
		    	logException(e);
			errorHandler.setErrorCode(UErrorCode.ER_UNKNOWN);
		}
		return -1;
	}

	synchronized public int lobRead(byte[] packedLobHandle, long offset,
			byte[] buf, int start, int len) {
		errorHandler = new UError(this);
		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return -1;
		}
		try {
		    	setBeginTime();
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return -1;

			outBuffer.newRequest(output, UFunctionCode.READ_LOB);
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
		    	logException(e);
			e.toUError(errorHandler);
		} catch (IOException e) {
		    	logException(e);
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		} catch (Exception e) {
		    	logException(e);
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
	
	public static byte[] createDBInfo(String dbname, String user, String passwd, String url) {
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
		byte[] info = new byte[32 + 32 + 32 + 512 + 20];
		UJCIUtil.copy_bytes(info, 0, 32, dbname);
		UJCIUtil.copy_bytes(info, 32, 32, user);
		UJCIUtil.copy_bytes(info, 64, 32, passwd);
		UJCIUtil.copy_bytes(info, 96, 511, url);

		if (url == null)
		{
			UJCIUtil.copy_byte(info, 96, (byte) 0); // null
			UJCIUtil.copy_byte(info, 97, (byte) 0); // length
		}
		else
		{
			String version = CUBRIDDriver.version_string;
			int index = 96 + url.getBytes().length + 1;
			if ((version.getBytes().length <= DRIVER_VERSION_MAX_SIZE)
				&& (url.getBytes().length + version.getBytes().length + 3 <= 512)) {
				
				// url = ( url string + length (1byte) + version string )
				byte len = (byte) version.getBytes().length;
				UJCIUtil.copy_byte(info, index, len);
				UJCIUtil.copy_bytes(info, index + 1, version.getBytes().length + 1, version);
			}
			else
			{
				UJCIUtil.copy_byte(info, index, (byte) 0); // length
			}
		}
		return info;
	}

	void clientSocketClose() {
		try {
			needReconnection = true;
			if (client != null) {
				client.setSoLinger(true, 0);
				client.close();
			}
			client = null;
		} catch (IOException e) {
		    	logException(e);
		}
		clearPooledUStatements();
		deferred_close_handle.clear();
	}

	UInputBuffer send_recv_msg(boolean recv_result) throws UJciException,
			IOException {
		byte prev_casinfo[] = casinfo;
		outBuffer.sendData();
		/* set cas info to UConnection member variable and return InputBuffer */
		UInputBuffer inputBuffer = new UInputBuffer(input, this);

		if (UJCIUtil.isConsoleDebug()) {
			printCasInfo(prev_casinfo, casinfo);
		}
		return inputBuffer;
	}

	UInputBuffer send_recv_msg() throws UJciException, IOException {
		if (client == null) {
			createJciException(UErrorCode.ER_COMMUNICATION);
		}
		return send_recv_msg(true);
	}

	void cancel() throws UJciException, IOException {
	    if (protoVersionIsAbove(PROTOCOL_V4)) {
		BrokerHandler.cancelBrokerEx(CASIp, CASPort, processId, 0);
	    } else {
	    	BrokerHandler.cancelBroker(CASIp, CASPort, processId, 0);
	    }
	}

	UUrlCache getUrlCache() {
		if (url_cache == null) {
			UUrlHostKey key = new UUrlHostKey(CASIp, CASPort, dbname, user);
			url_cache = UJCIManager.getUrlCache(key);
		}
		return url_cache;
	}

    private int getTimeout(long endTimestamp, int timeout) throws UJciException {
	if (endTimestamp == 0) {
	    return timeout;
	}

	long diff = endTimestamp - System.currentTimeMillis();
	if (diff <= 0) {
	    throw new UJciException(UErrorCode.ER_TIMEOUT);
	}
	if (diff < timeout) {
	    return (int)diff;
	}

	return timeout;
    }

    private void reconnectWorker(long endTimestamp) throws IOException, UJciException {
	if (UJCIUtil.isConsoleDebug()) {
	    CUBRIDDriver.printDebug(String.format("Try Connect (%s,%d)", CASIp, CASPort));
	}

	int timeout = connectionProperties.getConnectTimeout() * 1000;
	client = BrokerHandler.connectBroker(CASIp, CASPort, getTimeout(endTimestamp, timeout));
	output = new DataOutputStream(client.getOutputStream());
	input = new UTimedDataInputStream(client.getInputStream(), CASIp, CASPort);
	connectDB(getTimeout(endTimestamp, timeout));

	client.setTcpNoDelay(true);
	client.setSoTimeout(SOCKET_TIMEOUT);
	needReconnection = false;
	isClosed = false;

	if (lastIsolationLevel != CUBRIDIsolationLevel.TRAN_UNKNOWN_ISOLATION)
	    setIsolationLevel(lastIsolationLevel);
	if (lastLockTimeout != LOCK_TIMEOUT_NOT_USED)
	    setLockTimeout(lastLockTimeout);
	/*
	 * if(!lastAutoCommit) setAutoCommit(lastAutoCommit);
	 */
    }

	private void connectDB(int timeout) throws IOException, UJciException {
		UTimedDataInputStream is = new UTimedDataInputStream(client.getInputStream(), CASIp, CASPort, timeout);
		DataOutputStream os = new DataOutputStream(client.getOutputStream());

		// send database information
		os.write(dbInfo);

		// receive header
		int dataLength = is.readInt();
		casinfo = new byte[CAS_INFO_SIZE];
		is.readFully(casinfo);
		if (dataLength < 0) {
		    throw new UJciException(UErrorCode.ER_ILLEGAL_DATA_SIZE);
		}

		// receive data
		int response = is.readInt();
		if (response < 0) {
		    int code = is.readInt();
		    // the error greater than -10000 with CAS_ERROR_INDICATOR is sent by old broker
		    // -1018 (CAS_ER_NOT_AUTHORIZED_CLIENT) is especial case
		    if ((response == UErrorCode.CAS_ERROR_INDICATOR && code > -10000)
			    || code == -1018) {
			code -= 9000;
		    }
		    byte msg[] = new byte[dataLength - 8];
		    is.readFully(msg);
		    throw new UJciException(UErrorCode.ER_DBMS, response, code,
			    new String(msg, 0, Math.max(msg.length - 1, 0)));
		}

		processId = response;
		if (broker_info == null) {
	    		broker_info = new byte[BROKER_INFO_SIZE];
		}
		is.readFully(broker_info);

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

		if (protoVersionIsAbove(PROTOCOL_V4)) {
		    casId = is.readInt();
		} else {
		    casId = -1;
		}
	
		if (protoVersionIsAbove(PROTOCOL_V3)) {
		    is.readFully(sessionId);
		} else {
		    oldSessionId = is.readInt();
		}

	}

	private boolean setActiveHost(int hostId) throws UJciException {
		if (hostId >= altHosts.size())
			return false;

		String info = altHosts.get(hostId);
		setConnectInfo(info);
		return true;
	}

    private long getLoginEndTimestamp(long timestamp) {
	int timeout = connectionProperties.getConnectTimeout();
	if (timeout <= 0) {
	    return 0;
	}

	return timestamp + (timeout * 1000);
    }

    private void reconnect() throws IOException, UJciException {
	if (altHosts == null) {
	    reconnectWorker(getLoginEndTimestamp(beginTime));
	} else {
	    int retry = 0;
	    UUnreachableHostList unreachableHosts = UUnreachableHostList.getInstance();
	    
	    do {
		for (int hostId = 0; hostId < altHosts.size(); hostId++) {
		    /*
		     * if all hosts turn out to be unreachable, ignore host
		     * reachability and try one more time
		     */
		    if (!unreachableHosts.contains(altHosts.get(hostId)) || retry == 1) {
			try {
			    setActiveHost(hostId);
			    reconnectWorker(getLoginEndTimestamp(System.currentTimeMillis()));
			    connectedHostId = hostId;
			    
			    unreachableHosts.remove(altHosts.get(hostId));
			    
			    return; // success to connect
			} catch (IOException e) {
			    logException(e);
			    throw e;
			} catch (UJciException e) {
			    logException(e);
			    int errno = e.getJciError();
			    if (errno == UErrorCode.ER_COMMUNICATION
				    || errno == UErrorCode.ER_CONNECTION
				    || errno == UErrorCode.ER_TIMEOUT
				    || errno == UErrorCode.CAS_ER_FREE_SERVER) {
			    	unreachableHosts.add(altHosts.get(hostId));
			    } else {
				throw e;
			    }
			}
		    }
		    lastFailureTime = System.currentTimeMillis() / 1000;
		}
		retry++;
	    } while (retry < 2);
	    // failed to connect to neither hosts
	    throw createJciException(UErrorCode.ER_CONNECTION);
	}
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

	public boolean protoVersionIsSame(int ver) {
		if (brokerInfoVersion() == makeProtoVersion(ver)) {
			return true;
		}
		return false;
	}

	public boolean protoVersionIsUnder(int ver) {
		if (brokerInfoVersion() < makeProtoVersion(ver)) {
			return true;
		}
		return false;
	}

	public boolean protoVersionIsAbove(int ver) {
		if (isServerSideJdbc()
				|| (brokerInfoVersion() >= makeProtoVersion(ver))) {
			return true;
		}
		return false;
	}

	private void setConnectInfo(String info) throws UJciException {
		StringTokenizer st = new StringTokenizer(info, ":");
		if (st.countTokens() != 2) {
			throw createJciException(UErrorCode.ER_CONNECTION);
		}

		CASIp = st.nextToken();
		CASPort = Integer.valueOf(st.nextToken()).intValue();
	}

	private void manageElementOfSequence(CUBRIDOID oid, String attributeName,
			int index, Object value, byte flag) throws UJciException,
			IOException {
		UAParameter aParameter;
		aParameter = new UAParameter(attributeName, value);

		setBeginTime();
		checkReconnect();
		if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
			return;

		outBuffer.newRequest(output, UFunctionCode.RELATED_TO_COLLECTION);
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

		setBeginTime();
		checkReconnect();
		if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
			return;

		outBuffer.newRequest(output, UFunctionCode.RELATED_TO_COLLECTION);
		outBuffer.addByte(flag);
		outBuffer.addOID(oid);
		aParameter.writeParameter(outBuffer);

		send_recv_msg();
	}

	void checkReconnect() throws IOException, UJciException {
		if (dbInfo == null) {
			dbInfo = createDBInfo(dbname, user, passwd, url);
		}
		// set the session id
		if (brokerInfoVersion() == 0) {
			/* Interpretable session information supporting version 
			*   later than PROTOCOL_V3 as well as version earlier 
			*   than PROTOCOL_V3 should be delivered since no broker information 
			*   is provided at the time of initial connection.
			*/
			String id = "0";
			UJCIUtil.copy_bytes(dbInfo, 608, 20, id);
		} else if (protoVersionIsAbove(PROTOCOL_V3)) {
			System.arraycopy(sessionId, 0, dbInfo, 608, 20);
		} else {
		    	UJCIUtil.copy_bytes(dbInfo, 608, 20, new Integer(oldSessionId).toString());
		}

		if (outBuffer == null) {
			outBuffer = new UOutputBuffer(this);
		}

		if (pooled_ustmts == null) {
			pooled_ustmts = new Vector<UStatement>();
		}
		
		if (deferred_close_handle == null) {
			deferred_close_handle = new Vector<Integer>();
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

	public void closeSession() {
		try {
		    	setBeginTime();
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return;
			outBuffer.newRequest(output, UFunctionCode.END_SESSION);
			send_recv_msg();
			sessionId = createNullSession();
			oldSessionId = 0;
		} catch (Exception e) {
		}
	}

	private byte[] createNullSession() {
	    return new byte[SESSION_ID_SIZE];
	}

	// jci 3.0
	private void disconnect() {
		try {
		    	setBeginTime();
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return;

			outBuffer.newRequest(output, UFunctionCode.CON_CLOSE);
			send_recv_msg();
		} catch (Exception e) {
		}
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

    public void setConnectionProperties(ConnectionProperties connProperties) {
		this.connectionProperties = connProperties;
    }

    private Log getLogger() {
		if (log == null) {
		    log = new BasicLogger(connectionProperties.getLogFile());
		}
		return log;
    }

    private void initLogger() {
           if (connectionProperties.getLogOnException() || connectionProperties.getLogSlowQueries()) {
               log = getLogger();
           }
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
		logException(e);
		return e;
    }

    public void logException(Throwable t) {
		if (connectionProperties == null || !connectionProperties.getLogOnException()) {
		    return;
		}
	
		StringBuffer b = new StringBuffer();
		b.append("DUMP EXCEPTION\n");
		b.append("[" + t.getClass().getName() + "]");
	
		synchronized (this) {
		    getLogger().logInfo(b.toString(), t);
		}
    }

    private SimpleDateFormat dateFormat = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS");
    public void logSlowQuery(long begin, long end, String sql, UBindParameter p) {
	if (connectionProperties == null || connectionProperties.getLogSlowQueries() != true) {
	    return;
	}

	long elapsed = end - begin;
	if (connectionProperties.getSlowQueryThresholdMillis() > elapsed) {
	    return;
	}

	StringBuffer b = new StringBuffer();
	b.append("SLOW QUERY\n");
	b.append(String.format("[CAS INFO]\n%s:%d, %d, %d\n",
                 CASIp, CASPort, casId, processId));
	b.append(String.format("[TIME]\nSTART: %s, ELAPSED: %d\n",
				dateFormat.format(new Date(begin)),
				elapsed));
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

    public boolean isActive() {
	return getCASInfoStatus() == CAS_INFO_STATUS_ACTIVE;
    }

    public void setBeginTime() {
	beginTime = System.currentTimeMillis();
    }

    public long getRemainingTime(long timeout) {
	if (beginTime == 0 || timeout == 0) {
	    return timeout;
	}

	long now = System.currentTimeMillis();
	return timeout - (now - beginTime);
    }

    public void resetBeginTime() {
	beginTime = 0;
    }

    public boolean isRenewedSessionId() {
	return (brokerInfoReconnectWhenServerDown()
		  && ((casinfo[CAS_INFO_ADDITIONAL_FLAG] 
	             & CAS_INFO_FLAG_MASK_NEW_SESSION_ID) 
	                == CAS_INFO_FLAG_MASK_NEW_SESSION_ID));
    }

    public void setNewSessionId(byte[] newSessionId) {
	sessionId = newSessionId;
    }

    public void setShardId(int shardId)
    {
		lastShardId = shardId;
    }

    public int getShardId()
    {
		return lastShardId;
    }

	public int getShardCount()
	{
		if (isConnectedToProxy() == false)
		{
			return 0;
		}

		if (numShard == 0)
		{
   			 int num_shard = shardInfo();
			 if (num_shard == 0 
			 	|| errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
			 {
				return 0;
			 }
		}

		return numShard;
	}

   synchronized public int shardInfo() {
		errorHandler = new UError(this);

		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return 0;
		}

        if (isConnectedToProxy() == false)
		{
			errorHandler.setErrorCode(UErrorCode.ER_NO_SHARD_AVAILABLE);
			return 0;
		}

		if (numShard > 0) {
			return numShard;	// return cached shard info
		}

		try {
		    	setBeginTime();
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR) {
				return 0;
			}

			outBuffer.newRequest(output, UFunctionCode.GET_SHARD_INFO);

			UInputBuffer inBuffer;
			inBuffer = send_recv_msg();

			int num_shard = inBuffer.getResCode();
			if (num_shard > 0) {
				shardInfo = new UShardInfo[num_shard];

				for (int i=0; i<num_shard; i++) {
					shardInfo[i] = new UShardInfo(inBuffer.readInt());
					
					shardInfo[i].setDBName(inBuffer.readString(inBuffer.readInt(), UJCIManager.sysCharsetName));
					shardInfo[i].setDBServer(inBuffer.readString(inBuffer.readInt(), UJCIManager.sysCharsetName));
				}

				numShard = num_shard;
			}

		} catch (UJciException e) {
		   	logException(e);
			e.toUError(errorHandler);
		} catch (IOException e) {
		   	logException(e);
			errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
		}

		return numShard;
   }

   synchronized public UShardInfo getShardInfo(int shard_id) {
		errorHandler = new UError(this);

		if (isClosed == true) {
			errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
			return null;
		}

        if (isConnectedToProxy() == false)
		{
			errorHandler.setErrorCode(UErrorCode.ER_NO_SHARD_AVAILABLE);
			return null;
		}

		if (numShard == 0)
		{
   			 int num_shard = shardInfo();
			 if (num_shard == 0 
			 	|| errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
			 {
				return null;
			 }
		}
		
		if (shard_id < 0 || shard_id >= numShard)
		{
			errorHandler.setErrorCode(UErrorCode.ER_INVALID_SHARD);
			return null;
		}

		return shardInfo[shard_id];
	}
}
