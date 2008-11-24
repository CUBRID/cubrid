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

package cubrid.jdbc.driver;

/**
 * Title: CUBRID JDBC Driver Description:
 * 
 * @version 2.0
 */

public class CUBRIDJDBCErrorCode
{
  public static int unknown = 0;
  public static int connection_closed = 1;
  public static int statement_closed = 2;
  public static int prepared_statement_closed = 3;
  public static int result_set_closed = 4;
  public static int not_supported = 5;
  public static int invalid_trans_iso_level = 6;
  public static int invalid_url = 7;
  public static int no_dbname = 8;
  public static int invalid_query_type_for_executeQuery = 9;
  public static int invalid_query_type_for_executeUpdate = 10;
  public static int negative_value_for_length = 11;
  public static int ioexception_in_stream = 12;
  public static int deprecated = 13;
  public static int not_numerical_object = 14;
  public static int invalid_index = 15;
  public static int invalid_column_name = 16;
  public static int invalid_row = 17;
  public static int conversion_error = 18;
  public static int invalid_tuple = 19;
  public static int invalid_value = 20;
  public static int not_collection = 21;
  public static int dbmetadata_closed = 22;
  public static int non_scrollable = 23;
  public static int non_sensitive = 24;
  public static int non_updatable = 25;
  public static int non_updatable_column = 26;
  public static int invalid_query_type_for_executeInsert = 27;
  public static int argument_zero = 28;
  public static int empty_inputstream = 29;
  public static int empty_reader = 30;
  public static int insertion_query_fail = 31;
  public static int non_scrollable_statement = 32;
  public static int iss_fail_login = 33;
  public static int pooled_connection_closed = 34;
  public static int xa_connection_closed = 35;
  public static int xa_illegal_operation = 36;
  public static int oid_closed = 37;

  public static String[] err_msg = 
  {
      /*
       * 0
       */
      "",

      /**
       * 1 Connection이 닫히면 출력된다.
       */
      "Attempt to operate on a closed Connection.",

      /**
       * 2 Statement가 닫히면 출력된다.
       */
      "Attempt to access a closed Statement.",

      /**
       * 3 PreparedStatement가 닫히면 출력된다.
       */
      "Attempt to access a closed PreparedStatement.",

      /**
       * 4 ResultSet이 닫히면 출력된다.
       */
      "Attempt to access a closed ResultSet.",

      /**
       * 5 지원되지 않은 interface가 호출되었을 때 출력된다.
       */
      "Not supported method",

      /**
       * 6 Connection.setTransactionLevel(int level)에서 지원되지 않는 값이
       * 넘어왔을 때 출력된다.
       */
      "Unknown transaction isolation level.",

      /**
       * 7 CUBRIDDriver.connect(...)에서 url을 parsing할 수 없을 때 출력된다.
       */
      "invalid URL",

      /**
       * 8 CUBRIDDriver.connect(...)에서 Database name이 주어지지 않았을 때
       * 출력된다. Database name에는 default value가 없기 때문이다.
       */
      "The database name should be given.",

      /**
       * 9 Statement.executeQuery()에서 update query가 넘어왔을 때 출력된다.
       */
      "The query is not applicable to the executeQuery(). Use the executeUpdate() instead.",

      /**
       * 10 Statement.executeUpdate()에서 select query가 넘어왔을 때 출력된다.
       */
      "The query is not applicable to the executeUpdate(). Use the executeQuery() instead.",

      /**
       * 11 PreparedStatement.setAsciiStream(int, InputStream, int length)와
       * PreparedStatement.setBinaryStream(int, InputStream, int length)에서
       * length가 음수 일때 출력된다.
       */
      "The length of the stream cannot be negative.",

      /**
       * 12 PreparedStatement.setAsciiStream(int, InputStream x, int)와
       * PreparedStatement.setBinaryStream(int, InputStream x, int)에서
       * InputStream x를 사용하다가 IOException이 발생될 때 출력된다.
       */
      "An IOException was caught during reading the inputstream.",

      /**
       * 13 deprecated되어 지원되지 않는 interface가 호울되면 출력된다.
       */
      "Not supported method, because it is deprecated.",

      /**
       * 14 PreparedStatement.setObject(int, Object x, int, int)에서 x가
       * java.lang.Number class의 subclass가 아니면 출력된다.
       */
      "The object does not seem to be a number.",

      /**
       * 15 잘못된 index가 넘어왔을 때 출력된다.
       */
      "The index is out of range.",

      /**
       * 16 잘못된 column name이 넘어왔을 때 출력된다.
       */
      "The column name is invalid.",

      /**
       * 17 ResultSet에서 cursor가 before first나 after last인 경우 부적절한
       * 함수가 호출되면 출력된다.
       */
      "Invalid cursor position.",

      /**
       * 18 CUBRIDResultSetWithoutQuery에서 DB 값을 user가 요구하는 Java
       * type으로 전환할 수 없을 때 출력된다.
       */
      "Type conversion error.",

      /**
       * 19 CUBRIDResultSetWithoutQuery.addTuple( Object[] tuple )에서
       * tuple.length가 column의 개수와 같지 않을 때 출력된다.
       */
      "Internal error: The number of attributes is different from the expected.",

      /**
       * 20 max field size나 max rows나 query timeout등의 값이 잘못 넘어올 때
       * 출력된다.
       */
      "The argument is invalid.",

      /**
       * 21 CUBRIDResultSetMetaData에서 COLLECTION type이 아닌 column에 대해서
       * getElementType이나 getElementTypeName이 호출되었을 때 출력된다.
       */
      "The type of the column should be a collection type.",

      /**
       * 22 DatabaseMetaData가 닫히면 출력된다.
       */
      "Attempt to operate on a closed DatabaseMetaData.",

      /**
       * 23 Scrollable하지 않은 ResultSet의 Scrollability관련 메소드 호출시
       */
      "Attempt to call a method related to scrollability of non-scrollable ResultSet.",

      /**
       * 24 Scrollable하지 않은 ResultSet의 Scrollability관련 메소드 호출시
       */
      "Attempt to call a method related to sensitivity of non-sensitive ResultSet.",

      /**
       * 25 Scrollable하지 않은 ResultSet의 Scrollability관련 메소드 호출시
       */
      "Attempt to call a method related to updatability of non-updatable ResultSet.",

      /**
       * 26 Scrollable하지 않은 ResultSet의 Scrollability관련 메소드 호출시
       */
      "Attempt to update a column which cannot be updated.",

      /**
       * 27 Statement.executeInsert()에서 insert query가 아닐 때 출력된다.
       */
      "The query is not applicable to the executeInsert().",

      /**
       * 28 ResultSet.absolute()에서 argument가 0일때 발생한다.
       */
      "The argument row can not be zero.",

      /**
       * 29 비어 있는 InputStream을 argument로 받았을 때 발생한다.
       */
      "Given InputStream object has no data.",

      /**
       * 30 비어 있는 Reader argument로 받았을 때 발생한다.
       */
      "Given Reader object has no data.",

      /**
       * 31 Insert query가 실행 중 에러가 발생했을 때
       */
      "Insertion query failed.",

      /**
       * 32 TYPE_FORWARD_ONLY Statement의 setFetchDirection() 함수가 호출될 때
       * 발생
       */
      "Attempt to call a method related to scrollability of TYPE_FORWARD_ONLY Statement.",

      /**
       * 33
       */
      "Authentication failure",

      /*
       * 34 PooledConnection이 닫히면 출력된다.
       */
      "Attempt to operate on a closed PooledConnection.",

      /*
       * 35 XAConnection이 닫히면 출력된다.
       */
      "Attempt to operate on a closed XAConnection.",

      /*
       * 36 xa : setAutoCommit, commit, rollback 호출될 때
       */
      "Illegal operation in a distributed transaction",

      /*
       * 37 CUBRIDOID와 관련된 Connection 닫히면 출력된다.
       */
      "Attempt to access a CUBRIDOID associated with a Connection which has been closed."
  };

  public static String getMessage(int code)
  {
    return err_msg[code];
  }
}
