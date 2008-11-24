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

import java.io.IOException;
import java.io.InputStream;
import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;
import cubrid.sql.CUBRIDOID;
import cubrid.jdbc.driver.CUBRIDXid;
import cubrid.jdbc.driver.CUBRIDConnection;

/*
 * Performance와 robustness를 위해 server로부터 받을 message를 한꺼번에 buffer에
 * 받아놓고 buffer에서 data들을 manage할 수 있게 해주는 class이다.
 * Java.io.InputStream reference를 member로 가지고 있다. 특히 이 class는 Input
 * Data Size를 Check하여 맞지 않을 경우 UJciException을 throw한다.
 *
 * since 1.0
 */

class UInputBuffer
{
  private InputStream input;
  private int position;
  private int capacity;
  private byte buffer[];
  private int resCode;

  UInputBuffer(InputStream relatedI) throws IOException, UJciException
  {
    input = relatedI;
    position = 0;

    byte[] byteData = new byte[4];
    input.read(byteData);

    capacity = UJCIUtil.bytes2int(byteData, 0);

    if (capacity < 0)
    {
      capacity = 0;
      return;
    }

    buffer = new byte[capacity];
    readData();

    resCode = readInt();
    if (resCode < 0)
    {
      String msg = readString(remainedCapacity(), UJCIManager.sysCharsetName);
      throw new UJciException(UErrorCode.ER_DBMS, resCode, msg);
    }
  }

  int getResCode()
  {
    return resCode;
  }

  /* buffer로부터 1 byte를 읽어 return한다. */

  byte readByte() throws UJciException
  {
    if (position >= capacity)
      throw new UJciException(UErrorCode.ER_ILLEGAL_DATA_SIZE);

    return buffer[position++];
  }

  void readBytes(byte value[], int offset, int len) throws UJciException
  {
    if (value == null)
      return;

    if (position + len > capacity)
    {
      throw new UJciException(UErrorCode.ER_ILLEGAL_DATA_SIZE);
    }

    System.arraycopy(buffer, position, value, offset, len);
    position += len;
  }

  void readBytes(byte value[]) throws UJciException
  {
    readBytes(value, 0, value.length);
  }

  byte[] readBytes(int size) throws UJciException
  {
    byte[] value = new byte[size];
    readBytes(value, 0, size);
    return value;
  }

  /* buffer로부터 double값을 읽어 return하여 준다. */

  double readDouble() throws UJciException
  {
    return Double.longBitsToDouble(readLong());
  }

  /* buffer로부터 float값을 읽어 return하여 준다. */

  float readFloat() throws UJciException
  {
    return Float.intBitsToFloat(readInt());
  }

  /* buffer로부터 int값을 읽어 return하여 준다. */

  int readInt() throws UJciException
  {
    if (position + 4 > capacity)
    {
      throw new UJciException(UErrorCode.ER_ILLEGAL_DATA_SIZE);
    }

    int data = UJCIUtil.bytes2int(buffer, position);
    position += 4;

    return data;
  }

  /*
   * buffer로부터 long값을 읽어 return하여 준다. CUBRID은 long type이 없으므로
   * 단지 double값을 읽기 위해서만 사용되어 진다.
   */

  long readLong() throws UJciException
  {
    long data = 0;

    if (position + 8 > capacity)
    {
      throw new UJciException(UErrorCode.ER_ILLEGAL_DATA_SIZE);
    }

    for (int i = 0; i < 8; i++)
    {
      data <<= 8;
      data |= (buffer[position++] & 0xff);
    }

    return data;
  }

  /* buffer로부터 short값을 읽어 return해 준다. */

  short readShort() throws UJciException
  {
    if (position + 2 > capacity)
    {
      throw new UJciException(UErrorCode.ER_ILLEGAL_DATA_SIZE);
    }

    short data = UJCIUtil.bytes2short(buffer, position);
    position += 2;

    return data;
  }

  /* buffer로부터 size만큼의 String값을 읽어 String object를 return해 준다. */

  String readString(int size, String charsetName) throws UJciException
  {
    String stringData;

    if (size <= 0)
      return null;

    if (position + size > capacity)
    {
      throw new UJciException(UErrorCode.ER_ILLEGAL_DATA_SIZE);
    }

    try
    {
      stringData = new String(buffer, position, size - 1, charsetName);
    }
    catch (java.io.UnsupportedEncodingException e)
    {
      stringData = new String(buffer, position, size - 1);
    }

    position += size;

    return stringData;
  }

  Date readDate() throws UJciException
  {
    int year, month, day;
    year = readShort();
    month = readShort();
    day = readShort();

    String dateStr = "";
    if (year < 10)
      dateStr += "000" + year + "-";
    else if (year < 100)
      dateStr += "00" + year + "-";
    else if (year < 1000)
      dateStr += "0" + year + "-";
    else
      dateStr += year + "-";
    if (month < 10)
      dateStr += "0" + month + "-";
    else
      dateStr += month + "-";
    if (day < 10)
      dateStr += "0" + day;
    else
      dateStr += day;

    return (Date.valueOf(dateStr));
  }

  Time readTime() throws UJciException
  {
    int hour, minute, second;
    hour = readShort();
    minute = readShort();
    second = readShort();

    String timeStr = "";
    if (hour < 10)
      timeStr += "0" + hour + ":";
    else
      timeStr += hour + ":";
    if (minute < 10)
      timeStr += "0" + minute + ":";
    else
      timeStr += minute + ":";
    if (second < 10)
      timeStr += "0" + second;
    else
      timeStr += second;

    return (Time.valueOf(timeStr));
  }

  Timestamp readTimestamp() throws UJciException
  {
    int year, month, day, hour, minute, second;
    year = readShort();
    month = readShort();
    day = readShort();
    hour = readShort();
    minute = readShort();
    second = readShort();

    String tsStr = "";
    if (year < 10)
      tsStr += "000" + year + "-";
    else if (year < 100)
      tsStr += "00" + year + "-";
    else if (year < 1000)
      tsStr += "0" + year + "-";
    else
      tsStr += year + "-";
    if (month < 10)
      tsStr += "0" + month + "-";
    else
      tsStr += month + "-";
    if (day < 10)
      tsStr += "0" + day + " ";
    else
      tsStr += day + " ";
    if (hour < 10)
      tsStr += "0" + hour + ":";
    else
      tsStr += hour + ":";
    if (minute < 10)
      tsStr += "0" + minute + ":";
    else
      tsStr += minute + ":";
    if (second < 10)
      tsStr += "0" + second;
    else
      tsStr += second;

    return (Timestamp.valueOf(tsStr));
  }

  CUBRIDOID readOID(CUBRIDConnection con) throws UJciException
  {
    byte[] oid = readBytes(UConnection.OID_BYTE_SIZE);
    for (int i = 0; i < oid.length; i++)
    {
      if (oid[i] != (byte) 0)
      {
        return (new CUBRIDOID(con, oid));
      }
    }
    return null;
  }

  /*
   * buffer에 아직 읽지 않고 남아있는 Data의 length를 return해 준다. CAS로부터
   * error message를 읽어들이기 위해 사용되어 진다.
   */

  int remainedCapacity()
  {
    return capacity - position;
  }

  CUBRIDXid readXid() throws UJciException
  {
    int msg_size = readInt();
    int formatId = readInt();
    int gid_size = readInt();
    int bid_size = readInt();
    byte[] gid = readBytes(gid_size);
    byte[] bid = readBytes(bid_size);

    return (new CUBRIDXid(formatId, gid, bid));
  }

  private void readData() throws IOException
  {
    int realRead = 0, tempRead = 0;
    while (realRead < capacity)
    {
      tempRead = input.read(buffer, realRead, capacity - realRead);
      if (tempRead < 0)
      {
        capacity = realRead;
        break;
      }
      realRead += tempRead;
    }
  }
}
