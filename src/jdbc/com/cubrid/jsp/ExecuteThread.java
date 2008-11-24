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

package com.cubrid.jsp;

import java.lang.reflect.InvocationTargetException;
import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.math.BigDecimal;
import java.net.Socket;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.Calendar;

import cubrid.jdbc.driver.CUBRIDConnection;
import cubrid.jdbc.jci.UConnection;
import cubrid.jdbc.jci.UJCIUtil;
import cubrid.sql.CUBRIDOID;

import com.cubrid.jsp.exception.ExecuteException;
import com.cubrid.jsp.exception.TypeMismatchException;
import com.cubrid.jsp.value.DateValue;
import com.cubrid.jsp.value.DoubleValue;
import com.cubrid.jsp.value.FloatValue;
import com.cubrid.jsp.value.IntValue;
import com.cubrid.jsp.value.NullValue;
import com.cubrid.jsp.value.NumericValue;
import com.cubrid.jsp.value.OidValue;
import com.cubrid.jsp.value.SetValue;
import com.cubrid.jsp.value.ShortValue;
import com.cubrid.jsp.value.StringValue;
import com.cubrid.jsp.value.TimeValue;
import com.cubrid.jsp.value.TimestampValue;
import com.cubrid.jsp.value.Value;

import cubrid.jdbc.driver.CUBRIDResultSet;
import cubrid.sql.CUBRIDOID;

public class ExecuteThread extends Thread
{
  public static final int DB_NULL = 0;

  public static final int DB_INT = 1;

  public static final int DB_FLOAT = 2;

  public static final int DB_DOUBLE = 3;

  public static final int DB_STRING = 4;

  public static final int DB_OBJECT = 5;

  public static final int DB_SET = 6;

  public static final int DB_MULTISET = 7;

  public static final int DB_SEQUENCE = 8;

  public static final int DB_TIME = 10;

  public static final int DB_TIMESTAMP = 11;

  public static final int DB_DATE = 12;

  public static final int DB_MONETARY = 13;

  public static final int DB_SHORT = 18;

  public static final int DB_NUMERIC = 22;

  public static final int DB_CHAR = 25;

  public static final int DB_RESULTSET = 28;

  private Socket client;

  private DataOutputStream toClient;

  private ByteArrayOutputStream byteBuf = new ByteArrayOutputStream(1024);

  private DataOutputStream outBuf = new DataOutputStream(byteBuf);

  private Connection jdbcConnection = null;

  private String charSet = System.getProperty("file.encoding");

  ExecuteThread(Socket client) throws IOException
  {
    super();
    this.client = client;
    toClient = new DataOutputStream(new BufferedOutputStream(this.client
        .getOutputStream()));
  }

  public Socket getSocket()
  {
    return client;
  }

  private void closeJdbcConnection()
  {
    if (jdbcConnection != null)
    {
      try
      {
        jdbcConnection.close();
      }
      catch (SQLException e)
      {
        Server.log(e);
      }
      jdbcConnection = null;
    }
  }

  public void run()
  {
    while (true)
    {
      try
      {
        Object resolvedResult = null;

        StoredProcedure sp = makeStoredProcedure();
        Value result = sp.invoke();

        closeJdbcConnection();

        if (result != null)
        {
          resolvedResult = toDbTypeValue(sp.getReturnType(), result);
        }

        byteBuf.reset();
        sendValue(resolvedResult, outBuf, sp.getReturnType());
        returnOutArgs(sp, outBuf);
        outBuf.flush();

        toClient.writeInt(0x2);
        toClient.writeInt(byteBuf.size() + 4);
        byteBuf.writeTo(toClient);
        toClient.writeInt(0x2);
        toClient.flush();

        sp = null;
        result = null;

        // toClient.close();
        // client.close();
      }
      catch (Throwable e)
      {
        if (e instanceof IOException)
        {
          break;
        }
        else if (e instanceof InvocationTargetException)
        {
          Server.log(((InvocationTargetException) e).getTargetException());
        }
        else
        {
          Server.log(e);
        }
        closeJdbcConnection();
        try
        {
          sendError(e, client);
        }
        catch (IOException e1)
        {
          Server.log(e1);
        }
      }
      finally
      {
        closeJdbcConnection();
      }
    }

    try
    {
      byteBuf.close();
      outBuf.close();
      toClient.close();
      client.close();
    }
    catch (IOException e)
    {
    }

    client = null;
    toClient = null;
    byteBuf = null;
    outBuf = null;
    jdbcConnection = null;
    charSet = null;
  }

  private Object toDbTypeValue(int dbType, Value result)
      throws TypeMismatchException
  {
    Object resolvedResult = null;

    if (result == null)
      return null;

    switch (dbType)
    {
    case DB_INT:
      resolvedResult = result.toIntegerObject();
      break;
    case DB_FLOAT:
      resolvedResult = result.toFloatObject();
      break;
    case DB_DOUBLE:
    case DB_MONETARY:
      resolvedResult = result.toDoubleObject();
      break;
    case DB_CHAR:
    case DB_STRING:
      resolvedResult = result.toString();
      break;
    case DB_SET:
    case DB_MULTISET:
    case DB_SEQUENCE:
      resolvedResult = result.toObjectArray();
      break;
    case DB_TIME:
      resolvedResult = result.toTime();
      break;
    case DB_DATE:
      resolvedResult = result.toDate();
      break;
    case DB_TIMESTAMP:
      resolvedResult = result.toTimestamp();
      break;
    case DB_SHORT:
      resolvedResult = result.toShortObject();
      break;
    case DB_NUMERIC:
      resolvedResult = result.toBigDecimal();
      break;
    case DB_OBJECT:
      resolvedResult = result.toOid();
      break;
    case DB_RESULTSET:
      resolvedResult = result.toResultSet();
      break;
    default:
      break;
    }

    return resolvedResult;
  }

  private void returnOutArgs(StoredProcedure sp, DataOutputStream dos)
      throws IOException, ExecuteException, TypeMismatchException
  {
    Value[] args = sp.getArgs();
    for (int i = 0; i < args.length; i++)
    {
      if (args[i].getMode() > Value.IN)
      {
        Value v = makeOutBingValue(sp, args[i].getResolved());
        sendValue(toDbTypeValue(args[i].getDbType(), v), dos, args[i]
            .getDbType());
      }
    }
  }

  private Value makeOutBingValue(StoredProcedure sp, Object object)
      throws ExecuteException
  {
    Object obj = null;
    if (object instanceof byte[])
    {
      obj = new Byte(((byte[]) object)[0]);
    }
    else if (object instanceof short[])
    {
      obj = new Short(((short[]) object)[0]);
    }
    else if (object instanceof int[])
    {
      obj = new Integer(((int[]) object)[0]);
    }
    else if (object instanceof long[])
    {
      obj = new Long(((long[]) object)[0]);
    }
    else if (object instanceof float[])
    {
      obj = new Float(((float[]) object)[0]);
    }
    else if (object instanceof double[])
    {
      obj = new Double(((double[]) object)[0]);
    }
    else if (object instanceof byte[][])
    {
      obj = ((byte[][]) object)[0];
    }
    else if (object instanceof short[][])
    {
      obj = ((short[][]) object)[0];
    }
    else if (object instanceof int[][])
    {
      obj = ((int[][]) object)[0];
    }
    else if (object instanceof long[][])
    {
      obj = ((long[][]) object)[0];
    }
    else if (object instanceof float[][])
    {
      obj = ((float[][]) object)[0];
    }
    else if (object instanceof double[][])
    {
      obj = ((double[][]) object)[0];
    }
    else if (object instanceof Object[])
    {
      obj = ((Object[]) object)[0];
    }

    return sp.makeReturnValue(obj);
  }

  private void sendError(Throwable e, Socket socket) throws IOException
  {
    byteBuf.reset();

    sendValue(new Integer(1), outBuf, DB_INT);
    sendValue(e.toString(), outBuf, DB_STRING);

    outBuf.flush();
    toClient.writeInt(0x4);
    toClient.writeInt(byteBuf.size() + 4);
    byteBuf.writeTo(toClient);
    toClient.writeInt(0x4);
    toClient.flush();

    // toClient.close();
    // client.close();
  }

  private StoredProcedure makeStoredProcedure() throws Exception
  {
    DataInputStream dis = new DataInputStream(new BufferedInputStream(
        this.client.getInputStream()));

    int startCode = dis.readInt();
    // System.out.println("startCode= " + startCode);
    if (startCode != 0x1)
      return null;

    int methodSigLength = dis.readInt();
    // System.out.println("methodSigLength= " + methodSigLength);

    byte[] methodSig = new byte[methodSigLength];
    dis.readFully(methodSig);
    // System.out.println("methodSig= " + new String(methodSig));

    int paramCount = dis.readInt();
    // System.out.println("paramCount= " + paramCount);

    Value[] args = readArguments(dis, paramCount);
    // for (int i = 0; i < args.length; i++) {
    // System.out.println("arg[" + i + "]= " + args[i]);
    // }

    int returnType = dis.readInt();
    // System.out.println("returnType= " + returnType);

    int endCode = dis.readInt();
    // System.out.println("endcode= " + endCode);
    if (startCode != endCode)
      return null;

    return new StoredProcedure(new String(methodSig), args, returnType);
  }

  private Value[] readArguments(DataInputStream dis, int paramCount)
      throws IOException, TypeMismatchException, SQLException
  {
    Value[] args = new Value[paramCount];

    for (int i = 0; i < paramCount; i++)
    {
      int mode = dis.readInt();
      // System.out.println("mode= " + mode);

      int dbType = dis.readInt();
      // System.out.println("dbType= " + dbType);

      int paramType = dis.readInt();
      // System.out.println("paramType= " + paramType);

      int paramSize = dis.readInt();
      // System.out.println("paramSize= " + paramSize);

      Value arg = readArgument(dis, paramSize, paramType, mode, dbType);
      args[i] = (arg);
    }

    return args;
  }

  private Value[] readArgumentsForSet(DataInputStream dis, int paramCount)
      throws IOException, TypeMismatchException, SQLException
  {
    Value[] args = new Value[paramCount];

    for (int i = 0; i < paramCount; i++)
    {
      int paramType = dis.readInt();
      // System.out.println("paramType= " + paramType);

      int paramSize = dis.readInt();
      // System.out.println("paramSize= " + paramSize);

      Value arg = readArgument(dis, paramSize, paramType, Value.IN, 0);
      args[i] = (arg);
    }

    return args;
  }

  private Value readArgument(DataInputStream dis, int paramSize, int paramType,
      int mode, int dbType) throws IOException, TypeMismatchException,
      SQLException
  {
    Value arg = null;
    switch (paramType)
    {
    case DB_SHORT:
      // assert paramSize == 4
      arg = new ShortValue((short) dis.readInt(), mode, dbType);
      break;
    case DB_INT:
      // assert paramSize == 4
      arg = new IntValue(dis.readInt(), mode, dbType);
      break;
    case DB_FLOAT:
      // assert paramSize == 4
      arg = new FloatValue(dis.readFloat(), mode, dbType);
      break;
    case DB_DOUBLE:
    case DB_MONETARY:
      // assert paramSize == 8
      arg = new DoubleValue(dis.readDouble(), mode, dbType);
      break;
    case DB_NUMERIC:
    {
      byte[] paramValue = new byte[paramSize];
      dis.readFully(paramValue);

      int i;
      for (i = 0; i < paramValue.length; i++)
      {
        if (paramValue[i] == 0)
          break;
      }

      byte[] strValue = new byte[i];
      System.arraycopy(paramValue, 0, strValue, 0, i);

      arg = new NumericValue(new String(strValue), mode, dbType);
    }
      break;
    case DB_CHAR:
    case DB_STRING:
      // assert paramSize == n
    {
      byte[] paramValue = new byte[paramSize];
      dis.readFully(paramValue);

      int i;
      for (i = 0; i < paramValue.length; i++)
      {
        if (paramValue[i] == 0)
          break;
      }

      byte[] strValue = new byte[i];
      System.arraycopy(paramValue, 0, strValue, 0, i);
      arg = new StringValue(new String(strValue), mode, dbType);
    }
      break;
    case DB_DATE:
      // assert paramSize == 3
    {
      int year = dis.readInt();
      int month = dis.readInt();
      int day = dis.readInt();

      arg = new DateValue(year, month, day, mode, dbType);
    }
      break;
    case DB_TIME:
      // assert paramSize == 3
    {
      int hour = dis.readInt();
      int min = dis.readInt();
      int sec = dis.readInt();
      Calendar cal = Calendar.getInstance();
      cal.set(0, 0, 0, hour, min, sec);

      arg = new TimeValue(hour, min, sec, mode, dbType);
    }
      break;
    case DB_TIMESTAMP:
      // assert paramSize == 6
    {
      int year = dis.readInt();
      int month = dis.readInt();
      int day = dis.readInt();
      int hour = dis.readInt();
      int min = dis.readInt();
      int sec = dis.readInt();
      Calendar cal = Calendar.getInstance();
      cal.set(year, month, day, hour, min, sec);

      arg = new TimestampValue(year, month, day, hour, min, sec, mode, dbType);
    }
      break;
    case DB_SET:
    case DB_MULTISET:
    case DB_SEQUENCE:
    {
      int nCol = dis.readInt();
      // System.out.println(nCol);
      arg = new SetValue(readArgumentsForSet(dis, nCol), mode, dbType);
    }
      break;
    case DB_OBJECT:
    {
      int page = dis.readInt();
      short slot = (short) dis.readInt();
      short vol = (short) dis.readInt();

      byte[] bOID = new byte[UConnection.OID_BYTE_SIZE];
      bOID[0] = ((byte) ((page >>> 24) & 0xFF));
      bOID[1] = ((byte) ((page >>> 16) & 0xFF));
      bOID[2] = ((byte) ((page >>> 8) & 0xFF));
      bOID[3] = ((byte) ((page >>> 0) & 0xFF));
      bOID[4] = ((byte) ((slot >>> 8) & 0xFF));
      bOID[5] = ((byte) ((slot >>> 0) & 0xFF));
      bOID[6] = ((byte) ((vol >>> 8) & 0xFF));
      bOID[7] = ((byte) ((vol >>> 0) & 0xFF));

      if (jdbcConnection == null)
      {
        jdbcConnection = DriverManager
            .getConnection("jdbc:default:connection:");
      }
      arg = new OidValue(
          new CUBRIDOID((CUBRIDConnection) jdbcConnection, bOID), mode, dbType);
    }
      break;
    case DB_NULL:
      arg = new NullValue(mode, dbType);
      break;
    default:
      // unknown type
      break;
    }
    return arg;
  }

  private void sendValue(Object result, DataOutputStream dos, int ret_type)
      throws IOException
  {
    if (result == null)
    {
      dos.writeInt(DB_NULL);
    }
    else if (result instanceof Short)
    {
      dos.writeInt(DB_SHORT);
      dos.writeInt(((Short) result).intValue());
    }
    else if (result instanceof Integer)
    {
      dos.writeInt(DB_INT);
      dos.writeInt(((Integer) result).intValue());
    }
    else if (result instanceof Float)
    {
      dos.writeInt(DB_FLOAT);
      dos.writeFloat(((Float) result).floatValue());
    }
    else if (result instanceof Double)
    {
      dos.writeInt(ret_type);
      dos.writeDouble(((Double) result).doubleValue());
    }
    else if (result instanceof BigDecimal)
    {
      dos.writeInt(DB_NUMERIC);
      packAndSendString(((BigDecimal) result).toString(), dos);
    }
    else if (result instanceof String)
    {
      dos.writeInt(DB_STRING);
      packAndSendString((String) result, dos);
    }
    else if (result instanceof java.sql.Date)
    {
      dos.writeInt(DB_DATE);
      packAndSendString(result.toString(), dos);
    }
    else if (result instanceof java.sql.Time)
    {
      dos.writeInt(DB_TIME);
      packAndSendString(result.toString(), dos);
    }
    else if (result instanceof java.sql.Timestamp)
    {
      dos.writeInt(DB_TIMESTAMP);
      packAndSendString(result.toString(), dos);
    }
    else if (result instanceof CUBRIDOID)
    {
      dos.writeInt(DB_OBJECT);
      byte[] oid = ((CUBRIDOID) result).getOID();
      dos.writeInt(UJCIUtil.bytes2int(oid, 0));
      dos.writeInt(UJCIUtil.bytes2short(oid, 4));
      dos.writeInt(UJCIUtil.bytes2short(oid, 6));
    }
    else if (result instanceof ResultSet)
    {
      dos.writeInt(DB_RESULTSET);
      dos.writeInt(((CUBRIDResultSet) result).getServerHandle());
    }
    else if (result instanceof int[])
    {
      int length = ((int[]) result).length;
      Integer[] array = new Integer[length];
      for (int i = 0; i < array.length; i++)
      {
        array[i] = new Integer(((int[]) result)[i]);
      }
      sendValue(array, dos, ret_type);
    }
    else if (result instanceof short[])
    {
      int length = ((short[]) result).length;
      Short[] array = new Short[length];
      for (int i = 0; i < array.length; i++)
      {
        array[i] = new Short(((short[]) result)[i]);
      }
      sendValue(array, dos, ret_type);
    }
    else if (result instanceof float[])
    {
      int length = ((float[]) result).length;
      Float[] array = new Float[length];
      for (int i = 0; i < array.length; i++)
      {
        array[i] = new Float(((float[]) result)[i]);
      }
      sendValue(array, dos, ret_type);
    }
    else if (result instanceof double[])
    {
      int length = ((double[]) result).length;
      Double[] array = new Double[length];
      for (int i = 0; i < array.length; i++)
      {
        array[i] = new Double(((double[]) result)[i]);
      }
      sendValue(array, dos, ret_type);
    }
    else if (result instanceof Object[])
    {
      dos.writeInt(ret_type);
      Object[] arr = (Object[]) result;

      dos.writeInt(arr.length);
      for (int i = 0; i < arr.length; i++)
      {
        sendValue(arr[i], dos, ret_type);
      }
    }
    else
      ;
  }

  private void packAndSendString(String str, DataOutputStream dos)
      throws IOException
  {
    byte b[] = str.getBytes(this.charSet);

    int len = b.length + 1;
    int bits = len & 3;
    int pad = 0;

    if (bits != 0)
      pad = 4 - bits;

    dos.writeInt(len + pad);
    dos.write(b);
    for (int i = 0; i <= pad; i++)
    {
      dos.writeByte(0);
    }
  }

  public void setJdbcConnection(Connection con)
  {
    this.jdbcConnection = con;
  }

  public Connection getJdbcConnection()
  {
    return this.jdbcConnection;
  }

  public void setCharSet(String conCharsetName)
  {
    this.charSet = conCharsetName;
  }
}
