/*
 *
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
 */

package com.cubrid.jsp.jdbc;

import java.util.HashMap;

public class CUBRIDServerSideJDBCErrorCode {
    /* The following codes are ported from UErrorCode.java */
    public static final int ER_NO_ERROR = 0;
    public static final int ER_NOT_OBJECT = -21001;
    public static final int ER_DBMS = -21002;
    public static final int ER_COMMUNICATION = -21003;
    public static final int ER_NO_MORE_DATA = -21004;
    public static final int ER_TYPE_CONVERSION = -21005;
    public static final int ER_BIND_INDEX = -21006;
    public static final int ER_NOT_BIND = -21007;
    public static final int ER_WAS_NULL = -21008;
    public static final int ER_COLUMN_INDEX = -21009;
    public static final int ER_TRUNCATE = -21010;
    public static final int ER_SCHEMA_TYPE = -21011;
    public static final int ER_FILE = -21012;
    public static final int ER_CONNECTION = -21013;
    public static final int ER_ISO_TYPE = -21014;
    public static final int ER_ILLEGAL_REQUEST = -21015;
    public static final int ER_INVALID_ARGUMENT = -21016;
    public static final int ER_IS_CLOSED = -21017;
    public static final int ER_ILLEGAL_FLAG = -21018;
    public static final int ER_ILLEGAL_DATA_SIZE = -21019;
    public static final int ER_NO_MORE_RESULT = -21020;
    public static final int ER_OID_IS_NOT_INCLUDED = -21021;
    public static final int ER_CMD_IS_NOT_INSERT = -21022;
    public static final int ER_UNKNOWN = -21023;
    public static final int ER_TIMEOUT = -21024;
    public static final int ER_NO_SHARD_AVAILABLE = -21025;
    public static final int ER_INVALID_SHARD = -21026;
    public static final int ER_ILLEGAL_TIMESTAMP = -21027;
    public static final int ER_SSL_HANDSHAKE = -21028;

    /* The following codes are ported from CUBRIDJDBCErrorCode.java */
    public static final int ER_INVALID_QUERY_TYPE_FOR_EXECUTEQUERY = -21109;
    public static final int ER_INVALID_QUERY_TYPE_FOR_EXECUTEUPDATE = -21110;
    public static final int ER_INVALID_INDEX = -21115;
    public static final int ER_INVALID_COLUMN_NAME = -21116;
    public static final int ER_INVALID_ROW = -21117;
    public static final int ER_NOT_COLLECTION = -21121;
    public static final int ER_ARGUMENT_ZERO = -21128;

    private static HashMap<Integer, String> messageString = null;

    public static String codeToMessage(int index) {
        if (messageString == null) setMessageHash();
        return messageString.get(index);
    }

    public static String codeToMessage(int index, String msg) {
        if (messageString == null) {
            setMessageHash();
        }

        if (index == ER_DBMS && msg != null) {
            // received error message from DB server
            return msg;
        } else {
            // default error message
            return messageString.get(index);
        }
    }

    private static void setMessageHash() {
        messageString = new HashMap<Integer, String>();

        messageString.put(ER_UNKNOWN, "Error");
        messageString.put(ER_NO_ERROR, "No Error");
        messageString.put(ER_DBMS, "Server error");
        messageString.put(ER_COMMUNICATION, "Cannot communicate with the broker");
        messageString.put(ER_NO_MORE_DATA, "Invalid cursor position");
        messageString.put(ER_TYPE_CONVERSION, "Type conversion error");
        messageString.put(
                ER_BIND_INDEX, "Missing or invalid position of the bind variable provided");
        messageString.put(
                ER_NOT_BIND, "Attempt to execute the query when not all the parameters are binded");
        messageString.put(ER_WAS_NULL, "Internal Error: NULL value");
        messageString.put(ER_COLUMN_INDEX, "Column index is out of range");
        messageString.put(ER_TRUNCATE, "Data is truncated because receive buffer is too small");
        messageString.put(ER_SCHEMA_TYPE, "Internal error: Illegal schema type");
        messageString.put(ER_FILE, "File access failed");
        messageString.put(ER_CONNECTION, "Cannot connect to a broker");
        messageString.put(ER_ISO_TYPE, "Unknown transaction isolation level");
        messageString.put(
                ER_ILLEGAL_REQUEST, "Internal error: The requested information is not available");
        messageString.put(ER_INVALID_ARGUMENT, "The argument is invalid");
        messageString.put(ER_IS_CLOSED, "Connection or Statement might be closed");
        messageString.put(ER_ILLEGAL_FLAG, "Internal error: Invalid argument");
        messageString.put(
                ER_ILLEGAL_DATA_SIZE,
                "Cannot communicate with the broker or received invalid packet");
        messageString.put(ER_NOT_OBJECT, "Index's Column is Not Object");
        messageString.put(ER_NO_MORE_RESULT, "No More Result");
        messageString.put(ER_OID_IS_NOT_INCLUDED, "This ResultSet do not include the OID");
        messageString.put(ER_CMD_IS_NOT_INSERT, "Command is not insert");
        messageString.put(ER_TIMEOUT, "Request timed out");
        messageString.put(ER_NO_SHARD_AVAILABLE, "No shard available");
        messageString.put(ER_INVALID_SHARD, "Invalid shard");
        messageString.put(
                ER_ILLEGAL_TIMESTAMP, "Zero date can not be represented as java.sql.Timestamp");
        messageString.put(ER_SSL_HANDSHAKE, "SSL handshake failure");

        messageString.put(
                ER_INVALID_QUERY_TYPE_FOR_EXECUTEQUERY,
                "The query is not applicable to the executeQuery(). Use the executeUpdate() instead.");
        messageString.put(
                ER_INVALID_QUERY_TYPE_FOR_EXECUTEUPDATE,
                "The query is not applicable to the executeUpdate(). Use the executeQuery() instead.");
        messageString.put(
                ER_INVALID_INDEX, "Missing or invalid position of the bind variable provided.");
        messageString.put(ER_INVALID_COLUMN_NAME, "The column name is invalid.");
        messageString.put(ER_INVALID_ROW, "Invalid cursor position.");

        messageString.put(ER_NOT_COLLECTION, "The type of the column should be a collection type.");
        messageString.put(ER_ARGUMENT_ZERO, "The argument row can not be zero.");
    }
}
