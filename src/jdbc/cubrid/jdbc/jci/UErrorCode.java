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

import java.util.Hashtable;

/*
 * Error codes and messages in JCI
 */

abstract public class UErrorCode {
	public static final int ER_NO_ERROR = 0;
	public static final int ER_NOT_OBJECT = 1;
	public static final int ER_DBMS = 2;
	public static final int ER_COMMUNICATION = 3;
	public static final int ER_NO_MORE_DATA = 4;
	public static final int ER_TYPE_CONVERSION = 5;
	public static final int ER_BIND_INDEX = 6;
	public static final int ER_NOT_BIND = 7;
	public static final int ER_WAS_NULL = 8;
	public static final int ER_COLUMN_INDEX = 9;
	public static final int ER_TRUNCATE = 10;
	public static final int ER_SCHEMA_TYPE = 11;
	public static final int ER_FILE = 12;
	public static final int ER_CONNECTION = 13;
	public static final int ER_ISO_TYPE = 14;
	public static final int ER_ILLEGAL_REQUEST = 15;
	public static final int ER_INVALID_ARGUMENT = 16;
	public static final int ER_IS_CLOSED = 17;
	public static final int ER_ILLEGAL_FLAG = 18;
	public static final int ER_ILLEGAL_DATA_SIZE = 19;
	public static final int ER_NO_MORE_RESULT = 20;
	public static final int ER_OID_IS_NOT_INCLUDED = 21;
	public static final int ER_CMD_IS_NOT_INSERT = 22;
	public static final int ER_UNKNOWN = 23;

	/* CAS Error Code */

	public static final int CAS_ER_DBMS = -1000;
	public static final int CAS_ER_INTERNAL = -1001;
	public static final int CAS_ER_NO_MORE_MEMORY = -1002;
	public static final int CAS_ER_COMMUNICATION = -1003;
	public static final int CAS_ER_ARGS = -1004;
	public static final int CAS_ER_TRAN_TYPE = -1005;
	public static final int CAS_ER_SRV_HANDLE = -1006;
	public static final int CAS_ER_NUM_BIND = -1007;
	public static final int CAS_ER_UNKNOWN_U_TYPE = -1008;
	public static final int CAS_ER_DB_VALUE = -1009;
	public static final int CAS_ER_TYPE_CONVERSION = -1010;
	public static final int CAS_ER_PARAM_NAME = -1011;
	public static final int CAS_ER_NO_MORE_DATA = -1012;
	public static final int CAS_ER_OBJECT = -1013;
	public static final int CAS_ER_OPEN_FILE = -1014;
	public static final int CAS_ER_SCHEMA_TYPE = -1015;
	public static final int CAS_ER_VERSION = -1016;
	public static final int CAS_ER_FREE_SERVER = -1017;
	public static final int CAS_ER_NOT_AUTHORIZED_CLIENT = -1018;
	public static final int CAS_ER_QUERY_CANCEL = -1019;
	public static final int CAS_ER_NOT_COLLECTION = -1020;
	public static final int CAS_ER_COLLECTION_DOMAIN = -1021;
	public static final int CAS_ER_NO_MORE_RESULT_SET = -1022;
	public static final int CAS_ER_INVALID_CALL_STMT = -1023;
	public static final int CAS_ER_STMT_POOLING = -1024;
	public static final int CAS_ER_DBSERVER_DISCONNECTED = -1025;
	public static final int CAS_ER_MAX_PREPARED_STMT_COUNT_EXCEEDED = -1026;
	public static final int CAS_ER_HOLDABLE_NOT_ALLOWED = -1027;
	public static final int CAS_ER_NOT_IMPLEMENTED = -1100;
	public static final int CAS_ER_IS = -1200;

	public final static int CAS_ERROR_INDICATOR = -1;
	public final static int DBMS_ERROR_INDICATOR = -2;

	private static Hashtable messageString, CASMessageString;

	public static String codeToMessage(int index) {
		if (messageString == null)
			setMessageHash();
		return (String) messageString.get(new Integer(index));
	}

	public static String codeToCASMessage(int index) {
		if (CASMessageString == null)
			setCASMessageHash();
		return (String) CASMessageString.get(new Integer(index));
	}

	private static void setMessageHash() {
		messageString = new Hashtable(24);

		messageString.put(new Integer(ER_UNKNOWN), "Error");
		messageString.put(new Integer(ER_NO_ERROR), "No Error");
		messageString.put(new Integer(ER_DBMS), "Server error");
		messageString.put(new Integer(ER_COMMUNICATION),
				"Cannot communicate with the broker");
		messageString.put(new Integer(ER_NO_MORE_DATA),
				"Invalid cursor position");
		messageString.put(new Integer(ER_TYPE_CONVERSION),
				"Type conversion error");
		messageString.put(new Integer(ER_BIND_INDEX),
				"Missing or invalid position of the bind variable provided");
		messageString
				.put(new Integer(ER_NOT_BIND),
						"Attempt to execute the query when not all the parameters are binded");
		messageString.put(new Integer(ER_WAS_NULL),
				"Internal Error: NULL value");
		messageString.put(new Integer(ER_COLUMN_INDEX),
				"Column index is out of range");
		messageString.put(new Integer(ER_TRUNCATE),
				"Data is truncated because receive buffer is too small");
		messageString.put(new Integer(ER_SCHEMA_TYPE),
				"Internal error: Illegal schema type");
		messageString.put(new Integer(ER_FILE), "File access failed");
		messageString.put(new Integer(ER_CONNECTION),
				"Cannot connect to a broker");
		messageString.put(new Integer(ER_ISO_TYPE),
				"Unknown transaction isolation level");
		messageString.put(new Integer(ER_ILLEGAL_REQUEST),
				"Internal error: The requested information is not available");
		messageString.put(new Integer(ER_INVALID_ARGUMENT),
				"The argument is invalid");
		messageString.put(new Integer(ER_IS_CLOSED),
				"Connection or Statement might be closed");
		messageString.put(new Integer(ER_ILLEGAL_FLAG),
				"Internal error: Invalid argument");
		messageString
				.put(new Integer(ER_ILLEGAL_DATA_SIZE),
						"Cannot communicate with the broker or received invalid packet");
		messageString.put(new Integer(ER_NOT_OBJECT),
				"Index's Column is Not Object");
		messageString.put(new Integer(ER_NO_MORE_RESULT), "No More Result");
		messageString.put(new Integer(ER_OID_IS_NOT_INCLUDED),
				"This ResultSet do not include the OID");
		messageString.put(new Integer(ER_CMD_IS_NOT_INSERT),
				"Command is not insert");
	}

	private static void setCASMessageHash() {
		CASMessageString = new Hashtable(30);

		CASMessageString.put(new Integer(CAS_ER_DBMS),
				"Database connection error");
		CASMessageString.put(new Integer(CAS_ER_INTERNAL),
				"General server error");
		CASMessageString.put(new Integer(CAS_ER_NO_MORE_MEMORY),
				"Memory allocation error");
		CASMessageString.put(new Integer(CAS_ER_COMMUNICATION),
				"Communication error");
		CASMessageString.put(new Integer(CAS_ER_ARGS), "Invalid argument");
		CASMessageString.put(new Integer(CAS_ER_TRAN_TYPE),
				"Unknown transaction type");
		CASMessageString.put(new Integer(CAS_ER_SRV_HANDLE),
				"Internal server error");
		CASMessageString.put(new Integer(CAS_ER_NUM_BIND),
				"Parameter binding error");
		CASMessageString.put(new Integer(CAS_ER_UNKNOWN_U_TYPE),
				"Parameter binding error");
		CASMessageString.put(new Integer(CAS_ER_DB_VALUE),
				"Cannot make DB_VALUE");
		CASMessageString.put(new Integer(CAS_ER_TYPE_CONVERSION),
				"Type conversion error");
		CASMessageString.put(new Integer(CAS_ER_PARAM_NAME),
				"Invalid database parameter name");
		CASMessageString.put(new Integer(CAS_ER_NO_MORE_DATA), "No more data");
		CASMessageString.put(new Integer(CAS_ER_OBJECT), "Object is not valid");
		CASMessageString.put(new Integer(CAS_ER_OPEN_FILE), "File open error");
		CASMessageString.put(new Integer(CAS_ER_SCHEMA_TYPE),
				"Invalid schema type");
		CASMessageString.put(new Integer(CAS_ER_VERSION), "Version mismatch");
		CASMessageString.put(new Integer(CAS_ER_FREE_SERVER),
				"Cannot process the request. Try again later");
		CASMessageString.put(new Integer(CAS_ER_NOT_AUTHORIZED_CLIENT),
				"Authorization error");
		CASMessageString.put(new Integer(CAS_ER_QUERY_CANCEL),
				"Cannot cancel the query");
		CASMessageString.put(new Integer(CAS_ER_NOT_COLLECTION),
				"The attribute domain must be the set type");
		CASMessageString.put(new Integer(CAS_ER_COLLECTION_DOMAIN),
				"The domain of a set must be the same data type");
		CASMessageString.put(new Integer(CAS_ER_NO_MORE_RESULT_SET),
				"No More Result");
		CASMessageString.put(new Integer(CAS_ER_INVALID_CALL_STMT),
				"Illegal CALL statement");
		CASMessageString.put(new Integer(CAS_ER_STMT_POOLING),
				"Statement Pooling");
		CASMessageString.put(new Integer(CAS_ER_DBSERVER_DISCONNECTED),
				"DB Server disconnected");
		CASMessageString.put(new Integer(CAS_ER_MAX_PREPARED_STMT_COUNT_EXCEEDED),
				"Cannot prepare more than MAX_PREPARED_STMT_COUNT statements");
		CASMessageString.put(new Integer(CAS_ER_HOLDABLE_NOT_ALLOWED),
				"Holdable results may not be updatable or sensitive");
		CASMessageString.put(new Integer(CAS_ER_NOT_IMPLEMENTED),
				"Attempt to use a not supported service");
		CASMessageString.put(new Integer(CAS_ER_IS), "Authentication failure");
	}
}
