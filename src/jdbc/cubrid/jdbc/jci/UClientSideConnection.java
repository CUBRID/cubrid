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

/**
 * Title:        CUBRID Java Client Interface<p>
 * Description:  CUBRID Java Client Interface<p>
 * @version 2.0
 */

package cubrid.jdbc.jci;

import java.io.DataOutputStream;
import java.io.IOException;
import java.net.SocketTimeoutException;
import java.util.ArrayList;
import java.util.StringTokenizer;

import cubrid.jdbc.driver.CUBRIDDriver;
import cubrid.jdbc.driver.CUBRIDException;
import cubrid.jdbc.driver.CUBRIDJDBCErrorCode;
import cubrid.jdbc.driver.CUBRIDJdbcInfoTable;
import cubrid.jdbc.net.BrokerHandler;

public class UClientSideConnection extends UConnection {

	private ArrayList<String> altHosts = null;
	private int connectedHostId = 0;

	private long lastFailureTime = 0;

	public UClientSideConnection(String ip, int port, String dbname, String user, String passwd,
			String url) throws CUBRIDException {
		if (ip != null) {
			casIp = ip;
		}
		casPort = port;
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

	public UClientSideConnection(ArrayList<String> altHostList, String dbname, String user,
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
			casIp = hostPort.substring(0);
		} else {
			casIp = hostPort.substring(0, pos);
		}
		if (pos > 0) {
			casPort = Integer.valueOf(hostPort.substring(pos + 1)).intValue();
		} else {
			casPort = CUBRIDDriver.default_port;
		}
	}

	@Override
	public synchronized void setAutoCommit(boolean autoCommit) {
		if (lastAutoCommit != autoCommit) {
			lastAutoCommit = autoCommit;
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

	@Override
	public boolean getAutoCommit() {
		return lastAutoCommit;
	}

	@Override
	protected void checkReconnect() throws IOException, UJciException {
		super.checkReconnect();

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

	private void sendAppInfo() {
		String msg;
		msg = CUBRIDJdbcInfoTable.getValue();
		if (msg == null)
			return;
		check_cas(msg);
	}

	private void reconnect() throws IOException, UJciException {
		if (altHosts == null) {
			reconnectWorker(getLoginEndTimestamp(getBeginTime()));
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

	private long getLoginEndTimestamp(long timestamp) {
		int timeout = connectionProperties.getConnectTimeout();
		if (timeout <= 0) {
			return 0;
		}

		return timestamp + (timeout * 1000);
	}

	private void reconnectWorker(long endTimestamp) throws IOException, UJciException {
		if (UJCIUtil.isConsoleDebug()) {
			CUBRIDDriver.printDebug(String.format("Try Connect (%s,%d)", casIp, casPort));
		}

		int timeout = connectionProperties.getConnectTimeout() * 1000;
		client = BrokerHandler.connectBroker(casIp, casPort, getTimeout(endTimestamp, timeout));
		output = new DataOutputStream(client.getOutputStream());
		connectDB(getTimeout(endTimestamp, timeout));

		input = new UTimedDataInputStream(client.getInputStream(), casIp, casPort, casProcessId, sessionId, timeout);

		client.setTcpNoDelay(true);
		client.setSoTimeout(SOCKET_TIMEOUT);
		needReconnection = false;
		isClosed = false;

		int isolationLevel = currentIsolationLevel();
		if (isolationLevel != CUBRIDIsolationLevel.TRAN_UNKNOWN_ISOLATION)
			setIsolationLevel(isolationLevel);

		int lockTimeout = getLockTimeout ();
		if (lockTimeout != LOCK_TIMEOUT_NOT_USED)
			setLockTimeout(lockTimeout);
		/*
		 * if(!lastAutoCommit) setAutoCommit(lastAutoCommit);
		 */
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

	@Override
	public synchronized void endTransaction(boolean type) {
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
						if (!getAutoCommit() || getAutoCommitBySelf()
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
					if (getAutoCommit()) {
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

		casInfo[CAS_INFO_STATUS] = CAS_INFO_STATUS_INACTIVE;
		update_executed = false;
	}


	private void connectDB(int timeout) throws IOException, UJciException {
		UTimedDataInputStream is = new UTimedDataInputStream(client.getInputStream(), casIp, casPort, timeout);
		DataOutputStream os = new DataOutputStream(client.getOutputStream());

		// send database information
		os.write(dbInfo);

		// receive header
		int dataLength = is.readInt();
		casInfo = new byte[CAS_INFO_SIZE];
		is.readFully(casInfo);
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

		casProcessId = response;
		if (brokerInfo == null) {
			brokerInfo = new byte[BROKER_INFO_SIZE];
		}
		is.readFully(brokerInfo);

		/* synchronize with broker_info */
		byte version = brokerInfo[BROKER_INFO_PROTO_VERSION];
		if ((version & CAS_PROTO_INDICATOR) == CAS_PROTO_INDICATOR) {
			brokerVersion = makeProtoVersion(version & CAS_PROTO_VER_MASK);
		} else {
			brokerVersion = makeBrokerVersion(
					(int) brokerInfo[BROKER_INFO_MAJOR_VERSION],
					(int) brokerInfo[BROKER_INFO_MINOR_VERSION],
					(int) brokerInfo[BROKER_INFO_PATCH_VERSION]);
		}

		protocolVersion = (int)version & CAS_PROTO_VER_MASK;

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


	private void setConnectInfo(String info) throws UJciException {
		StringTokenizer st = new StringTokenizer(info, ":");
		if (st.countTokens() != 2) {
			throw createJciException(UErrorCode.ER_CONNECTION);
		}

		casIp = st.nextToken();
		casPort = Integer.valueOf(st.nextToken()).intValue();
	}

	@Override
	protected void closeInternal() {
		// jci 3.0
		if (client != null) {
			disconnect();
			clientSocketClose();
		}

		isClosed = true;
		/*
		 * jci 2.x if (transactionList != null && transactionList.size() > 0)
		 * endTransaction(false);
		 */

		// System.gc();
		// UJCIManager.deleteInList(this);
	}
}
