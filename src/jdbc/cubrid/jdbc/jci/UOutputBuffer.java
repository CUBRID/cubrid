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

import java.io.OutputStream;
import java.io.IOException;
import java.math.BigDecimal;
import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;
import java.text.SimpleDateFormat;
import javax.transaction.xa.Xid;

import cubrid.sql.CUBRIDOID;

/*
 * Performance와 Robustness를 위해 CAS에 넘길 data를 buffer에서 보관하여
 * 한꺼번에 write하는 class이다. java.io.OutputStream object의 reference를
 * member variable로 가지고 CAS와의 communication을 한다.
 *
 * Internal Note
 *
 * internal buffer는 2차원 array로 [DEFAULT_BUFFER_SIZE][DEFAULT_CAPACITY]처럼
 * 구성되어 있다. 처음으로 UOutputBuffer object가 만들어졌을 때에는
 * DEFAULT_BUFFER_SIZE만큼 buffer의 row를 확보해놓고 1번째 column( [0][DEFAULT_CAPACITY] )
 * 를 allocation하며 buffer공간이 부족할 경우 추가로 column을 allocation한다.
 * 한 번 allocation한 이상 UOutputBuffer Object가 garbage collector에 의해
 * delete되지 않는 이상 계속 allocation시켜놓는다.
 *
 * since 1.0
 */

class UOutputBuffer
{
  private final static int DEFAULT_BUFFER_SIZE = 1024;
  private final static int DEFAULT_CAPACITY = 100000;

  OutputStream outStream;

  private int length; // data size in bufferes
  private int positionInABuffer; // current cursor position in a buffer
  private int bufferCursor; // current buffer number
  private int allocatedBufferSize; // allocated buffer number
  private byte buffer[][];
  private UConnection u_con;

  UOutputBuffer(UConnection ucon)
  {
    outStream = null;
    length = 0;
    buffer = new byte[DEFAULT_BUFFER_SIZE][];
    allocatedBufferSize = 1;
    buffer[0] = new byte[DEFAULT_CAPACITY];
    bufferCursor = 0;
    positionInABuffer = 0;
    this.u_con = ucon;
  }

  void sendData() throws IOException
  {
    /*
     * byte[] bSize = new byte[4];
     * 
     * bSize[0] = (byte) ((length >>> 24) & 0xFF); bSize[1] = (byte) ((length
     * >>> 16) & 0xFF); bSize[2] = (byte) ((length >>> 8) & 0xFF); bSize[3] =
     * (byte) ((length >>> 0) & 0xFF); outStream.write(bSize);
     * outStream.flush();
     */
    byte[] jdbcInfo = null;
    /* now, just send back received casinfo data to cas */
    jdbcInfo = u_con.getCASInfo();

    overwriteInt(length - 8, 0, 0);
    if (jdbcInfo != null)
    {
      overwriteBytes(u_con.getCASInfo(), 0, 4);
    }

    for (int i = 0; i < bufferCursor; i++)
    {
      outStream.write(buffer[i]);
    }
    if (positionInABuffer > 0)
      outStream.write(buffer[bufferCursor], 0, positionInABuffer);
    outStream.flush();
    clearBuffer(outStream);
  }

  void newRequest(OutputStream out, byte func_code)
  {
    clearBuffer(out);
    writeByte(func_code);
  }

  void newRequest(byte func_code)
  {
    clearBuffer(outStream);
    writeByte(func_code);
  }

  int addInt(int intValue)
  {
    writeInt(4);
    writeInt(intValue);
    return 8;
  }

  int addLong(long longValue)
  {
    writeInt(8);
    writeLong(longValue);
    return 12;
  }

  int addByte(byte bValue)
  {
    writeInt(1);
    writeByte(bValue);
    return 5;
  }

  int addBytes(byte[] value)
  {
    return (addBytes(value, 0, value.length));
  }

  int addBytes(byte[] value, int offset, int len)
  {
    writeInt(len);
    writeBytes(value, offset, len);
    return (len + 4);
  }

  int addNull()
  {
    writeInt(0);
    return 4;
  }

  int addStringWithNull(String str)
  {
    byte[] b;

    try
    {
      b = str.getBytes(u_con.conCharsetName);
    }
    catch (java.io.UnsupportedEncodingException e)
    {
      b = str.getBytes();
    }

    writeInt(b.length + 1);
    writeBytes(b, 0, b.length);
    writeByte((byte) 0);
    return (b.length + 5);
  }

  int addDouble(double value)
  {
    writeInt(8);
    writeDouble(value);
    return 12;
  }

  int addShort(short value)
  {
    writeInt(2);
    writeShort(value);
    return 6;
  }

  int addFloat(float value)
  {
    writeInt(4);
    writeFloat(value);
    return 8;
  }

  int addDate(Date value)
  {
    writeInt(14);
    writeDate(value);
    return 18;
  }

  int addTime(Time value)
  {
    writeInt(14);
    writeTime(value);
    return 18;
  }

  int addTimestamp(Timestamp value)
  {
    writeInt(14);
    writeTimestamp(value);
    return 18;
  }

  int addDatetime(Timestamp value)
  {
    writeInt(14);
    writeDatetime(value);
    return 18;
  }

  int addOID(CUBRIDOID value)
  {
    byte[] b = value.getOID();

    if (b == null || b.length != UConnection.OID_BYTE_SIZE)
    {
      b = new byte[UConnection.OID_BYTE_SIZE];
    }

    writeInt(UConnection.OID_BYTE_SIZE);
    writeBytes(b, 0, b.length);
    return (UConnection.OID_BYTE_SIZE + 4);
  }

  int addXid(Xid xid)
  {
    byte[] gid = xid.getGlobalTransactionId();
    byte[] bid = xid.getBranchQualifier();
    int msg_size = 12 + gid.length + bid.length;
    writeInt(msg_size);
    writeInt(xid.getFormatId());
    writeInt(gid.length);
    writeInt(bid.length);
    writeBytes(gid, 0, gid.length);
    writeBytes(bid, 0, bid.length);
    return msg_size;
  }

  int addCacheTime(UStatementCacheData cache_data)
  {
    int sec, usec;
    if (cache_data == null)
    {
      sec = usec = 0;
    }
    else
    {
      sec = (int) (cache_data.srvCacheTime >>> 32);
      usec = (int) (cache_data.srvCacheTime);
    }
    writeInt(8);
    writeInt(sec);
    writeInt(usec);
    return 12;
  }

  void writeParameter(byte type, Object value) throws UJciException
  {
    String stringData;

    if (value == null)
    {
      addNull();
      return;
    }
    switch (type)
    {
    case UUType.U_TYPE_NULL:
      addNull();
      break;
    case UUType.U_TYPE_CHAR:
    case UUType.U_TYPE_NCHAR:
    case UUType.U_TYPE_STRING:
    case UUType.U_TYPE_VARNCHAR:
      stringData = UGetTypeConvertedValue.getString(value);
      addStringWithNull(stringData);
      break;
    case UUType.U_TYPE_NUMERIC:
      stringData = UGetTypeConvertedValue.getString(value);
      addStringWithNull(stringData);
      break;
    case UUType.U_TYPE_BIT:
    case UUType.U_TYPE_VARBIT:
      if ((value instanceof byte[]) && (((byte[]) value).length > 1))
      {
        addBytes(UGetTypeConvertedValue.getBytes(value));
      }
      else
      {
        addByte(UGetTypeConvertedValue.getByte(value));
      }
      break;
    case UUType.U_TYPE_MONETARY:
    case UUType.U_TYPE_DOUBLE:
      addDouble(UGetTypeConvertedValue.getDouble(value));
      break;
    case UUType.U_TYPE_DATE:
      addDate(UGetTypeConvertedValue.getDate(value));
      break;
    case UUType.U_TYPE_TIME:
      addTime(UGetTypeConvertedValue.getTime(value));
      break;
    case UUType.U_TYPE_TIMESTAMP:
        addTimestamp(UGetTypeConvertedValue.getTimestamp(value));
        break;
    case UUType.U_TYPE_DATETIME:
      addDatetime(UGetTypeConvertedValue.getTimestamp(value));
      break;
    case UUType.U_TYPE_FLOAT:
      addFloat(UGetTypeConvertedValue.getFloat(value));
      break;
    case UUType.U_TYPE_SHORT:
      addShort(UGetTypeConvertedValue.getShort(value));
      break;
    case UUType.U_TYPE_INT:
      addInt(UGetTypeConvertedValue.getInt(value));
      break;
    case UUType.U_TYPE_BIGINT:
      addLong(UGetTypeConvertedValue.getLong(value));
      break;
    case UUType.U_TYPE_SET:
    case UUType.U_TYPE_MULTISET:
    case UUType.U_TYPE_SEQUENCE:
      if (!(value instanceof CUBRIDArray))
        throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
      writeCollection((CUBRIDArray) value);
      break;
    case UUType.U_TYPE_OBJECT:
    {
      if (!(value instanceof CUBRIDOID))
      {
        throw new UJciException(UErrorCode.ER_TYPE_CONVERSION);
      }
      addOID((CUBRIDOID) value);
    }
      break;
    }
  }

  private void writeCollection(CUBRIDArray data)
  {
    int collection_size = 0;
    int collection_size_msg_buffer_cursor;
    int collection_size_msg_buffer_position;

    // writeInt(data.getLength() * 4 + data.getBytesLength() + 1);
    collection_size_msg_buffer_cursor = bufferCursor;
    collection_size_msg_buffer_position = positionInABuffer;

    writeInt(collection_size);

    writeByte((byte) data.getBaseType());
    collection_size++;

    switch (data.getBaseType())
    {
    case UUType.U_TYPE_BIT:
    case UUType.U_TYPE_VARBIT:
    {
      Object[] objectValues;
      byte[][] byteValues = null;
      objectValues = (Object[]) data.getArray();
      if (objectValues != null && objectValues instanceof byte[][])
        byteValues = (byte[][]) objectValues;
      else if (objectValues != null && objectValues instanceof Boolean[])
      {
        byteValues = new byte[objectValues.length][];
        for (int i = 0; i < byteValues.length; i++)
        {
          if (((Boolean[]) objectValues)[i] != null)
          {
            byteValues[i] = new byte[1];
            byteValues[i][0] = (((Boolean[]) objectValues)[i].booleanValue() == true) ? (byte) 1
                : (byte) 0;
          }
          else
            byteValues[i] = null;
        }
      }
      for (int i = 0; byteValues != null && i < byteValues.length; i++)
      {
        if (byteValues[i] == null)
          collection_size += addNull();
        else
          collection_size += addBytes(byteValues[i]);
      }
    }
      break;
    case UUType.U_TYPE_SHORT:
    {
      Number[] shortValues; // Byte[] or Short[]
      shortValues = (Number[]) data.getArray();
      for (int i = 0; shortValues != null && i < shortValues.length; i++)
      {
        if (shortValues[i] == null)
          collection_size += addNull();
        else
          collection_size += addShort(shortValues[i].shortValue());
      }
    }
      break;
    case UUType.U_TYPE_INT:
    {
      Integer[] intValues;
      intValues = (Integer[]) data.getArray();
      for (int i = 0; intValues != null && i < intValues.length; i++)
      {
        if (intValues[i] == null)
          collection_size += addNull();
        else
          collection_size += addInt(intValues[i].intValue());
      }
    }
      break;
    case UUType.U_TYPE_BIGINT:
    {
      Long[] longValues;
      longValues = (Long[]) data.getArray();
      for (int i = 0; longValues != null && i < longValues.length; i++)
      {
        if (longValues[i] == null)
          collection_size += addNull();
        else
          collection_size += addLong(longValues[i].longValue());
      }
    }
      break;
    case UUType.U_TYPE_FLOAT:
    {
      Float[] floatValues;
      floatValues = (Float[]) data.getArray();
      for (int i = 0; floatValues != null && i < floatValues.length; i++)
      {
        if (floatValues[i] == null)
          collection_size += addNull();
        else
          collection_size += addFloat(floatValues[i].floatValue());
      }
    }
      break;
    case UUType.U_TYPE_DOUBLE:
    case UUType.U_TYPE_MONETARY:
    {
      Double[] doubleValues;
      doubleValues = (Double[]) data.getArray();
      for (int i = 0; doubleValues != null && i < doubleValues.length; i++)
      {
        if (doubleValues[i] == null)
          collection_size += addNull();
        else
          collection_size += addDouble(doubleValues[i].doubleValue());
      }
    }
      break;
    case UUType.U_TYPE_NUMERIC:
    {
      String[] stringValues;
      BigDecimal tempBValues[] = (BigDecimal[]) data.getArray();
      stringValues = new String[(tempBValues != null) ? tempBValues.length : 0];
      for (int i = 0; stringValues != null && i < stringValues.length; i++)
      {
        if (tempBValues[i] == null)
          collection_size += addNull();
        else
        {
          stringValues[i] = tempBValues[i].toString();
          collection_size += addStringWithNull(stringValues[i]);
        }
      }
    }
      break;
    case UUType.U_TYPE_DATE:
    {
      Date[] dateValues;
      dateValues = (Date[]) data.getArray();
      for (int i = 0; dateValues != null && i < dateValues.length; i++)
      {
        if (dateValues[i] == null)
          collection_size += addNull();
        else
          collection_size += addDate(dateValues[i]);
      }
    }
      break;
    case UUType.U_TYPE_TIME:
    {
      Time[] timeValues;
      timeValues = (Time[]) data.getArray();
      for (int i = 0; timeValues != null && i < timeValues.length; i++)
      {
        if (timeValues[i] == null)
          collection_size += addNull();
        else
          collection_size += addTime(timeValues[i]);
      }
    }
      break;
    case UUType.U_TYPE_TIMESTAMP:
    case UUType.U_TYPE_DATETIME:
    {
      Timestamp[] timestampValues;
      timestampValues = (Timestamp[]) data.getArray();
      for (int i = 0; timestampValues != null && i < timestampValues.length; i++)
      {
        if (timestampValues[i] == null)
          collection_size += addNull();
        else
          collection_size += addTimestamp(timestampValues[i]);
      }
    }
      break;
    case UUType.U_TYPE_CHAR:
    case UUType.U_TYPE_NCHAR:
    case UUType.U_TYPE_STRING:
    case UUType.U_TYPE_VARNCHAR:
    {
      String[] stringValues;
      stringValues = (String[]) data.getArray();
      for (int i = 0; stringValues != null && i < stringValues.length; i++)
      {
        if (stringValues[i] == null)
          collection_size += addNull();
        else
          collection_size += addStringWithNull(stringValues[i]);
      }
    }
      break;
    case UUType.U_TYPE_OBJECT:
    {
      if (data.getLength() > 0)
      {
        CUBRIDOID oidValues[] = (CUBRIDOID[]) data.getArray();
        for (int i = 0; i < oidValues.length; i++)
        {
          if (oidValues[i] == null)
            collection_size += addNull();
          else
            collection_size += addOID(oidValues[i]);
        }
      }
    }
      break;
    case UUType.U_TYPE_NULL:
    default:
    {
      Object[] objectValues;
      objectValues = (Object[]) data.getArray();
      for (int i = 0; objectValues != null && i < objectValues.length; i++)
        collection_size += addNull();
    }
      break;
    }

    overwriteInt(collection_size, collection_size_msg_buffer_cursor,
        collection_size_msg_buffer_position);
  }

  private void overwriteInt(int data, int msg_buffer_cursor,
      int msg_buffer_position)
  {
    int saved_length = length;
    int saved_buffer_cursor = bufferCursor;
    int saved_position_buffer = positionInABuffer;

    bufferCursor = msg_buffer_cursor;
    ;
    positionInABuffer = msg_buffer_position;

    writeInt(data);

    length = saved_length;
    bufferCursor = saved_buffer_cursor;
    positionInABuffer = saved_position_buffer;
  }
  
  private void overwriteBytes(byte[] data, int msg_buffer_cursor,
      int msg_buffer_position)
  {
    int saved_length = length;
    int saved_buffer_cursor = bufferCursor;
    int saved_position_buffer = positionInABuffer;

    bufferCursor = msg_buffer_cursor;
    positionInABuffer = msg_buffer_position;

    writeBytes(data, 0, data.length);

    length = saved_length;
    bufferCursor = saved_buffer_cursor;
    positionInABuffer = saved_position_buffer;
  }

  private void writeInt(int data)
  {
    writeByte((byte) ((data >>> 24) & 0xFF));
    writeByte((byte) ((data >>> 16) & 0xFF));
    writeByte((byte) ((data >>> 8) & 0xFF));
    writeByte((byte) ((data >>> 0) & 0xFF));
  }

  private void writeByte(byte data)
  {
    length++;
    if (positionInABuffer >= DEFAULT_CAPACITY)
    {
      bufferCursor++;
      if (bufferCursor >= allocatedBufferSize)
      {
        buffer[bufferCursor] = new byte[DEFAULT_CAPACITY];
        allocatedBufferSize = bufferCursor + 1;
      }
      positionInABuffer = 0;
    }
    buffer[bufferCursor][positionInABuffer++] = data;
  }

  private void writeBoolean(boolean data)
  {
    if (data == true)
      writeByte((byte) 1);
    else
      writeByte((byte) 0);
  }

  private void writeBytes(byte data[], int pos, int len)
  {
    if (positionInABuffer + len <= DEFAULT_CAPACITY)
    {
      System.arraycopy(data, pos, buffer[bufferCursor], positionInABuffer, len);
      length += len;
      positionInABuffer += len;
      return;
    }

    for (int i = 0; i < len; i++)
    {
      writeByte(data[pos++]);
    }
  }

  private void writeDouble(double data)
  {
    writeLong(Double.doubleToLongBits(data));
  }

  private void writeLong(long data)
  {
    writeByte((byte) ((data >>> 56) & 0xFF));
    writeByte((byte) ((data >>> 48) & 0xFF));
    writeByte((byte) ((data >>> 40) & 0xFF));
    writeByte((byte) ((data >>> 32) & 0xFF));
    writeByte((byte) ((data >>> 24) & 0xFF));
    writeByte((byte) ((data >>> 16) & 0xFF));
    writeByte((byte) ((data >>> 8) & 0xFF));
    writeByte((byte) ((data >>> 0) & 0xFF));
  }

  private void writeShort(short data)
  {
    writeByte((byte) ((data >>> 8) & 0xFF));
    writeByte((byte) ((data >>> 0) & 0xFF));
  }

  private void writeDate(Date data) throws IllegalArgumentException
  {
    String stringData;
    SimpleDateFormat localFormat = new SimpleDateFormat("yyyy-MM-dd");
    stringData = localFormat.format((Date) data);
    writeShort(Short.parseShort(stringData.substring(0, 4)));
    writeShort(Short.parseShort(stringData.substring(5, 7)));
    writeShort(Short.parseShort(stringData.substring(8, 10)));
    writeShort((short) 0);
    writeShort((short) 0);
    writeShort((short) 0);
    writeShort((short) 0);
  }

  private void writeTime(Time data) throws IllegalArgumentException
  {
    String stringData;
    SimpleDateFormat localFormat;
    localFormat = new SimpleDateFormat("HH:mm:ss");
    stringData = localFormat.format((Time) data);
    writeShort((short) 0);
    writeShort((short) 0);
    writeShort((short) 0);
    writeShort(Short.parseShort(stringData.substring(0, 2)));
    writeShort(Short.parseShort(stringData.substring(3, 5)));
    writeShort(Short.parseShort(stringData.substring(6, 8)));
    writeShort((short) 0);
  }

  private void writeTimestamp(Timestamp data) throws IllegalArgumentException
  {
    String stringData;
    SimpleDateFormat localFormat;
    localFormat = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss");
    stringData = localFormat.format((Timestamp) data);
    writeShort(Short.parseShort(stringData.substring(0, 4)));
    writeShort(Short.parseShort(stringData.substring(5, 7)));
    writeShort(Short.parseShort(stringData.substring(8, 10)));
    writeShort(Short.parseShort(stringData.substring(11, 13)));
    writeShort(Short.parseShort(stringData.substring(14, 16)));
    writeShort(Short.parseShort(stringData.substring(17, 19)));
    writeShort((short) 0);
  }

  private void writeDatetime(Timestamp data) throws IllegalArgumentException
  {
    String stringData;
    SimpleDateFormat localFormat;
    localFormat = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS");
    stringData = localFormat.format((Timestamp) data);
    writeShort(Short.parseShort(stringData.substring(0, 4)));
    writeShort(Short.parseShort(stringData.substring(5, 7)));
    writeShort(Short.parseShort(stringData.substring(8, 10)));
    writeShort(Short.parseShort(stringData.substring(11, 13)));
    writeShort(Short.parseShort(stringData.substring(14, 16)));
    writeShort(Short.parseShort(stringData.substring(17, 19)));
    writeShort(Short.parseShort(stringData.substring(20, 23)));
  }

  private void writeFloat(float data)
  {
    writeInt(Float.floatToIntBits(data));
  }

  private void clearBuffer(OutputStream out)
  {
    byte[] jdbcinfo = new byte[4]; 
    /* clear jdbcinfo value, 
       this data will be over written by sendData() function */
    /* not defined yet */
    jdbcinfo[0] = 0;
    jdbcinfo[1] = 0;
    jdbcinfo[2] = 0;
    jdbcinfo[3] = 0;

    length = 0;
    bufferCursor = 0;
    positionInABuffer = 0;
    outStream = out;

    writeInt(0);
    writeBytes(jdbcinfo, 0, jdbcinfo.length);
  }
}
