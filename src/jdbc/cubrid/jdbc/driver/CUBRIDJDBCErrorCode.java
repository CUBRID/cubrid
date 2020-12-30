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

package cubrid.jdbc.driver;

import java.util.Hashtable;


/**
 * Title: CUBRID JDBC Driver Description:
 * 
 * @version 2.0
 */

public class CUBRIDJDBCErrorCode {

	public static int unknown = -21100;
	public static int connection_closed = -21101;
	public static int statement_closed = -21102;
	public static int prepared_statement_closed = -21103;
	public static int result_set_closed = -21104;
	public static int not_supported = -21105;
	public static int invalid_trans_iso_level = -21106;
	public static int invalid_url = -21107;
	public static int no_dbname = -21108;
	public static int invalid_query_type_for_executeQuery = -21109;
	public static int invalid_query_type_for_executeUpdate = -21110;
	public static int negative_value_for_length = -21111;
	public static int ioexception_in_stream = -21112;
	public static int deprecated = -21113;
	public static int not_numerical_object = -21114;
	public static int invalid_index = -21115;
	public static int invalid_column_name = -21116;
	public static int invalid_row = -21117;
	public static int conversion_error = -21118;
	public static int invalid_tuple = -21119;
	public static int invalid_value = -21120;
	public static int not_collection = -21121;
	public static int dbmetadata_closed = -21122;
	public static int non_scrollable = -21123;
	public static int non_sensitive = -21124;
	public static int non_updatable = -21125;
	public static int non_updatable_column = -21126;
	public static int invalid_query_type_for_executeInsert = -21127;
	public static int argument_zero = -21128;
	public static int empty_inputstream = -21129;
	public static int empty_reader = -21130;
	public static int insertion_query_fail = -21131;
	public static int non_scrollable_statement = -21132;
	public static int iss_fail_login = -21133;
	public static int pooled_connection_closed = -21134;
	public static int xa_connection_closed = -21135;
	public static int xa_illegal_operation = -21136;
	public static int oid_closed = -21137;
	public static int invalid_table_name = -21138;
	public static int lob_pos_invalid = -21139;
	public static int lob_is_not_writable = -21140;
	public static int request_timeout = -21141;

	private static Hashtable<Integer, String> messageString;

	private static void setMessageHash() {
		messageString = new Hashtable<Integer, String>();

		messageString.put(new Integer(unknown), "");
		messageString.put(new Integer(connection_closed), 
			"Attempt to operate on a closed Connection.");
		messageString.put(new Integer(statement_closed), 
			"Attempt to access a closed Statement.");
		messageString.put(new Integer(prepared_statement_closed), 
			"Attempt to access a closed PreparedStatement.");
		messageString.put(new Integer(result_set_closed), 
			"Attempt to access a closed ResultSet.");
		messageString.put(new Integer(not_supported), 
			"Not supported method");
		messageString.put(new Integer(invalid_trans_iso_level), 
			"Unknown transaction isolation level.");
		messageString.put(new Integer(invalid_url), 
			"invalid URL - ");
		messageString.put(new Integer(no_dbname), 
			"The database name should be given.");
		messageString.put(new Integer(invalid_query_type_for_executeQuery), 
			"The query is not applicable to the executeQuery(). Use the executeUpdate() instead.");
		messageString.put(new Integer(invalid_query_type_for_executeUpdate), 
			"The query is not applicable to the executeUpdate(). Use the executeQuery() instead.");
		messageString.put(new Integer(negative_value_for_length), 
			"The length of the stream cannot be negative.");
		messageString.put(new Integer(ioexception_in_stream), 
			"An IOException was caught during reading the inputstream.");
		messageString.put(new Integer(deprecated), 
			"Not supported method, because it is deprecated.");
		messageString.put(new Integer(not_numerical_object), 
			"The object does not seem to be a number.");
		messageString.put(new Integer(invalid_index), 
			"Missing or invalid position of the bind variable provided.");
		messageString.put(new Integer(invalid_column_name), 
			"The column name is invalid.");
		messageString.put(new Integer(invalid_row), 
			"Invalid cursor position.");
		messageString.put(new Integer(conversion_error), 
			"Type conversion error.");
		messageString.put(new Integer(invalid_tuple), 
			"Internal error: The number of attributes is different from the expected.");
		messageString.put(new Integer(invalid_value), 
			"The argument is invalid.");
		messageString.put(new Integer(not_collection), 
			"The type of the column should be a collection type.");
		messageString.put(new Integer(dbmetadata_closed), 
			"Attempt to operate on a closed DatabaseMetaData.");
		messageString.put(new Integer(non_scrollable), 
			"Attempt to call a method related to scrollability of non-scrollable ResultSet.");
		messageString.put(new Integer(non_sensitive), 
			"Attempt to call a method related to sensitivity of non-sensitive ResultSet.");
		messageString.put(new Integer(non_updatable), 
			"Attempt to call a method related to updatability of non-updatable ResultSet.");
		messageString.put(new Integer(non_updatable_column), 
			"Attempt to update a column which cannot be updated.");
		messageString.put(new Integer(invalid_query_type_for_executeInsert), 
			"The query is not applicable to the executeInsert().");
		messageString.put(new Integer(argument_zero), 
			"The argument row can not be zero.");
		messageString.put(new Integer(empty_inputstream), 
			"Given InputStream object has no data.");
		messageString.put(new Integer(empty_reader), 
			"Given Reader object has no data.");
		messageString.put(new Integer(insertion_query_fail), 
			"Insertion query failed.");
		messageString.put(new Integer(non_scrollable_statement), 
			"Attempt to call a method related to scrollability of TYPE_FORWARD_ONLY Statement.");
		messageString.put(new Integer(iss_fail_login), 
			"Authentication failure");
		messageString.put(new Integer(pooled_connection_closed), 
			"Attempt to operate on a closed PooledConnection.");
		messageString.put(new Integer(xa_connection_closed), 
			"Attempt to operate on a closed XAConnection.");
		messageString.put(new Integer(xa_illegal_operation), 
			"Illegal operation in a distributed transaction");
		messageString.put(new Integer(oid_closed), 
			"Attempt to access a CUBRIDOID associated with a Connection which has been closed.");
		messageString.put(new Integer(invalid_table_name), 
			"The table name is invalid.");
		messageString.put(new Integer(lob_pos_invalid), 
			"Lob position to write is invalid.");
		messageString.put(new Integer(lob_is_not_writable), 
			"Lob is not writable.");
		messageString.put(new Integer(request_timeout), 
			"Request timed out.");
	}

	public static String getMessage(int code) {
		if (messageString == null)
			setMessageHash();
		return (String) messageString.get(new Integer(code));
	}
}
