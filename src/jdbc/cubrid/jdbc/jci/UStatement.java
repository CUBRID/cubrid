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

import java.sql.*;
import java.io.IOException;
import java.math.BigDecimal;
import java.util.ArrayList;

import cubrid.sql.CUBRIDOID;
import cubrid.jdbc.driver.CUBRIDOutResultSet;
import cubrid.jdbc.driver.CUBRIDResultSet;

/**
 * Performance 향상을 위한 precompile된 SQL statement를 표현하고 그 ResultSet을
 * manage하는 class이다. 모든 statement는 UStatement로 표현되며 input
 * parameter를 가지고 있어 같은 format의 statement를 reuse할 수 있다.
 * 
 * Execute하기 전에 모든 input parameter는 bind되어야 한다. 모든 public
 * interface는 method 수행 중에 발생한 error를 UError object인 errorHandler에
 * set한다. 따라서 UConnection의 public interface를 호출할 때에는 반드시 method
 * getRecentError()를 call하여 error를 check하여야 한다.
 * 
 * Internal Note
 * 
 * flag isClosed를 가지고 있어 CAS와의 Connection이 끊어졌을 때 method들이
 * call될 때에는 Error를 Set한다. prepare할 때 parameter의 type 정보를 알 수
 * 없으므로 bind value를 확인하고 그 type에 해당하는 CUBRID Type정보를 match하여
 * parameter정보를 set한다. 따라서 user는 parameter를 set할 때 parameter의
 * type에 대한 정보를 알고 있어야 한다.
 * 
 * since 1.0
 */

public class UStatement
{
  /*
   * ResultSet의 cursor의 origin을 정의한다. CURSOR_SET은 ResultSet의 처음을
   * CURSOR_CUR는 현재 위치를, CURSOR_END는 ResultSet의 끝을 의미한다.
   */

  public final static int CURSOR_SET = 0, CURSOR_CUR = 1, CURSOR_END = 2;

  public final static byte QUERY_INFO_PLAN = 0x01;

  /*
   * statement를 execute할 때 사용되는 flag이다. NORMAL_EXECUTE는 synchronous
   * execute를 ASYNC_EXECUTE는 asynchronous execute를 의미한다.
   */

  private final static byte NORMAL_EXECUTE = 0, ASYNC_EXECUTE = 1;
  private final static byte NORMAL = 0, GET_BY_OID = 1, GET_SCHEMA_INFO = 2,
      GET_AUTOINCREMENT_KEYS = 3;
  private final static byte TRUE = -128, FALSE = 0;
  private final static int DEFAULT_FETCH_SIZE = 100;

  private final static byte EXEC_FLAG_ASYNC = 0x01, EXEC_FLAG_QUERY_ALL = 0x02,
      EXEC_FLAG_QUERY_INFO = 0x04, EXEC_FLAG_ONLY_QUERY_PLAN = 0x08;

  private byte statementType;

  private UConnection relatedConnection;
  private boolean isClosed;
  private boolean realFetched;
  private boolean isUpdatable;
  private boolean isSensitive;

  private int serverHandler;
  private int parameterNumber;
  private int columnNumber;
  private UBindParameter bindParameter;
  private ArrayList batchParameter;
  private UColumnInfo columnInfo[], firstColumnInfo[];
  private UResultInfo resultInfo[];
  private byte commandTypeIs;
  private byte executeFlag;

  // private String oidString;
  private String className;

  private int fetchDirection;
  private int fetchSize;
  private int maxFetchSize;
  private int fetchedTupleNumber;
  private int totalTupleNumber;
  private int currentFirstCursor;
  private int cursorPosition;
  private int executeResult;
  private UResultTuple tuples[];

  private UError errorHandler;

  private UOutputBuffer outBuffer;

  private int schemaType;
  private boolean isReturnable = false;
  private String sql_stmt;
  private byte prepare_flag;
  private UInputBuffer tmp_inbuffer;
  private boolean isAutoCommit = false;
  private boolean isGeneratedKeys = false;

  /*
   * 3.0 private int resultset_index; private int resultset_index_flag; private
   * UParameterInfo parameterInfo[]; private int pramNumber;
   */

  public int result_cache_lifetime;
  private boolean result_cacheable = false;
  private UStmtCache stmt_cache;

  /*
   * normal statement를 위한 UStatement constructor ResultSet의 column정보를
   * Fetch한다.
   */

  UStatement(UConnection relatedC, UInputBuffer inBuffer, boolean assign_only,
      String sql, byte _prepare_flag) throws IOException, UJciException
  {
    errorHandler = new UError();
    if (assign_only)
    {
      relatedConnection = relatedC;
      tmp_inbuffer = inBuffer;
      sql_stmt = sql;
      prepare_flag = _prepare_flag;
    }
    else
    {
      init(relatedC, inBuffer, sql, _prepare_flag, true);
    }

    if (result_cacheable
        && (prepare_flag & UConnection.PREPARE_INCLUDE_OID) == 0
        && (prepare_flag & UConnection.PREPARE_UPDATABLE) == 0)
    {
      UUrlCache url_cache = relatedC.getUrlCache();
      stmt_cache = url_cache.getStmtCache(sql);
    }
  }

  private void init(UConnection relatedC, UInputBuffer inBuffer, String sql,
      byte _prepare_flag, boolean clear_bind_info) throws IOException,
      UJciException
  {
    sql_stmt = sql;
    prepare_flag = _prepare_flag;
    outBuffer = relatedC.outBuffer;
    statementType = NORMAL;
    relatedConnection = relatedC;

    serverHandler = inBuffer.getResCode();
    result_cache_lifetime = inBuffer.readInt();
    if (result_cache_lifetime >= 0 && UJCIManager.result_cache_enable)
      result_cacheable = true;
    commandTypeIs = inBuffer.readByte();
    parameterNumber = inBuffer.readInt();
    isUpdatable = (inBuffer.readByte() == 1) ? true : false;
    columnNumber = inBuffer.readInt();
    firstColumnInfo = readColumnInfo(inBuffer);
    columnInfo = firstColumnInfo;

    if (clear_bind_info)
    {
      if (parameterNumber > 0)
        bindParameter = new UBindParameter(parameterNumber);
      else
        bindParameter = null;
      batchParameter = null;
    }
    fetchSize = DEFAULT_FETCH_SIZE;
    currentFirstCursor = cursorPosition = totalTupleNumber = fetchedTupleNumber = 0;
    maxFetchSize = 0;
    realFetched = false;
    isClosed = false;

    if (commandTypeIs == CUBRIDCommandType.CUBRID_STMT_CALL_SP)
      columnNumber = parameterNumber + 1;

    /*
     * 3.0 resultset_index = 0; resultset_index_flag =
     * java.sql.Statement.CLOSE_CURRENT_RESULT;
     */
  }

  /*
   * UConnection.getByOid에 의해 allocate되는 UStatement의 constructor
   * constructor에서 ResultSet의 Column정보 뿐만 아니라 ResultSet까지 Fetch한다.
   */

  UStatement(UConnection relatedC, CUBRIDOID oid, String attributeName[],
      UInputBuffer inBuffer) throws IOException, UJciException
  {
    outBuffer = relatedC.outBuffer;
    statementType = GET_BY_OID;
    relatedConnection = relatedC;
    // oidString = oString;

    errorHandler = new UError();

    serverHandler = -1;

    className = inBuffer.readString(inBuffer.readInt(),
        relatedConnection.conCharsetName);
    columnNumber = inBuffer.readInt();
    firstColumnInfo = readColumnInfo(inBuffer);
    columnInfo = firstColumnInfo;
    fetchSize = 1;
    tuples = new UResultTuple[fetchSize];

    readATupleByOid(oid, inBuffer);

    bindParameter = null;
    batchParameter = null;
    totalTupleNumber = fetchedTupleNumber = 1;
    currentFirstCursor = cursorPosition = 0;
    maxFetchSize = 0;
    realFetched = false;
    isUpdatable = false;
    isClosed = false;

    /*
     * 3.0 resultset_index = 0; resultset_index_flag =
     * java.sql.Statement.CLOSE_CURRENT_RESULT;
     */
  }

  /*
   * UConnection.getSchemaInfo에 의해 allocate되는 UStatement의 constructor
   * ResultSet의 columnInfo를 fetch한다.
   */

  UStatement(UConnection relatedC, String cName, String attributePattern,
      int type, UInputBuffer inBuffer) throws IOException, UJciException
  {
    outBuffer = relatedC.outBuffer;
    statementType = GET_SCHEMA_INFO;
    relatedConnection = relatedC;
    className = cName;
    schemaType = type;

    errorHandler = new UError();

    serverHandler = inBuffer.getResCode();
    totalTupleNumber = inBuffer.readInt();
    columnNumber = inBuffer.readInt();
    firstColumnInfo = readColumnInfo(inBuffer);
    columnInfo = firstColumnInfo;

    fetchSize = DEFAULT_FETCH_SIZE;
    currentFirstCursor = cursorPosition = fetchedTupleNumber = 0;
    bindParameter = null;
    batchParameter = null;
    maxFetchSize = 0;
    realFetched = false;
    isUpdatable = false;
    isClosed = false;

    /*
     * 3.0 resultset_index = 0; resultset_index_flag =
     * java.sql.Statement.CLOSE_CURRENT_RESULT;
     */
  }

  public UStatement(UConnection u_con, int srv_handle) throws Exception
  {
    relatedConnection = u_con;
    outBuffer = u_con.outBuffer;
    statementType = NORMAL;
    errorHandler = new UError();
    bindParameter = null;
    fetchSize = DEFAULT_FETCH_SIZE;
    currentFirstCursor = cursorPosition = totalTupleNumber = fetchedTupleNumber = 0;
    maxFetchSize = 0;
    realFetched = false;
    isClosed = false;
    // executeFlag = ASYNC_EXECUTE;

    UInputBuffer inBuffer;
    synchronized (u_con)
    {
      outBuffer.newRequest(UFunctionCode.MAKE_OUT_RS);
      outBuffer.addInt(srv_handle);
      inBuffer = u_con.send_recv_msg();
    }

    serverHandler = inBuffer.readInt();
    commandTypeIs = inBuffer.readByte();
    totalTupleNumber = inBuffer.readInt();
    isUpdatable = (inBuffer.readByte() == 1) ? true : false;
    columnNumber = inBuffer.readInt();
    firstColumnInfo = readColumnInfo(inBuffer);
    columnInfo = firstColumnInfo;

    executeResult = totalTupleNumber;
  }

  public UStatement(UStatement u_stmt)
  {
    serverHandler = u_stmt.serverHandler;
    relatedConnection = u_stmt.relatedConnection;
    outBuffer = u_stmt.outBuffer;
    statementType = NORMAL;
    errorHandler = new UError();
    bindParameter = null;
    fetchSize = DEFAULT_FETCH_SIZE;
    currentFirstCursor = cursorPosition = totalTupleNumber = fetchedTupleNumber = 0;
    maxFetchSize = 0;
    realFetched = false;
    isClosed = false;
  }

  public UResCache getResCache()
  {
    UBindKey key;

    if (bindParameter == null)
      key = new UBindKey(null);
    else
      key = new UBindKey(bindParameter.values);

    return ((UResCache) stmt_cache.get(key));
  }

  public int getParameterCount()
  {
    return parameterNumber;
  }

  public void registerOutParameter(int index)
  {
    errorHandler = new UError();
    if (isClosed == true)
    {
      errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
      return;
    }
    if (index < 0 || index >= parameterNumber)
    {
      errorHandler.setErrorCode(UErrorCode.ER_BIND_INDEX);
      return;
    }
    synchronized (bindParameter)
    {
      try
      {
        bindParameter.setOutParam(index);
      }
      catch (UJciException e)
      {
        e.toUError(errorHandler);
      }
    }
  }

  /*
   * index번째 paramenter에 boolean type value를 set한다. CUBRID은 boolean
   * type을 지원하지 않으므로 bit(1) type으로 match한다. 다만 현재 bit(1) type
   * manage에 문제가 있어 bit(8) type으로 간주한다.
   */

  public void bind(int index, boolean value)
  {
    Byte data = new Byte((value == true) ? TRUE : FALSE);

    bindValue(index, UUType.U_TYPE_BIT, data);
  }

  /*
   * index번째 paramenter에 byte type value를 set한다. byte type value는
   * Database의 tinyint type에 match되어야 하나 CUBRID에서는 tinyint를 지원하지
   * 않으므로 smallint type으로 match한다.
   */

  public void bind(int index, byte value)
  {
    Short data = new Short(value);

    bindValue(index, UUType.U_TYPE_SHORT, data);
  }

  public void bind(int index, short value)
  {
    Short data = new Short(value);

    bindValue(index, UUType.U_TYPE_SHORT, data);
  }

  public void bind(int index, int value)
  {
    Integer data = new Integer(value);

    bindValue(index, UUType.U_TYPE_INT, data);
  }

  public void bind(int index, long value)
  {
    BigDecimal data = new BigDecimal(Long.toString(value));

    bindValue(index, UUType.U_TYPE_NUMERIC, data);
  }

  public void bind(int index, float value)
  {
    Float data = new Float(value);

    bindValue(index, UUType.U_TYPE_FLOAT, data);
  }

  public void bind(int index, double value)
  {
    Double data = new Double(value);

    bindValue(index, UUType.U_TYPE_DOUBLE, data);
  }

  public void bind(int index, BigDecimal value)
  {
    bindValue(index, UUType.U_TYPE_NUMERIC, value);
  }

  public void bind(int index, String value)
  {
    bindValue(index, UUType.U_TYPE_STRING, value);
  }

  /*
   * index번째 paramenter에 byte[] type value를 set한다. CUBRID의 Bit
   * Varying이나 Bit(n)과 match되지만 parameter의 type 정보를 알 수 없으므로 Bit
   * Varying type으로 match시킨다.
   */

  public void bind(int index, byte[] value)
  {
    byte[] data;

    if (value == null)
      data = null;
    else
      data = (byte[]) value.clone();

    bindValue(index, UUType.U_TYPE_VARBIT, data);
  }

  public void bind(int index, Date value)
  {
    bindValue(index, UUType.U_TYPE_DATE, value);
  }

  public void bind(int index, Time value)
  {
    bindValue(index, UUType.U_TYPE_TIME, value);
  }

  public void bind(int index, Timestamp value)
  {
    bindValue(index, UUType.U_TYPE_TIMESTAMP, value);
  }

  public void bind(int index, Object value)
  {
    byte type = UUType.getObjectDBtype(value);

    if (type == UUType.U_TYPE_SEQUENCE)
    {
      bindCollection(index, (Object[]) value);
      return;
    }
    if (type == UUType.U_TYPE_NULL && value != null)
    {
      errorHandler = new UError();
      errorHandler.setErrorCode(UErrorCode.ER_INVALID_ARGUMENT);
      return;
    }

    bindValue(index, type, value);
  }

  public void bindCollection(int index, Object values[])
  {
    CUBRIDArray collectionData;

    if (values == null)
    {
      collectionData = null;
    }
    else
    {
      try
      {
        collectionData = new CUBRIDArray(values);
      }
      catch (UJciException e)
      {
        errorHandler = new UError();
        e.toUError(errorHandler);
        return;
      }
    }

    bindValue(index, UUType.U_TYPE_SEQUENCE, collectionData);
  }

  public void bindOID(int index, CUBRIDOID oid)
  {
    bindValue(index, UUType.U_TYPE_OBJECT, oid);
  }

  public void addBatch()
  {
    errorHandler = new UError();
    if (isClosed == true)
    {
      errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
      return;
    }

    if (bindParameter == null)
      return;

    if (bindParameter.checkAllBinded() == false)
    {
      errorHandler.setErrorCode(UErrorCode.ER_NOT_BIND);
      return;
    }

    if (batchParameter == null)
    {
      batchParameter = new ArrayList();
    }
    batchParameter.add(bindParameter);

    bindParameter = new UBindParameter(parameterNumber);
  }

  public void bindNull(int index)
  {
    bindValue(index, UUType.U_TYPE_NULL, null);
  }

  /*
   * 현재의 UStatement를 cancel한다. 현재의 UStatement가 UConnection object의
   * Lock을 가지고 있으므로 새로운 connection을 만들어야 한다. 이 method에서
   * 만들어진 UConnection object는 cancel이 끝나면 close된다. Error Code :
   * ER_IS_CLOSED, ER_ILLEGAL_DATA_SIZE, ER_CONNECTION
   */

  public UError cancel()
  {
    UError localError = new UError();
    if (isClosed == true)
    {
      localError.setErrorCode(UErrorCode.ER_IS_CLOSED);
      return localError;
    }
    if (statementType == GET_BY_OID)
      return localError;

    try
    {
      relatedConnection.cancel();
    }
    catch (UJciException e)
    {
      e.toUError(localError);
    }
    catch (IOException e)
    {
      localError.setErrorCode(UErrorCode.ER_COMMUNICATION);
    }
    return localError;
  }

  /*
   * PreparedStatement의 batch job을 위한 parameter array를 clear시킨다.
   * clearBatch가 불린 후 parameter array cursor는 1번째 element에 놓이며 어떠한
   * parameter도 binding되어 있지 않게 된다. Error Code : ER_IS_CLOSED
   */

  public void clearBatch()
  {
    errorHandler = new UError();
    if (isClosed == true)
    {
      errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
      return;
    }
    if (batchParameter == null)
      return;
    synchronized (batchParameter)
    {
      batchParameter.clear();
    }
  }

  /*
   * bind된 parameter들을 clear시킨다. Error Code : ER_IS_CLOSED
   */

  synchronized public void clearBind()
  {
    errorHandler = new UError();
    if (isClosed == true)
    {
      errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
      return;
    }
    if (bindParameter == null)
      return;
    synchronized (bindParameter)
    {
      bindParameter.clear();
    }
  }

  /*
   * 현재 Statement를 close한다. 주로 UConnection에서 Transaction을 commit하거나
   * rollback시킬때 call된다. 또한 JCI statement만 close시키는게 아니라 해당되는
   * CAS handler또한 close시킨다. Memory garbage를 정리하기 위해 garbage
   * collector를 call한다. Error Code : ER_IS_CLOSED, ER_ILLEGAL_DATA_SIZE,
   * ER_COMMUNICATION
   */

  synchronized public void close(boolean close_srv_handle)
  {
    try
    {
      errorHandler = new UError();

      if (isClosed == true)
      {
        errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
        return;
      }
      /*
       * closeFirstColumnInfo(); closeInternal();
       */

      relatedConnection.pooled_ustmts.remove(this);

      if (!isReturnable
          && close_srv_handle
          && !(relatedConnection.getAutoCommit() == true && relatedConnection
              .brokerInfoStatementPooling() == false))
      {
        // if (relatedConnection.delayed_close_handle <= 0)
        // relatedConnection.delayed_close_handle = serverHandler;
        // else {
        synchronized (relatedConnection)
        {
          outBuffer.newRequest(UFunctionCode.CLOSE_USTATEMENT);
          // outBuffer.addInt(relatedConnection.delayed_close_handle);
          outBuffer.addInt(serverHandler);

          UInputBuffer inBuffer;
          inBuffer = relatedConnection.send_recv_msg();
        }
        // relatedConnection.delayed_close_handle = serverHandler;
        // }
      }
    }
    catch (UJciException e)
    {
      e.toUError(errorHandler);
      return;
    }
    catch (IOException e)
    {
      errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
      return;
    }
    finally
    {
      currentFirstCursor = cursorPosition = totalTupleNumber = fetchedTupleNumber = 0;
      isClosed = true;
      if (stmt_cache != null)
        stmt_cache.decr_ref_count();
    }
  }

  synchronized public void close()
  {
    close(true);
  }

  /*
   * cursor가 가르키는 tuple이 OID값을 가지고 있을 경우 그 oid를 가지는 object가
   * db에 존재하는지를 결정해준다. Cursor가 가르키는 tuple이 OID값을 가지지 않을
   * 경우 ER_OID_IS_NOT_INCLUDED error가 set된다. Error Code : ER_IS_CLOSED,
   * ER_INVALID_ARGUMENT, ER_ILLEGAL_DATA_SIZE, ER_COMMUNICATION,
   * ER_OID_IS_NOT_INCLUDED
   */

  synchronized public boolean cursorIsInstance(int cursor)
  {
    errorHandler = new UError();
    if (isClosed == true)
    {
      errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
      return false;
    }
    if ((currentFirstCursor < 0)
        || (cursor < 0)
        || (cursor >= 0 && ((cursor < currentFirstCursor) || (cursor > currentFirstCursor
            + fetchedTupleNumber - 1))))
    {
      errorHandler.setErrorCode(UErrorCode.ER_INVALID_ARGUMENT);
      return false;
    }
    if (tuples[cursorPosition - currentFirstCursor].oidIsIncluded() == false)
    {
      errorHandler.setErrorCode(UErrorCode.ER_OID_IS_NOT_INCLUDED);
      return false;
    }

    Object instance_obj;

    synchronized (relatedConnection)
    {
      instance_obj = relatedConnection.oidCmd(tuples[cursorPosition
          - currentFirstCursor].getOid(), UConnection.IS_INSTANCE);
    }
    errorHandler.copyValue(relatedConnection.getRecentError());
    if (instance_obj == null)
      return false;
    else
      return true;
  }

  /*
   * cursor가 가르키는 tuple이 OID값을 가지고 있을 경우 그 oid를 가지는 object를
   * db에서 delete한다. Cursor가 가르키는 tuple이 OID값을 가지지 않을 경우
   * ER_OID_IS_NOT_INCLUDED error가 set된다. Error Code : ER_IS_CLOSED,
   * ER_INVALID_ARGUMENT, ER_OID_IS_NOT_INCLUDED, ER_ILLEGAL_DATA_SIZE,
   * ER_COMMUNICATION
   */

  synchronized public void deleteCursor(int cursor)
  {
    errorHandler = new UError();
    if (isClosed == true)
    {
      errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
      return;
    }
    if ((currentFirstCursor < 0)
        || (cursor < 0)
        || (cursor >= 0 && ((cursor < currentFirstCursor) || (cursor > currentFirstCursor
            + fetchedTupleNumber - 1))))
    {
      errorHandler.setErrorCode(UErrorCode.ER_INVALID_ARGUMENT);
      return;
    }
    if (tuples[cursorPosition - currentFirstCursor].oidIsIncluded() == false)
    {
      errorHandler.setErrorCode(UErrorCode.ER_OID_IS_NOT_INCLUDED);
      return;
    }

    synchronized (relatedConnection)
    {
      relatedConnection.oidCmd(tuples[cursorPosition - currentFirstCursor]
          .getOid(), UConnection.DROP_BY_OID);
    }
    errorHandler.copyValue(relatedConnection.getRecentError());
  }

  /*
   * Bind된 UStatement를 실행한다. Prepare한 statement가 'q1;q2;…;qn'와 같을
   * 경우 allExecute가 true이면 q1, q2, …, qn의 모든 statement가 execute되며
   * false일 경우 q1만 실행된다. 두 경우 모두 모든 parameter가 binding되어야
   * 한다. 그리고 allExecute가 true일 경우 isAsync는 언제나 false로 간주된다.
   * isSensitive가 true일 경우 sensitive하게 ResultSet을 fetch한다. Error Code :
   * ER_IS_CLOSED, ER_NOT_BIND, ER_INVALID_ARGUMENT, ER_ILLEGAL_DATA_SIZE,
   * ER_COMMUNICATION, ER_TYPE_CONVERSION
   */

  synchronized public void execute(boolean isAsync, int maxRow, int maxField,
      boolean allExecute, boolean is_sensitive, boolean is_scrollable,
      boolean query_plan_flag, boolean only_query_plan,
      UStatementCacheData cache_data)
  {
    UInputBuffer inBuffer = null;
    errorHandler = new UError();
    boolean retry_flag = true;

    if (isClosed == true)
    {
      if (relatedConnection.brokerInfoStatementPooling() == true)
      {
        retry_flag = false;
        UStatement tmp_ustmt = relatedConnection.prepare(sql_stmt,
            prepare_flag, true);
        relatedConnection.pooled_ustmts.remove(tmp_ustmt);
        relatedConnection.pooled_ustmts.add(this);

        UError tmp_err = relatedConnection.getRecentError();
        if (tmp_err.getErrorCode() != UErrorCode.ER_NO_ERROR)
        {
          errorHandler.copyValue(tmp_err);
          return;
        }

        try
        {
          init(relatedConnection, tmp_ustmt.tmp_inbuffer, sql_stmt,
              prepare_flag, false);
        }
        catch (UJciException e)
        {
          e.toUError(errorHandler);
          return;
        }
        catch (IOException e)
        {
          if (errorHandler.getErrorCode() != UErrorCode.ER_CONNECTION)
            errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
          return;
        }
      }
      else
      {
        errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
        return;
      }
    }
    if (statementType == GET_SCHEMA_INFO)
      return;
    if (bindParameter != null && bindParameter.checkAllBinded() == false)
    {
      errorHandler.setErrorCode(UErrorCode.ER_NOT_BIND);
      return;
    }
    executeFlag = 0;
    if (allExecute)
      executeFlag |= EXEC_FLAG_QUERY_ALL;
    if (isAsync)
      executeFlag |= EXEC_FLAG_ASYNC;
    if (query_plan_flag)
      executeFlag |= EXEC_FLAG_QUERY_INFO;
    if (only_query_plan)
      executeFlag |= EXEC_FLAG_ONLY_QUERY_PLAN;

    currentFirstCursor = -1;
    fetchedTupleNumber = 0;

    maxFetchSize = maxRow;

    try
    {
      synchronized (relatedConnection)
      {
        if (relatedConnection.brokerInfoStatementPooling() == true)
        {
          relatedConnection.skip_checkcas = true;
          relatedConnection.need_checkcas = false;
          relatedConnection.checkReconnect();
        }

        outBuffer.newRequest(UFunctionCode.EXECUTE);
        outBuffer.addInt(serverHandler);
        outBuffer.addByte(executeFlag);
        outBuffer.addInt(((maxField < 0) ? 0 : maxField));
        // jci 3.0
        outBuffer.addInt(0);
        // outBuffer.addInt((maxFetchSize < 0) ? 0 : maxFetchSize);
        // jci 3.3
        if (commandTypeIs == CUBRIDCommandType.CUBRID_STMT_CALL_SP
            && bindParameter != null)
        {
          outBuffer.addBytes(bindParameter.paramMode);
        }
        else
        {
          outBuffer.addNull();
        }
        /* fetch flag */
        if (commandTypeIs == CUBRIDCommandType.CUBRID_STMT_SELECT)
          outBuffer.addByte((byte) 1);
        else
          outBuffer.addByte((byte) 0);

        outBuffer.addByte(relatedConnection.getAutoCommit() && (!isGeneratedKeys) ? (byte) 1
            : (byte) 0);
        outBuffer.addByte(!is_scrollable ? (byte) 1 : (byte) 0);

        /* cache info - jci 3.7 */
        outBuffer.addCacheTime(cache_data);

        if (bindParameter != null)
        {
          synchronized (bindParameter)
          {
            bindParameter.writeParameter(outBuffer);
          }
        }

        inBuffer = relatedConnection.send_recv_msg();
      }

      byte cache_reusable = inBuffer.readByte();
      if (cache_data != null && cache_reusable == (byte) 1)
      {
        setCacheData(cache_data);
        return;
      }

      readResultInfo(inBuffer);
    }
    catch (UJciException e)
    {
      e.toUError(errorHandler);
    }
    catch (IOException e)
    {
      errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
    }
    finally
    {
      relatedConnection.skip_checkcas = false;
    }

    if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
    {
      if (relatedConnection.brokerInfoStatementPooling() == true
          && retry_flag == true)
      {
        if (errorHandler.getJdbcErrorCode() == UErrorCode.CAS_ER_STMT_POOLING)
        {
          close();
        }
        else if (relatedConnection.need_checkcas
            && relatedConnection.check_cas() == false)
        {
          try
          {
            relatedConnection.clientSocketClose();
          }
          catch (Exception e)
          {
          }
        }
        else
        {
          return;
          // close();
        }
        errorHandler.clear();
        execute(isAsync, maxRow, maxField, allExecute, is_sensitive,
            is_scrollable, query_plan_flag, only_query_plan, cache_data);
        return;
      }
      else
      {
        return;
      }
    }

    columnInfo = firstColumnInfo;
    cursorPosition = -1;
    if (commandTypeIs == CUBRIDCommandType.CUBRID_STMT_CALL_SP)
      cursorPosition = 0;

    executeResult = inBuffer.getResCode();
    errorHandler.setErrorCode(UErrorCode.ER_NO_ERROR);

    /* set max resultset row size */
    totalTupleNumber = executeResult = ((maxFetchSize > 0) && (executeResult > maxFetchSize)) ? maxFetchSize
        : executeResult;
    batchParameter = null;
    isSensitive = is_sensitive;

    try
    {
      if (commandTypeIs == CUBRIDCommandType.CUBRID_STMT_SELECT
          && totalTupleNumber > 0)
      {
        int fetch_rescode = inBuffer.readInt();
        read_fetch_data(inBuffer);
      }
    }
    catch (IOException e)
    {
      errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
    }
    catch (UJciException e)
    {
      e.toUError(errorHandler);
    }

    for (int i = 0; i < resultInfo.length; i++)
    {
      if (resultInfo[i].statementType != CUBRIDCommandType.CUBRID_STMT_SELECT)
      {
        relatedConnection.update_executed = true;
        break;
      }
    }

    if (cache_data != null && fetchedTupleNumber == totalTupleNumber
        && resultInfo.length == 1)
    {
      cache_data.setCacheData(totalTupleNumber, tuples, resultInfo);
    }
    else if (resultInfo.length > 1)
    {
      result_cacheable = false;
    }
  }

  synchronized public CUBRIDOID executeInsert(boolean isAsync)
  {
    errorHandler = new UError();
    if (isClosed == true)
    {
      errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
      return null;
    }
    if (commandTypeIs != CUBRIDCommandType.CUBRID_STMT_INSERT)
    {
      errorHandler.setErrorCode(UErrorCode.ER_CMD_IS_NOT_INSERT);
      return null;
    }
    execute(isAsync, 0, 0, false, false, false, false, false, null);
    if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
      return null;
    if (resultInfo != null && resultInfo[0] != null)
      return resultInfo[0].getCUBRIDOID();
    errorHandler.setErrorCode(UErrorCode.ER_OID_IS_NOT_INCLUDED);
    return null;
  }

  synchronized public void setAutoCommit(boolean autoCommit)
  {
    isAutoCommit = autoCommit;
  }

  /*
   * PreparedStatement의 batch execute를 위한 interface이다. Binding된 parameter
   * list를 batch execute하며 각 statement의 결과는 integer array로 return되어
   * 진다. result value는 각 statement가 success한 경우 result count(0이상)를
   * 나타내고 fail인 경우 -3값을 가진다. Prepare된 Statement가 "q1;…;qn"와 같을
   * 때 단지 q1 statement만이 execute된다. Error Code ER_IS_CLOSED, ER_NOT_BIND,
   * ER_ILLEGAL_DATA_SIZE, ER_TYPE_CONVERSION ER_COMMUNICATION
   */

  synchronized public UBatchResult executeBatch()
  {
    UInputBuffer inBuffer;

    errorHandler = new UError();
    try
    {
      synchronized (relatedConnection)
      {
        relatedConnection.checkReconnect();
        if (isClosed == true)
        {
          if (relatedConnection.brokerInfoStatementPooling() == true)
          {
            UStatement tmp_ustmt = relatedConnection.prepare(sql_stmt,
                prepare_flag, true);
            relatedConnection.pooled_ustmts.remove(tmp_ustmt);
            relatedConnection.pooled_ustmts.add(this);

            UError tmp_err = relatedConnection.getRecentError();
            if (tmp_err.getErrorCode() != UErrorCode.ER_NO_ERROR)
            {
              errorHandler.copyValue(tmp_err);
              return null;
            }

            try
            {
              init(relatedConnection, tmp_ustmt.tmp_inbuffer, sql_stmt,
                  prepare_flag, false);
            }
            catch (UJciException e)
            {
              e.toUError(errorHandler);
              return null;
            }
            catch (IOException e)
            {
              if (errorHandler.getErrorCode() != UErrorCode.ER_CONNECTION)
                errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
              return null;
            }
          }
          else
          {
            errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
            return null;
          }
        }

        outBuffer.newRequest(relatedConnection.getOutputStream(),
            UFunctionCode.EXECUTE_BATCH_PREPAREDSTATEMENT);
        outBuffer.addInt(serverHandler);
        outBuffer.addByte(isAutoCommit ? (byte) 1 : (byte) 0);

        if (batchParameter != null)
        {
          synchronized (batchParameter)
          {
            for (int i = 0; i < batchParameter.size(); i++)
            {
              UBindParameter b = (UBindParameter) batchParameter.get(i);
              b.writeParameter(outBuffer);
            }
          }
        }

        inBuffer = relatedConnection.send_recv_msg();
      }

      batchParameter = null;
      UBatchResult batchResult;
      int result;

      batchResult = new UBatchResult(inBuffer.readInt());
      for (int i = 0; i < batchResult.getResultNumber(); i++)
      {
        batchResult.setStatementType(i, statementType);
        result = inBuffer.readInt();
        if (result < 0)
          batchResult.setResultError(i, result, inBuffer.readString(inBuffer
              .readInt(), UJCIManager.sysCharsetName));
        else
        {
          batchResult.setResult(i, result);
          // jci 3.0
          inBuffer.readInt();
          inBuffer.readShort();
          inBuffer.readShort();
        }
      }
      return batchResult;
    }
    catch (UJciException e)
    {
      e.toUError(errorHandler);
      return null;
    }
    catch (IOException e)
    {
      if (errorHandler.getErrorCode() != UErrorCode.ER_CONNECTION)
        errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
      return null;
    }
  }

  /*
   * UStatement를 execute하여 얻어진 ResultSet을 CAS로부터 fetch한다. getByOid에
   * 의해 만들어진 UStatement는 UStatement의 constructor에서 이미 ResultSet을
   * fetch하므로 바로 return한다. Error Code ER_IS_CLOSED, ER_NO_MORE_DATA,
   * ER_ILLEGAL_DATA_SIZE, ER_INVALID_ARGUMENT, ER_COMMUNICATION
   */

  synchronized public void fetch()
  {
    errorHandler = new UError();
    if (isClosed == true)
    {
      errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
      return;
    }
    realFetched = false;

    /* need not to fetch */
    if (statementType == GET_BY_OID)
      return;

    if (cursorPosition < 0
        || (executeFlag != ASYNC_EXECUTE && totalTupleNumber <= 0))
    {
      errorHandler.setErrorCode(UErrorCode.ER_NO_MORE_DATA);
      return;
    }

    /* need not to fetch really */
    if (currentFirstCursor >= 0 && currentFirstCursor <= cursorPosition
        && cursorPosition <= currentFirstCursor + fetchedTupleNumber - 1)
    {
      return;
    }

    reFetch();
  }

  /*
   * ResultSet의 현재 Cursor가 위치한 Tuple의 index번째 data를 BigDecimal
   * Type으로 return한다. Type conversion이 불가한 Type match인 경우 error를
   * set한다. Error Code ER_IS_CLOSED, ER_COLUMN_INDEX, ER_NO_MORE_DATA,
   * ER_WAS_NULL, ER_TYPE_CONVERSION
   */

  synchronized public BigDecimal getBigDecimal(int index)
  {
    errorHandler = new UError();

    Object obj = beforeGetXXX(index);
    if (obj == null)
      return null;

    try
    {
      return (UGetTypeConvertedValue.getBigDecimal(obj));
    }
    catch (UJciException e)
    {
      e.toUError(errorHandler);
    }
    return null;
  }

  /*
   * ResultSet의 현재 Cursor가 위치한 Tuple의 index번째 data를 boolean Type으로
   * return한다. Type conversion이 불가한 Type match인 경우 error를 set한다.
   * Error Code ER_IS_CLOSED, ER_COLUMN_INDEX, ER_NO_MORE_DATA, ER_WAS_NULL,
   * ER_TYPE_CONVERSION
   */

  synchronized public boolean getBoolean(int index)
  {
    errorHandler = new UError();

    Object obj = beforeGetXXX(index);
    if (obj == null)
      return false;

    try
    {
      return (UGetTypeConvertedValue.getBoolean(obj));
    }
    catch (UJciException e)
    {
      e.toUError(errorHandler);
    }
    return false;
  }

  /*
   * ResultSet의 현재 Cursor가 위치한 Tuple의 index번째 data를 byte Type으로
   * return한다. Type conversion이 불가한 Type match인 경우 error를 set한다.
   * Error Code : ER_IS_CLOSED, ER_COLUMN_INDEX, ER_NO_MORE_DATA, ER_WAS_NULL,
   * ER_TYPE_CONVERSION
   */

  synchronized public byte getByte(int index)
  {
    errorHandler = new UError();

    Object obj = beforeGetXXX(index);
    if (obj == null)
      return ((byte) 0);

    try
    {
      return (UGetTypeConvertedValue.getByte(obj));
    }
    catch (UJciException e)
    {
      e.toUError(errorHandler);
    }
    return ((byte) 0);
  }

  /*
   * ResultSet의 현재 Cursor가 위치한 Tuple의 index번째 data를 byte [] Type으로
   * return한다. Type conversion이 불가한 Type match인 경우 error를 set한다.
   * Error Code : ER_IS_CLOSED, ER_COLUMN_INDEX, ER_NO_MORE_DATA, ER_WAS_NULL,
   * ER_TYPE_CONVERSION
   */

  synchronized public byte[] getBytes(int index)
  {
    errorHandler = new UError();

    Object obj = beforeGetXXX(index);
    if (obj == null)
      return null;

    try
    {
      return (UGetTypeConvertedValue.getBytes(obj));
    }
    catch (UJciException e)
    {
      e.toUError(errorHandler);
    }
    return null;
  }

  /*
   * ResultSet의 현재 Cursor가 위치한 Tuple의 index번째 data를 [] Type으로
   * return한다. Type conversion이 불가한 Type match인 경우 error를 set한다.
   * Error Code : ER_IS_CLOSED, ER_COLUMN_INDEX, ER_NO_MORE_DATA, ER_WAS_NULL,
   * ER_TYPE_CONVERSION
   */

  synchronized public Object getCollection(int index)
  {
    errorHandler = new UError();

    Object obj = beforeGetXXX(index);
    if (obj == null)
      return null;

    if (!(obj instanceof CUBRIDArray))
    {
      errorHandler.setErrorCode(UErrorCode.ER_TYPE_CONVERSION);
      return null;
    }

    return (((CUBRIDArray) obj).getArrayClone());
  }

  /*
   * UStatement를 prepare한 뒤 얻어지는 ResultSet의 Column 정보를 가지고 있는
   * UColumnInfo array를 return한다. Error Code : ER_IS_CLOSED
   */

  public UColumnInfo[] getColumnInfo()
  {
    UError localError;

    localError = new UError();
    if (isClosed == true)
    {
      localError.setErrorCode(UErrorCode.ER_IS_CLOSED);
      errorHandler = localError;
      return null;
    }
    errorHandler = localError;
    return columnInfo;
  }

  /*
   * ResultSet의 현재 Cursor가 위치한 Tuple의 index번째 data를 CUBRIDOID
   * Type으로 return한다. column value가 object instance인 경우 oid값만
   * server쪽에서 넘어오는데 이 oid값을 얻을 때 사용되어진다. 그 외의 경우 type
   * conversion error를 set한다. Error Code : ER_IS_CLOSED, ER_COLUMN_INDEX,
   * ER_NO_MORE_DATA, ER_WAS_NULL, ER_NOT_OBJECT ER_TYPE_CONVERSION
   */

  synchronized public CUBRIDOID getColumnOID(int index)
  {
    errorHandler = new UError();

    Object obj = beforeGetXXX(index);
    if (obj == null)
      return null;

    if (!(obj instanceof CUBRIDOID))
    {
      errorHandler.setErrorCode(UErrorCode.ER_NOT_OBJECT);
      return null;
    }

    return ((CUBRIDOID) obj);
  }

  synchronized public CUBRIDOID getCursorOID()
  {
    errorHandler = new UError();
    if (isClosed == true)
    {
      errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
      return null;
    }

    if (checkReFetch() != true)
      return null;

    return (tuples[cursorPosition - currentFirstCursor].getOid());
  }

  synchronized public CUBRIDOID getGloOID(int index)
  {
    errorHandler = new UError();

    Object obj = beforeGetXXX(index);
    if (obj == null)
      return null;

    if (obj instanceof CUBRIDOID)
    {
      if (columnInfo != null && columnInfo.length > index)
      {
        if (columnInfo[index].isGloInstance(relatedConnection, (CUBRIDOID) obj))
          return ((CUBRIDOID) obj);
      }
      else
      {
        synchronized (relatedConnection)
        {
          obj = relatedConnection.oidCmd((CUBRIDOID) obj,
              UConnection.IS_GLO_INSTANCE);
        }
        if (obj != null)
          return ((CUBRIDOID) obj);
      }
    }

    errorHandler.setErrorCode(UErrorCode.ER_TYPE_CONVERSION);
    return null;
  }

  /*
   * ResultSet의 현재 Cursor가 위치한 Tuple의 index번째 data를 Date Type으로
   * return한다. Type conversion이 불가한 Type match인 경우 error를 set한다.
   * Error Code : ER_IS_CLOSED, ER_COLUMN_INDEX, ER_NO_MORE_DATA, ER_WAS_NULL,
   * ER_TYPE_CONVERSION
   */

  synchronized public Date getDate(int index)
  {
    errorHandler = new UError();

    Object obj = beforeGetXXX(index);
    if (obj == null)
      return null;

    try
    {
      return (UGetTypeConvertedValue.getDate(obj));
    }
    catch (UJciException e)
    {
      e.toUError(errorHandler);
    }
    return null;
  }

  /*
   * ResultSet의 현재 Cursor가 위치한 Tuple의 index번째 data를 double Type으로
   * return한다. Type conversion이 불가한 Type match인 경우 error를 set한다.
   * Error Code : ER_IS_CLOSED, ER_COLUMN_INDEX, ER_NO_MORE_DATA, ER_WAS_NULL,
   * ER_TYPE_CONVERSION
   */

  synchronized public double getDouble(int index)
  {
    errorHandler = new UError();

    Object obj = beforeGetXXX(index);
    if (obj == null)
      return ((double) 0);

    try
    {
      return (UGetTypeConvertedValue.getDouble(obj));
    }
    catch (UJciException e)
    {
      e.toUError(errorHandler);
    }
    return ((double) 0);
  }

  /*
   * Execute한 statement의 result count를 return한다. Error Code : ER_IS_CLOSED
   */

  public int getExecuteResult()
  {
    UError localError = new UError();
    if (isClosed == true)
    {
      localError.setErrorCode(UErrorCode.ER_IS_CLOSED);
      errorHandler = localError;
      return 0;
    }
    errorHandler = localError;
    return executeResult;
  }

  /*
   * ResultSet의 fetch direction을 알려준다. Error Code : ER_IS_CLOSED
   */

  public int getFetchDirection()
  {
    UError localError = new UError();
    if (isClosed == true)
    {
      localError.setErrorCode(UErrorCode.ER_IS_CLOSED);
      errorHandler = localError;
      return 0;
    }
    errorHandler = localError;
    return fetchDirection;
  }

  /*
   * ResultSet의 fetch size를 알려준다. Error Code : ER_IS_CLOSED
   */

  public int getFetchSize()
  {
    UError localError = new UError();
    if (isClosed == true)
    {
      localError.setErrorCode(UErrorCode.ER_IS_CLOSED);
      errorHandler = localError;
      return 0;
    }
    errorHandler = localError;
    return fetchSize;
  }

  /*
   * ResultSet의 현재 Cursor가 위치한 Tuple의 index번째 data를 float Type으로
   * return한다. Type conversion이 불가한 Type match인 경우 error를 set한다.
   * Error Code : ER_IS_CLOSED, ER_COLUMN_INDEX, ER_NO_MORE_DATA, ER_WAS_NULL,
   * ER_TYPE_CONVERSION
   */

  synchronized public float getFloat(int index)
  {
    errorHandler = new UError();

    Object obj = beforeGetXXX(index);
    if (obj == null)
      return ((float) 0);

    try
    {
      return (UGetTypeConvertedValue.getFloat(obj));
    }
    catch (UJciException e)
    {
      e.toUError(errorHandler);
    }
    return ((float) 0);
  }

  /*
   * ResultSet의 현재 Cursor가 위치한 Tuple의 index번째 data를 int Type으로
   * return한다. Type conversion이 불가한 Type match인 경우 error를 set한다.
   * Error Code : ER_IS_CLOSED, ER_COLUMN_INDEX, ER_NO_MORE_DATA, ER_WAS_NULL,
   * ER_TYPE_CONVERSION
   */

  synchronized public int getInt(int index)
  {
    errorHandler = new UError();

    Object obj = beforeGetXXX(index);
    if (obj == null)
      return 0;

    try
    {
      return (UGetTypeConvertedValue.getInt(obj));
    }
    catch (UJciException e)
    {
      e.toUError(errorHandler);
    }

    return 0;
  }

  /*
   * ResultSet의 현재 Cursor가 위치한 Tuple의 index번째 data를 long Type으로
   * return한다. Type conversion이 불가한 Type match인 경우 error를 set한다.
   * Error Code : ER_IS_CLOSED, ER_COLUMN_INDEX, ER_NO_MORE_DATA, ER_WAS_NULL,
   * ER_TYPE_CONVERSION
   */

  synchronized public long getLong(int index)
  {
    errorHandler = new UError();

    Object obj = beforeGetXXX(index);
    if (obj == null)
      return ((long) 0);

    try
    {
      return (UGetTypeConvertedValue.getLong(obj));
    }
    catch (UJciException e)
    {
      e.toUError(errorHandler);
    }

    return ((long) 0);
  }

  /*
   * ResultSet의 현재 Cursor가 위치한 Tuple의 index번째 data를 Object Type으로
   * return한다. type이 UUType.U_TYPE_BIT인 경우 java의 boolean위해 만들어진
   * type으로 간주하고 Boolean object로 바꾸어준다. Error Code : ER_IS_CLOSED,
   * ER_COLUMN_INDEX, ER_NO_MORE_DATA, ER_WAS_NULL
   */

  synchronized public Object getObject(int index)
  {
    errorHandler = new UError();

    Object obj = beforeGetXXX(index);
    if (obj == null)
      return null;

    Object retValue;
    try
    {
      if ((commandTypeIs != CUBRIDCommandType.CUBRID_STMT_CALL_SP)
          && (columnInfo[index].getColumnType() == UUType.U_TYPE_BIT)
          && (columnInfo[index].getColumnPrecision() == 8))
      {
        retValue = new Boolean(UGetTypeConvertedValue.getBoolean(obj));
      }
      else if (obj instanceof CUBRIDArray)
        retValue = ((CUBRIDArray) obj).getArrayClone();
      else if (obj instanceof byte[])
        retValue = ((byte[]) obj).clone();
      else if (obj instanceof Date)
        retValue = ((Date) obj).clone();
      else if (obj instanceof Time)
        retValue = ((Time) obj).clone();
      else if (obj instanceof Timestamp)
        retValue = ((Timestamp) obj).clone();
      else if (obj instanceof CUBRIDOutResultSet)
      {
        try
        {
          ((CUBRIDOutResultSet) obj).createInstance();
          retValue = obj;
        }
        catch (Exception e)
        {
          retValue = null;
        }
      }
      else
        retValue = obj;
    }
    catch (UJciException e)
    {
      e.toUError(errorHandler);
      return null;
    }

    return retValue;
  }

  /*
   * class UStatement의 가장 최근에 불려진 public interface에서 발생한 error
   * 값을 얻는 method이다.
   */

  public UError getRecentError()
  {
    return errorHandler;
  }

  /*
   * Statement를 execute후 얻어지는 Result의 정보를 관리하는 UResultInfo array를
   * return한다. 실행되는 statement의 개수만큼의 length를 갖는 UResultInfo
   * array가 return된다. 반드시 UStatement.execute()가 선행된 후 call되어야 하며
   * 그렇지 않은 경우 null을 return한다. Error Code : ER_IS_CLOSED
   */

  public UResultInfo[] getResultInfo()
  {
    UError localError;

    localError = new UError();
    if (isClosed == true)
    {
      localError.setErrorCode(UErrorCode.ER_IS_CLOSED);
      errorHandler = localError;
      return null;
    }
    errorHandler = localError;
    return resultInfo;
  }

  /*
   * ResultSet의 현재 Cursor가 위치한 Tuple의 index번째 data를 short Type으로
   * return한다. Type conversion이 불가한 Type match인 경우 error를 set한다.
   * Error Code : ER_IS_CLOSED, ER_COLUMN_INDEX, ER_NO_MORE_DATA, ER_WAS_NULL,
   * ER_TYPE_CONVERSION
   */

  synchronized public short getShort(int index)
  {
    errorHandler = new UError();

    Object obj = beforeGetXXX(index);
    if (obj == null)
      return ((short) 0);

    try
    {
      return (UGetTypeConvertedValue.getShort(obj));
    }
    catch (UJciException e)
    {
      e.toUError(errorHandler);
    }
    return ((short) 0);
  }

  /*
   * UStatement가 query문이면 true를, 그렇지 않으면 false를 return한다. Error
   * Code : ER_IS_CLOSED
   */

  public boolean getSqlType()
  {
    UError localError = new UError();
    if (isClosed == true)
    {
      localError.setErrorCode(UErrorCode.ER_IS_CLOSED);
      errorHandler = localError;
      return false;
    }
    if ((commandTypeIs == CUBRIDCommandType.CUBRID_STMT_SELECT)
        || (commandTypeIs == CUBRIDCommandType.CUBRID_STMT_CALL)
        || (commandTypeIs == CUBRIDCommandType.CUBRID_STMT_GET_STATS)
        || (commandTypeIs == CUBRIDCommandType.CUBRID_STMT_EVALUATE))
    {
      errorHandler = localError;
      return true;
    }
    else
    {
      errorHandler = localError;
      return false;
    }
  }

  /*
   * ResultSet의 현재 Cursor가 위치한 Tuple의 index번째 data를 String Type으로
   * return한다. Type conversion이 불가한 Type match인 경우 error를 set한다.
   * Error Code : ER_IS_CLOSED, ER_COLUMN_INDEX, ER_NO_MORE_DATA, ER_WAS_NULL,
   * ER_TYPE_CONVERSION
   */

  synchronized public String getString(int index)
  {
    errorHandler = new UError();

    Object obj = beforeGetXXX(index);
    if (obj == null)
      return null;

    try
    {
      return (UGetTypeConvertedValue.getString(obj));
    }
    catch (UJciException e)
    {
      e.toUError(errorHandler);
    }
    return null;
  }

  /*
   * ResultSet의 현재 Cursor가 위치한 Tuple의 index번째 data를 Time Type으로
   * return한다. Type conversion이 불가한 Type match인 경우 error를 set한다.
   * Error Code : ER_IS_CLOSED, ER_COLUMN_INDEX, ER_NO_MORE_DATA, ER_WAS_NULL,
   * ER_TYPE_CONVERSION
   */

  synchronized public Time getTime(int index)
  {
    errorHandler = new UError();

    Object obj = beforeGetXXX(index);
    if (obj == null)
      return null;

    try
    {
      return (UGetTypeConvertedValue.getTime(obj));
    }
    catch (UJciException e)
    {
      e.toUError(errorHandler);
    }
    return null;
  }

  /*
   * ResultSet의 현재 Cursor가 위치한 Tuple의 index번째 data를 Timestamp
   * Type으로 return한다. Type conversion이 불가한 Type match인 경우 error를
   * set한다. Error Code : ER_IS_CLOSED, ER_COLUMN_INDEX, ER_NO_MORE_DATA,
   * ER_WAS_NULL, ER_TYPE_CONVERSION
   */

  synchronized public Timestamp getTimestamp(int index)
  {
    errorHandler = new UError();

    Object obj = beforeGetXXX(index);
    if (obj == null)
      return null;

    try
    {
      return (UGetTypeConvertedValue.getTimestamp(obj));
    }
    catch (UJciException e)
    {
      e.toUError(errorHandler);
    }
    return null;
  }

  /* UStatement가 close되었는지를 return해 준다. */

  public boolean isClosed()
  {
    return isClosed;
  }

  /*
   * 현재 statement가 updatable인지 판단하기 위한 interface이다. prepare시
   * isUpdatable이 true, statement type이 select이고 result set에 OID가
   * include되어져 있을 경우 true로 return되며 그렇지 않은 경우 false로
   * return된다. Error Code : ER_IS_CLOSED
   */

  public boolean isOIDIncluded()
  {
    UError localError = new UError();
    if (isClosed == true)
    {
      localError.setErrorCode(UErrorCode.ER_IS_CLOSED);
      errorHandler = localError;
      return false;
    }
    errorHandler = localError;
    return isUpdatable;
  }

  /*
   * cursor의 위치를 옮긴다. synchronous query의 경우 JCI내부에서만 cursor의
   * 위치를 움직이며 asynchronous query의 경우 cursor가 현재까지 fetch된
   * tuple내로 움직이고자 하면 JCI내부에서 move하지만 fetch된 tuple의 범위를
   * 벗어난 곳으로 move하고자 하면 CAS의 CURSOR function을 call하여 cursor의
   * move가능을 check한다. Error Code : ER_IS_CLOSED, ER_NO_MORE_DATA,
   * ER_ILLEGAL_DATA_SIZE, ER_COMMUNICATION
   */

  synchronized public void moveCursor(int offset, int origin)
  {
    UInputBuffer inBuffer;

    errorHandler = new UError();
    if (isClosed == true)
    {
      errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
      return;
    }
    if ((origin != CURSOR_SET && origin != CURSOR_CUR && origin != CURSOR_END)
        || (executeFlag != ASYNC_EXECUTE && totalTupleNumber == 0))
    {
      errorHandler.setErrorCode(UErrorCode.ER_NO_MORE_DATA);
      return;
    }

    int currentCursor = cursorPosition;

    if (origin == CURSOR_SET)
      cursorPosition = offset;
    else if (origin == CURSOR_CUR)
      cursorPosition += offset;
    if (origin == CURSOR_SET || origin == CURSOR_CUR)
    {
      if (executeFlag == ASYNC_EXECUTE)
      {
        if ((cursorPosition <= currentFirstCursor + fetchedTupleNumber - 1)
            || (totalTupleNumber != 0 && cursorPosition < totalTupleNumber))
          return;
      }
      else if (cursorPosition < totalTupleNumber)
        return;
      else
      {
        errorHandler.setErrorCode(UErrorCode.ER_NO_MORE_DATA);
        cursorPosition = currentCursor;
        return;
      }
    }
    if (origin == CURSOR_END && totalTupleNumber != 0)
    {
      cursorPosition = totalTupleNumber - offset - 1;
      if (cursorPosition >= 0)
        return;
      else
      {
        errorHandler.setErrorCode(UErrorCode.ER_NO_MORE_DATA);
        cursorPosition = currentCursor;
        return;
      }
    }
    if (origin == CURSOR_CUR)
    {
      origin = CURSOR_SET;
      offset += currentCursor;
    }
    try
    {
      synchronized (relatedConnection)
      {
        outBuffer.newRequest(UFunctionCode.CURSOR);
        outBuffer.addInt(serverHandler);
        outBuffer.addInt(offset);
        outBuffer.addInt(origin);

        inBuffer = relatedConnection.send_recv_msg();
      }

      totalTupleNumber = inBuffer.readInt();
    }
    catch (UJciException e)
    {
      cursorPosition = currentCursor;
      e.toUError(errorHandler);
      return;
    }
    catch (IOException e)
    {
      cursorPosition = currentCursor;
      errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
      return;
    }

    if (totalTupleNumber < 0)
    {
      totalTupleNumber = 0;
    }
    else if (totalTupleNumber <= cursorPosition)
    {
      errorHandler.setErrorCode(UErrorCode.ER_NO_MORE_DATA);
      cursorPosition = currentCursor;
    }
  }

  /*
   * Next Result가 존재하는지를 확인해준다. Next result가 존재하지 않을 경우
   * ER_NO_MORE_RESULT error를 set한다. Error Code : ER_IS_CLOSED,
   * ER_NO_MORE_RESULT, ER_ILLEGAL_DATA_SIZE, ER_COMMUNICATION
   */

  synchronized public boolean nextResult()
  {
    UInputBuffer inBuffer;

    errorHandler = new UError();
    if (isClosed == true)
    {
      errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
      return false;
    }
    try
    {
      synchronized (relatedConnection)
      {
        outBuffer.newRequest(UFunctionCode.NEXT_RESULT);
        outBuffer.addInt(serverHandler);
        // jci 3.0
        outBuffer.addInt(0);

        inBuffer = relatedConnection.send_recv_msg();
      }

      closeInternal();
      executeResult = inBuffer.readInt();
      commandTypeIs = inBuffer.readByte();
      isUpdatable = (inBuffer.readByte() == 1) ? true : false;
      columnNumber = inBuffer.readInt();
      columnInfo = readColumnInfo(inBuffer);
    }
    catch (UJciException e)
    {
      closeInternal();
      e.toUError(errorHandler);
      return false;
    }
    catch (IOException e)
    {
      closeInternal();
      errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
      return false;
    }

    fetchedTupleNumber = 0;
    currentFirstCursor = cursorPosition = -1;
    /* set max resultset row size */
    totalTupleNumber = executeResult = ((maxFetchSize > 0) && (executeResult > maxFetchSize)) ? maxFetchSize
        : executeResult;
    realFetched = false;
    return true;
  }

  /*
   * 3.0 synchronized public boolean nextResult (int rs_mode) { UInputBuffer
   * inBuffer;
   * 
   * errorHandler = new UError(); if (isClosed == true) {
   * errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED); return false; } try {
   * synchronized (relatedConnection) {
   * outBuffer.newRequest(UFunctionCode.NEXT_RESULT);
   * outBuffer.addInt(serverHandler); outBuffer.addInt(rs_mode);
   * 
   * inBuffer = relatedConnection.send_recv_msg(); }
   * 
   * closeInternal(); executeResult = inBuffer.readInt(); commandTypeIs =
   * inBuffer.readByte(); isUpdatable = (inBuffer.readByte() == 1) ? true :
   * false; columnNumber = inBuffer.readInt(); columnInfo =
   * readColumnInfo(inBuffer); } catch (UJciException e) { closeInternal();
   * e.toUError(errorHandler); return false; } catch (IOException e) {
   * closeInternal(); errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
   * return false; }
   * 
   * fetchedTupleNumber = 0; currentFirstCursor = cursorPosition = -1; // set
   * max resultset row size totalTupleNumber = executeResult = ((maxFetchSize>0)
   * && (executeResult>maxFetchSize)) ? maxFetchSize : executeResult;
   * realFetched = false; return true; } 3.0
   */

  /*
   * fetch method를 call했을 때 cursor만 움직이고 fetch하지 않으면 false,
   * server와의 통신을 통해 실재로 fetch가 이루어졌으면 true를 return한다.
   */

  public boolean realFetched()
  {
    return realFetched;
  }

  /*
   * 현재 ResultSet이 가지고 있는 data들을 refetch한다. 이 때 적용되는
   * sensitivity는 execute시 넘겨준 flag에 의해 결정된다. Error Code :
   * ER_IS_CLOSED, ER_ILLEGAL_DATA_SIZE, ER_INVALID_ARGUMENT, ER_COMMUNICATION
   */

  synchronized public void reFetch()
  {
    UInputBuffer inBuffer;

    errorHandler = new UError();
    if (isClosed == true)
    {
      errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
      return;
    }

    if (statementType == GET_BY_OID)
      return;

    try
    {
      synchronized (relatedConnection)
      {
        outBuffer.newRequest(UFunctionCode.FETCH);
        outBuffer.addInt(serverHandler);
        if (fetchDirection == ResultSet.FETCH_REVERSE)
        {
          int startPos = cursorPosition - fetchSize + 2;
          if (startPos < 1)
            startPos = 1;
          outBuffer.addInt(startPos);
        }
        else
        {
          outBuffer.addInt(cursorPosition + 1);
        }
        outBuffer.addInt(fetchSize);
        outBuffer.addByte((isSensitive == true) ? (byte) 1 : (byte) 0);
        // jci 3.0
        outBuffer.addInt(0);
        // outBuffer.addInt(resultset_index);

        inBuffer = relatedConnection.send_recv_msg();
      }

      read_fetch_data(inBuffer);
      realFetched = true;
    }
    catch (UJciException e)
    {
      e.toUError(errorHandler);
    }
    catch (IOException e)
    {
      errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
    }
  }

  /*
   * ResultSet의 fetch direction을 지정해준다. Direction은 ResultSet에 지정된
   * static variable이어야 하며 그렇지 않은 경우 ER_INVALID_ARGUMENT error를
   * set한다. Error Code : ER_IS_CLOSED, ER_INVALID_ARGUMENT
   */

  synchronized public void setFetchDirection(int direction)
  {
    errorHandler = new UError();
    if (isClosed == true)
    {
      errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
      return;
    }
    if (direction != ResultSet.FETCH_FORWARD
        && direction != ResultSet.FETCH_REVERSE
        && direction != ResultSet.FETCH_UNKNOWN)
    {
      errorHandler.setErrorCode(UErrorCode.ER_INVALID_ARGUMENT);
      return;
    }
    fetchDirection = direction;
  }

  /*
   * ResultSet의 fetch size를 지정해준다. Error Code : ER_IS_CLOSED,
   * ER_INVALID_ARGUMENT
   */

  synchronized public void setFetchSize(int size)
  {
    errorHandler = new UError();
    if (isClosed == true)
    {
      errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
      return;
    }

    if (size < 0)
    {
      errorHandler.setErrorCode(UErrorCode.ER_INVALID_ARGUMENT);
      return;
    }

    if (size == 0)
      fetchSize = DEFAULT_FETCH_SIZE;
    else
      fetchSize = size;
  }

  /*
   * ResultSet의 특정 attribute value를 update하기 위한 interface이다. 이때 각
   * value들은 해당 index의 columnInfo에 저장되어 있는 type으로 간주되어
   * conversion을 시도하며 type conversion이 불가할 경우 Type Conversion Error를
   * Set한다. JCI에서는 해당 column이 updatable인지 판단하지 않는다. 따라서
   * updatable 하지 않는 column을 update하고자 한다면 server error가 발생할
   * 것이다. Error Code : ER_IS_CLOSED, ER_INVALID_ARGUMENT,
   * ER_ILLEGAL_DATA_SIZE, ER_TYPE_CONVERSION, ER_COMMUNICATION
   */

  synchronized public void updateRows(int cursorPosition, int[] indexes,
      Object[] values)
  {
    UInputBuffer inBuffer;

    errorHandler = new UError();
    if (isClosed == true)
    {
      errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
      return;
    }

    UUpdateParameter updateParameter;

    try
    {
      updateParameter = new UUpdateParameter(columnInfo, indexes, values);
      synchronized (relatedConnection)
      {
        outBuffer.newRequest(UFunctionCode.CURSOR_UPDATE);
        outBuffer.addInt(serverHandler);
        outBuffer.addInt(cursorPosition + 1);
        updateParameter.writeParameter(outBuffer);

        inBuffer = relatedConnection.send_recv_msg();
      }
    }
    catch (UJciException e)
    {
      e.toUError(errorHandler);
    }
    catch (IOException e)
    {
      errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
    }
  }

  synchronized public byte getCommandType()
  {
    return commandTypeIs;
  }

  /*
   * 3.0 synchronized public UParameterInfo[] getParameterInfo() { UInputBuffer
   * inBuffer;
   * 
   * errorHandler = new UError(); if (isClosed == true) {
   * errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED); return null; } try {
   * synchronized (relatedConnection) {
   * outBuffer.newRequest(UFunctionCode.PARAMETER_INFO);
   * outBuffer.addInt(serverHandler);
   * 
   * inBuffer = relatedConnection.send_recv_msg(); }
   * 
   * pramNumber = inBuffer.getResCode(); return readParameterInfo(); } catch
   * (UJciException e) { e.toUError(errorHandler); } catch (IOException e) {
   * errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION); } return null; }
   * 3.0
   */

  synchronized public String getQueryplan()
  {
    String plan = null;

    errorHandler = new UError();
    if (isClosed == true)
    {
      errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
      return null;
    }

    try
    {
      UInputBuffer inBuffer;

      synchronized (relatedConnection)
      {
        outBuffer.newRequest(UFunctionCode.GET_QUERY_INFO);
        outBuffer.addInt(serverHandler);
        outBuffer.addByte(QUERY_INFO_PLAN);

        inBuffer = relatedConnection.send_recv_msg();
      }

      plan = inBuffer.readString(inBuffer.remainedCapacity(),
          relatedConnection.conCharsetName);
    }
    catch (UJciException e)
    {
      closeInternal();
      e.toUError(errorHandler);
      return null;
    }
    catch (IOException e)
    {
      closeInternal();
      errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
      return null;
    }

    return plan;
  }

  synchronized public boolean getGeneratedKeys()
  {
    errorHandler = new UError();
    if (isClosed == true)
    {
      errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
      return false;
    }

    try
    {
      UInputBuffer inBuffer;

      synchronized (relatedConnection)
      {
        outBuffer.newRequest(UFunctionCode.GET_GENERATED_KEYS);
        outBuffer.addInt(serverHandler);
        inBuffer = relatedConnection.send_recv_msg();

        commandTypeIs = inBuffer.readByte();
        totalTupleNumber = inBuffer.readInt();
        isUpdatable = (inBuffer.readByte() == 1) ? true : false;
        columnNumber = inBuffer.readInt();
        statementType = GET_AUTOINCREMENT_KEYS;
        firstColumnInfo = readColumnInfo(inBuffer);
        columnInfo = firstColumnInfo;
        executeResult = totalTupleNumber;
        read_fetch_data(inBuffer);
      }
    }
    catch (UJciException e)
    {
      closeInternal();
      e.toUError(errorHandler);
      return false;
    }
    catch (IOException e)
    {
      closeInternal();
      errorHandler.setErrorCode(UErrorCode.ER_COMMUNICATION);
      return false;
    }

    return true;
  }

  private void bindValue(int index, byte type, Object data)
  {
    UError localError;

    localError = new UError();
    if (bindParameter == null)
    {
      localError.setErrorCode(UErrorCode.ER_IS_CLOSED);
      errorHandler = localError;
      return;
    }
    if (index < 0 || index >= parameterNumber)
    {
      localError.setErrorCode(UErrorCode.ER_BIND_INDEX);
      errorHandler = localError;
      return;
    }

    try
    {
      synchronized (bindParameter)
      {
        bindParameter.setParameter(index, type, data);
      }
    }
    catch (UJciException e)
    {
      e.toUError(localError);
    }
    errorHandler = localError;
  }

  private Object beforeGetXXX(int index)
  {
    if (isClosed == true)
    {
      errorHandler.setErrorCode(UErrorCode.ER_IS_CLOSED);
      return null;
    }
    if (index < 0 || index >= columnNumber)
    {
      errorHandler.setErrorCode(UErrorCode.ER_COLUMN_INDEX);
      return null;
    }
    if (checkReFetch() != true)
      return null;
    if (fetchedTupleNumber <= 0)
    {
      errorHandler.setErrorCode(UErrorCode.ER_NO_MORE_DATA);
      return null;
    }

    /*
     * if (tuples == null || tuples[cursorPosition - currentFirstCursor] == null
     * || tuples[cursorPosition-currentFirstCursor].wasNull(index) == true)
     */
    Object obj;
    if ((tuples == null)
        || (tuples[cursorPosition - currentFirstCursor] == null)
        || ((obj = tuples[cursorPosition - currentFirstCursor]
            .getAttribute(index)) == null))
    {
      errorHandler.setErrorCode(UErrorCode.ER_WAS_NULL);
      return null;
    }

    return obj;
  }

  /*
   * cursor가 현재 fetch된 tuple의 범위 밖으로 위치해 있을 때 cursor가 위치한
   * tuple을 fetch하기 위한 method
   */

  private boolean checkReFetch()
  {
    if ((currentFirstCursor < 0)
        || (cursorPosition >= 0 && ((cursorPosition < currentFirstCursor) || (cursorPosition > currentFirstCursor
            + fetchedTupleNumber - 1))))
    {
      fetch();
      if (errorHandler.getErrorCode() != UErrorCode.ER_NO_ERROR)
        return false;
    }
    return true;
  }

  /*
   * column을 fetch할 때 column의 type이 NULL인 경우 column size보다 먼저 column
   * type이 CAS로부터 넘어온다. 이 때 column의 type이 NULL인지 아닌지를 검사
   */

  private byte readTypeFromData(int index, UInputBuffer inBuffer)
      throws UJciException
  {
    if ((commandTypeIs == CUBRIDCommandType.CUBRID_STMT_CALL)
        || (commandTypeIs == CUBRIDCommandType.CUBRID_STMT_EVALUATE)
        || (commandTypeIs == CUBRIDCommandType.CUBRID_STMT_CALL_SP)
        || (columnInfo[index].getColumnType() == UUType.U_TYPE_NULL))
    {
      return inBuffer.readByte();
    }
    return UUType.U_TYPE_NULL;
  }

  private void closeFirstColumnInfo()
  {
    if (firstColumnInfo != null)
    {
      synchronized (firstColumnInfo)
      {
        for (int i = 0; i < firstColumnInfo.length; i++)
          firstColumnInfo[i] = null;
      }
      firstColumnInfo = null;
    }
  }

  private void closeInternal()
  {
    if (columnInfo != null)
    {
      synchronized (columnInfo)
      {
        for (int i = 0; i < columnInfo.length; i++)
          columnInfo[i] = null;
      }
      columnInfo = null;
    }
    if (bindParameter != null)
    {
      synchronized (bindParameter)
      {
        bindParameter.close();
      }
      bindParameter = null;
    }
    if (tuples != null)
    {
      synchronized (tuples)
      {
        for (int i = 0; i < tuples.length; i++)
        {
          if (tuples[i] == null)
            continue;
          tuples[i].close();
          tuples[i] = null;
        }
      }
      tuples = null;
    }
  }

  /*
   * collection type의 경우 server쪽에서 넘어오는 type정보가 collection
   * type정보와 collection base type정보가 logical or로 조합되어 넘어온다. 이
   * method는 getSchemaInfo에 의해 얻어진 ResultSet의 Collection type 정보를
   * 나타내는 value를 check하여 collection type 정보만을 추출하여 그 값을 Result
   * set의 value로 바꾸어준다. 이때 collection base type 정보는 무시된다.
   */

  private void confirmSchemaTypeInfo(int index)
  {
    if (statementType != GET_SCHEMA_INFO)
      return;

    byte realType[];
    Short fetchedType;

    switch (schemaType)
    {
    case USchType.SCH_ATTRIBUTE:
    case USchType.SCH_CLASS_ATTRIBUTE:
    case USchType.SCH_METHOD:
    case USchType.SCH_CLASS_METHOD:
    case USchType.SCH_SUPERCLASS:
    case USchType.SCH_SUBCLASS:
      fetchedType = (Short) tuples[index].getAttribute(1);
      realType = UColumnInfo.confirmType(fetchedType.byteValue());
      tuples[index].setAttribute(1, new Short((short) realType[0]));
    }
  }

  /*
   * UInputBuffer로부터 column의 값을 읽어 return해 준다. column type이 NULL인
   * 경우 column size보다 먼저 column의 type이 CAS로부터 넘어온다.
   */

  private Object readAAttribute(int index, UInputBuffer inBuffer)
      throws IOException, UJciException
  {
    int size;
    int localType;

    size = inBuffer.readInt();
    if (size <= 0)
      return null;

    localType = readTypeFromData(index, inBuffer);
    if (localType == UUType.U_TYPE_NULL)
      localType = columnInfo[index].getColumnType();
    else
      size--;

    return (readData(inBuffer, localType, size));
  }

  private Object readData(UInputBuffer inBuffer, int dataType, int dataSize)
      throws IOException, UJciException
  // throws IOException, UJciException
  {
    switch (dataType)
    {
    case UUType.U_TYPE_CHAR:
    case UUType.U_TYPE_NCHAR:
    case UUType.U_TYPE_STRING:
    case UUType.U_TYPE_VARNCHAR:
      return inBuffer.readString(dataSize, relatedConnection.conCharsetName);
    case UUType.U_TYPE_NUMERIC:
      return new BigDecimal(inBuffer.readString(dataSize,
          UJCIManager.sysCharsetName));
    case UUType.U_TYPE_INT:
      return new Integer(inBuffer.readInt());
    case UUType.U_TYPE_SHORT:
      return new Short(inBuffer.readShort());
    case UUType.U_TYPE_DATE:
      return inBuffer.readDate();
    case UUType.U_TYPE_TIME:
      return inBuffer.readTime();
    case UUType.U_TYPE_TIMESTAMP:
      return inBuffer.readTimestamp();
    case UUType.U_TYPE_OBJECT:
      return inBuffer.readOID(relatedConnection.cubridcon);
    case UUType.U_TYPE_SET:
    case UUType.U_TYPE_MULTISET:
    case UUType.U_TYPE_SEQUENCE:
    {
      CUBRIDArray aArray;
      aArray = new CUBRIDArray(inBuffer.readByte(), inBuffer.readInt());
      int baseType = aArray.getBaseType();
      for (int i = 0; i < aArray.getLength(); i++)
      {
        int eleSize = inBuffer.readInt();
        if (eleSize <= 0)
          aArray.setElement(i, null);
        else
          aArray.setElement(i, readData(inBuffer, baseType, eleSize));
      }
      return aArray;
    }
    case UUType.U_TYPE_MONETARY:
    case UUType.U_TYPE_DOUBLE:
      return new Double(inBuffer.readDouble());
    case UUType.U_TYPE_FLOAT:
      return new Float(inBuffer.readFloat());
    case UUType.U_TYPE_BIT:
    case UUType.U_TYPE_VARBIT:
      return inBuffer.readBytes(dataSize);
    case UUType.U_TYPE_RESULTSET:
      return new CUBRIDOutResultSet(relatedConnection, inBuffer.readInt());
    default:
      return null;
    }
  }

  private void read_fetch_data(UInputBuffer inBuffer) throws IOException,
      UJciException
  {
    fetchedTupleNumber = inBuffer.readInt();
    if (fetchedTupleNumber < 0)
      fetchedTupleNumber = 0;
    tuples = new UResultTuple[fetchedTupleNumber];
    for (int i = 0; i < fetchedTupleNumber; i++)
      readATuple(i, inBuffer);
  }

  /*
   * class UConnection method getByOid에 의해 allocate되는 UStatement
   * constructor가 ResultSet을 Fetch하기 위한 method
   */

  private void readATupleByOid(CUBRIDOID oid, UInputBuffer inBuffer)
      throws IOException, UJciException
  {
    tuples[0] = new UResultTuple(1, columnNumber);
    tuples[0].setOid(oid);
    for (int i = 0; i < columnNumber; i++)
    {
      tuples[0].setAttribute(i, readAAttribute(i, inBuffer));
    }
    currentFirstCursor = 0;
  }

  /* 일반적인 UStatement의 ResultSet의 1개의 tuple을 fetch하기 위한 method */

  private void readATuple(int index, UInputBuffer inBuffer) throws IOException,
      UJciException
  {
    tuples[index] = new UResultTuple(inBuffer.readInt(), columnNumber);
    tuples[index].setOid(inBuffer.readOID(relatedConnection.cubridcon));
    for (int i = 0; i < columnNumber; i++)
    {
      tuples[index].setAttribute(i, readAAttribute(i, inBuffer));
    }

    confirmSchemaTypeInfo(index);

    if (index == 0)
      currentFirstCursor = tuples[index].tupleNumber() - 1;
  }

  /* UStatement constructor에서 column info를 read하기 위해 call하는 method */

  private UColumnInfo[] readColumnInfo(UInputBuffer inBuffer)
      throws IOException, UJciException
  {
    byte type;
    short scale;
    int precision;
    String name;

    UColumnInfo localColumnInfo[] = new UColumnInfo[columnNumber];
    for (int i = 0; i < columnNumber; i++)
    {
      type = inBuffer.readByte();
      scale = inBuffer.readShort();
      precision = inBuffer.readInt();
      name = inBuffer.readString(inBuffer.readInt(),
          relatedConnection.conCharsetName);
      localColumnInfo[i] = new UColumnInfo(type, scale, precision, name);
      if (statementType == NORMAL)
      {
        String attributeName = inBuffer.readString(inBuffer.readInt(),
            relatedConnection.conCharsetName);
        String className = inBuffer.readString(inBuffer.readInt(),
            relatedConnection.conCharsetName);
        byte byteData = inBuffer.readByte();
        localColumnInfo[i].setRemainedData(attributeName, className,
            ((byteData == (byte) 0) ? true : false));
      }
      /*
       * if ((commandTypeIs==CUBRIDCommandType.CUBRID_STMT_EVALUATE) ||
       * (commandTypeIs==CUBRIDCommandType.CUBRID_STMT_CALL)) {
       * localColumnInfo[i] = new UColumnInfo(UUType.U_TYPE_VARCHAR, (short) 0,
       * 1073741823 , "method_result"); localColumnInfo[i].setRemainedData("",
       * "", false); }
       */
    }
    return localColumnInfo;
  }

  /* result info를 read하기 위해 call하는 method */

  private void readResultInfo(UInputBuffer inBuffer) throws UJciException
  {
    resultInfo = new UResultInfo[inBuffer.readInt()];
    for (int i = 0; i < resultInfo.length; i++)
    {
      resultInfo[i] = new UResultInfo(inBuffer.readByte(), inBuffer.readInt());
      resultInfo[i].setResultOid(inBuffer.readOID(relatedConnection.cubridcon));
      resultInfo[i].setSrvCacheTime(inBuffer.readInt(), inBuffer.readInt());
    }
  }

  public int getServerHandle()
  {
    return serverHandler;
  }

  public void setReturnable()
  {
    isReturnable = true;
  }

  public boolean isReturnable()
  {
    return isReturnable;
  }

  /*
   * 3.0 private UParameterInfo[] readParameterInfo() throws UJciException {
   * byte mode; byte type; short scale; int precision;
   * 
   * UParameterInfo localParameterInfo[] = new UParameterInfo[pramNumber]; for
   * (int i=0; i < pramNumber ; i++) { mode = inBuffer.readByte(); type =
   * inBuffer.readByte(); scale = inBuffer.readShort(); precision =
   * inBuffer.readInt(); localParameterInfo[i] = new UParameterInfo(mode, type,
   * scale, precision); } return localParameterInfo; }
   */

  /*
   * public UStatementCacheData makeCacheData() { if (fetchedTupleNumber <
   * totalTupleNumber) return null;
   * 
   * if (resultInfo.length > 1) { result_cacheable = false; return null; }
   * 
   * return (new UStatementCacheData(totalTupleNumber, tuples, resultInfo)); }
   */

  public void setCacheData(UStatementCacheData cache_data)
  {
    totalTupleNumber = cache_data.tuple_count;
    tuples = cache_data.tuples;
    resultInfo = cache_data.resultInfo;

    cursorPosition = currentFirstCursor = 0;
    fetchedTupleNumber = totalTupleNumber;
    executeResult = totalTupleNumber;
    realFetched = true;
  }

  public boolean is_result_cacheable()
  {
    if (result_cacheable == true && relatedConnection.update_executed == false )
      return true;
    else
      return false;
  }

  public void setAutoGeneratedKeys(boolean isGeneratedKeys){
	  this.isGeneratedKeys = isGeneratedKeys;
  }

  protected void finalize()
  {
    if (stmt_cache != null)
      stmt_cache.decr_ref_count();
  }
}
