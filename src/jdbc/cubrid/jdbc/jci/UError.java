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

public class UError {
	public final static int METHOD_USER_ERROR_BASE = -100000;

	private UConnection connection = null;
	private int jciErrorCode;
	private int serverErrorCode;
	private String errorMessage;
	private StackTraceElement[] stackTrace;

	UError(UConnection c) {
	    	connection = c;
		jciErrorCode = UErrorCode.ER_NO_ERROR;
	}

	public UError(UError src) {
		copyValue(src);
	}

	public int getErrorCode() {
		return jciErrorCode;
	}

	private int getSessionNumber(byte[] session) {
	    int ch1 = session[8];
	    int ch2 = session[9];
	    int ch3 = session[10];
	    int ch4 = session[11];

	    return ((ch1 << 24) + (ch2 << 16) + (ch3 << 8) + (ch4 << 0));
	}

	public String getErrorMsg() {
	    	if (connection != null) {
	    	    if (connection.protoVersionIsAbove(UConnection.PROTOCOL_V4)) {
	    		return String.format("%s[CAS INFO - %s:%d,%d,%d],[SESSION-%d],[URL-%s].",
				errorMessage, connection.CASIp,
				connection.CASPort, connection.casId,
	    			connection.processId ,
				getSessionNumber(connection.sessionId),
	    			connection.url);
	    	    } else if (connection.protoVersionIsAbove(UConnection.PROTOCOL_V3)) {
	    		return String.format("%s[CAS INFO - %s:%d,%d],[SESSION-%d],[URL-%s].", 
				errorMessage, connection.CASIp,
				connection.CASPort,connection.processId,
	    			getSessionNumber(connection.sessionId),
				connection.url);
	    	    } else {
	    		return String.format("%s[CAS INFO - %s:%d,%d],[SESSION-%d],[URL-%s].", 
				errorMessage, connection.CASIp,
				connection.CASPort, connection.processId,
	    			connection.oldSessionId, connection.url);
	    	    }
	    	}
		return errorMessage;
	}

	public int getJdbcErrorCode() {
		if (jciErrorCode == UErrorCode.ER_NO_ERROR)
			return UErrorCode.ER_NO_ERROR;
		else if (jciErrorCode == UErrorCode.ER_DBMS)
			return serverErrorCode;
		else
			return (jciErrorCode);
	}

	void copyValue(UError object) {
	    	connection = object.connection;
		jciErrorCode = object.jciErrorCode;
		errorMessage = object.errorMessage;
		serverErrorCode = object.serverErrorCode;
		stackTrace = object.stackTrace;
	}

	synchronized void setDBError(int code, String message) {
		jciErrorCode = UErrorCode.ER_DBMS;
		serverErrorCode = code;
		errorMessage = message;
	}

	synchronized void setErrorCode(int code) {
		jciErrorCode = code;
		if (code != UErrorCode.ER_NO_ERROR) {
			errorMessage = UErrorCode.codeToMessage(code);
		}
	}

	void setErrorMessage(int code, String addMessage) {
		setErrorCode(code);
		errorMessage += ":" + addMessage;
	}

	void clear() {
		jciErrorCode = UErrorCode.ER_NO_ERROR;
	}

	public void setStackTrace(StackTraceElement[] stackTrace) {
	    this.stackTrace = stackTrace;
	}

	public void changeStackTrace(Throwable t) {
	    if (stackTrace != null) {
		t.setStackTrace(stackTrace);
	    }
	}
}
