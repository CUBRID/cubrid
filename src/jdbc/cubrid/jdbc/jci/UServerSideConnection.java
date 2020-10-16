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
import java.net.Socket;
import java.util.List;

import cubrid.jdbc.driver.CUBRIDException;

public class UServerSideConnection extends UConnection {
	public final static int INVOKE = 2;
	
	private final static byte CAS_CLIENT_SERVER_SIDE_JDBC = 6;
	
	private Thread curThread;
	private UStatementHandlerCache stmtHandlerCache;
	
	public UServerSideConnection(Socket socket, Thread curThread) throws CUBRIDException {
		errorHandler = new UError(this);
		try {
			client = socket;
			client.setTcpNoDelay(true);

			output = new DataOutputStream(client.getOutputStream());
			input = new UTimedDataInputStream(client.getInputStream(), casIp, casPort);

			needReconnection = false;
			lastAutoCommit = false;
			
			/* initialize default info */
			driverInfo[5] = CAS_CLIENT_SERVER_SIDE_JDBC;
			initBrokerInfo ();
			initCasInfo ();

			this.curThread = curThread;
			UJCIUtil.invoke("com.cubrid.jsp.ExecuteThread", "setCharSet",
					new Class[] { String.class }, this.curThread,
					new Object[] { connectionProperties.getCharSet() });
			
			stmtHandlerCache = new UStatementHandlerCache ();
		} catch (IOException e) {
			UJciException je = new UJciException(UErrorCode.ER_CONNECTION);
			je.toUError(errorHandler);
			throw new CUBRIDException(errorHandler, e);
		}
	}
	
	private void initBrokerInfo () {
		brokerInfo = new byte[BROKER_INFO_SIZE];
		brokerInfo[BROKER_INFO_DBMS_TYPE] = DBMS_CUBRID;
		brokerInfo[BROKER_INFO_RESERVED4] = 0;
		brokerInfo[BROKER_INFO_STATEMENT_POOLING] = 1;
		brokerInfo[BROKER_INFO_CCI_PCONNECT] = 0;
		brokerInfo[BROKER_INFO_PROTO_VERSION] 
				= CAS_PROTO_INDICATOR | CAS_PROTOCOL_VERSION;
		brokerInfo[BROKER_INFO_FUNCTION_FLAG] 
				= CAS_RENEWED_ERROR_CODE | CAS_SUPPORT_HOLDABLE_RESULT;
		brokerInfo[BROKER_INFO_RESERVED2] = 0;
		brokerInfo[BROKER_INFO_RESERVED3] = 0;

		brokerVersion = makeProtoVersion(CAS_PROTOCOL_VERSION);
	}
	
	private void initCasInfo () {
		casInfo = new byte[CAS_INFO_SIZE];
		casInfo[CAS_INFO_STATUS] = CAS_INFO_STATUS_ACTIVE;
		casInfo[CAS_INFO_RESERVED_1] = 0;
		casInfo[CAS_INFO_RESERVED_2] = 0;
		casInfo[CAS_INFO_ADDITIONAL_FLAG] = 0;
	}
	
	@Override
	public void setCharset(String newCharsetName) {
		if (UJCIUtil.isServerSide()) {
			UJCIUtil.invoke("com.cubrid.jsp.ExecuteThread", "setCharSet",
					new Class[] { String.class }, this.curThread,
					new Object[] { newCharsetName });
		}
	}

	@Override
	public void setZeroDateTimeBehavior(String behavior) {
		if (UJCIUtil.isServerSide()) {
			UJCIUtil.invoke("com.cubrid.jsp.ExecuteThread",
					"setZeroDateTimeBehavior", new Class[] { String.class },
					this.curThread, new Object[] { behavior });
		}
	}

	@Override
	public void setResultWithCUBRIDTypes(String support) {
		if (UJCIUtil.isServerSide()) {
			UJCIUtil.invoke("com.cubrid.jsp.ExecuteThread",
					"setResultWithCUBRIDTypes", new Class[] { String.class },
					this.curThread, new Object[] { support });
		}
	}

	@Override
	public void setAutoCommit(boolean autoCommit) {
		/* do nothing */
	}

	@Override
	public boolean getAutoCommit() {
		return false;
	}

	@Override
	public void endTransaction(boolean type) {
		/* do nothing */
	}

	@Override
	public boolean protoVersionIsAbove(int ver) {
		/* do not need to check protocol version for internal JDBC */
		return true;
	}

	@Override
	public boolean protoVersionIsUnder(int ver) {
		/* do not need to check protocol version for internal JDBC */
		return true;
	}

	@Override
	protected void disconnect() {
		try {
			setBeginTime();
			checkReconnect();
			if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
				return;

			outBuffer.newRequest(output, UFunctionCode.CON_CLOSE);
			UInputBuffer in = send_recv_msg();
			in.readInt(); // consume result code(0) from fn_con_close
		} catch (Exception e) {
		}
	}
	
	@Override
	protected void closeInternal() {
		if (client != null) {
			stmtHandlerCache.clearStatus();
			disconnect();
			UJCIUtil.invoke("com.cubrid.jsp.ExecuteThread", "setStatus",
					new Class[] { Integer.class }, this.curThread,
					new Object[] { INVOKE });
		}
	}
	
	@Override
	protected UStatement prepareInternal(String sql, byte flag, boolean recompile)
			throws IOException, UJciException {
		UStatement preparedStmt = null;
		
		List<UStatementHandlerCacheEntry> entries = stmtHandlerCache.getEntry(sql);	
		/* try to find cached UStatement */
	
		for (UStatementHandlerCacheEntry e: entries) {
			if (e.isAvailable()) {
				preparedStmt = e.getStatement();
				preparedStmt.initToReuse();
				preparedStmt.moveCursor(0, UStatement.CURSOR_SET);
				e.setAvailable(false);
				return preparedStmt;
			}
		}
		
		/* if entry not found, create new UStatement */
		if (preparedStmt == null) {
			UStatement newStmt = super.prepareInternal(sql, flag, recompile);
			entries.add(new UStatementHandlerCacheEntry (newStmt));
			preparedStmt = newStmt;
		}
		
		return preparedStmt;
	}
	
	public void destroy () {
		stmtHandlerCache.destroy();
	}
}
