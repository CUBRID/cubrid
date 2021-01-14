/*
 * Copyright (C) 2008 Search Solution Corporation. 
 * Copyright (c) 2016 CUBRID Corporation.
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

package cubrid.jdbc.jci;


public class UJciException extends Exception {
    	private static final long serialVersionUID = 4464106407657785825L;

	private int jciErrCode;
	private int serverErrCode;
	private int serverErrIndicator;

	public UJciException(int err) {
		super();
		jciErrCode = err;
	}

	public UJciException(int err, Throwable t) {
	    	super();
		jciErrCode = err;
		setStackTrace(t.getStackTrace());
	}

	public UJciException(int err, int indicator, int srv_err, String msg) {
		super(msg);
		jciErrCode = err;
		serverErrCode = srv_err;
		if (serverErrCode <= UError.METHOD_USER_ERROR_BASE)
			serverErrCode = UError.METHOD_USER_ERROR_BASE - serverErrCode;
		serverErrIndicator = indicator;
	}

	void toUError(UError error) {
	    	error.setStackTrace(getStackTrace());
		if (jciErrCode == UErrorCode.ER_DBMS) {
			String msg;
			if (serverErrIndicator == UErrorCode.DBMS_ERROR_INDICATOR) {
				msg = getMessage();
			} else {
				msg = UErrorCode.codeToCASMessage(serverErrCode);
			}
			error.setDBError(serverErrCode, msg);
		} else if (jciErrCode == UErrorCode.ER_UNKNOWN) {
			error.setErrorMessage(jciErrCode, getMessage());
		} else {
			error.setErrorCode(jciErrCode);
		}
	}

	public int getJciError() {
		return this.jciErrCode;
	}

	public String toString() {
	    	String msg, indicator;
		int errorCode;
		if (jciErrCode == UErrorCode.ER_DBMS) {
			if (serverErrIndicator == UErrorCode.DBMS_ERROR_INDICATOR) {
				msg = getMessage();
				indicator = "ER_DBMS";
				errorCode = serverErrCode;
			} else {
				msg = UErrorCode.codeToCASMessage(serverErrCode);
				indicator = "ER_BROKER";
				errorCode = jciErrCode;
			}
		} else {
		    	msg = getMessage();
		    	indicator = "ER_DRIVER";
		    	errorCode = jciErrCode;
		}
		return String.format("%s[%d,%s]", indicator, errorCode, msg);
	}
}
