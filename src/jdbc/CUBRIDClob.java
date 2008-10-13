package cubrid.jdbc.driver;

import java.sql.*;
import java.io.*;

import cubrid.jdbc.driver.CUBRIDException;
import cubrid.jdbc.driver.CUBRIDJDBCErrorCode;

import cubrid.sql.CUBRIDOID;

public class CUBRIDClob extends CUBRIDOID implements Clob {

/*=======================================================================
 |      CONSTANT VALUES
 =======================================================================*/

final static int READ_CHAR_BUFFER_SIZE = 8192;
private final static int READ_BYTE_BUFFER_SIZE = READ_CHAR_BUFFER_SIZE + 1;

/*=======================================================================
 |      PRIVATE VARIABLES
 =======================================================================*/

private String charsetName;
private StringBuffer clobData;
private int clobByteLength;
private int clobLength;
private int gloNextReadPos;
private int gloPos;
private int clobPos;
private byte[] byteBuffer;
private boolean end_of_glo;

/*=======================================================================
 |      CONSTRUCTOR
 =======================================================================*/

public CUBRIDClob(CUBRIDOID o, String charsetName)
{
  super(o);
  this.charsetName = charsetName;
  clobData = new StringBuffer("");
  gloNextReadPos = 1;
  clobPos = 0;
  end_of_glo = false;
  clobLength = -1;
  clobByteLength = -1;
  byteBuffer = new byte[READ_BYTE_BUFFER_SIZE];
}

/*=======================================================================
 |      java.sql.Clob interface
 =======================================================================*/

public synchronized long length() throws SQLException
{
  read_clob_part(Integer.MAX_VALUE, 1);
  return clobLength;
}

public synchronized String getSubString(long pos, int length)
	throws SQLException
{
  if (pos < 1)
    throw new IndexOutOfBoundsException();
  if (length <= 0)
    return "";

  int read_len = read_clob_part((int) pos, length);
  return (clobData.substring(0, read_len));
}

public synchronized Reader getCharacterStream() throws SQLException
{
  return (new CUBRIDGloReader(this));
}

public InputStream getAsciiStream() throws SQLException
{
  return (new CUBRIDGloInputStream(this));
}

public synchronized long position(String searchstr, long start)
	throws SQLException
{
  byte[] b = string2bytes(searchstr);
  return (gloBinarySearch(start, b, 0, b.length));
}

public synchronized long position(Clob searchClob, long start)
	throws SQLException
{
  String searchstr = searchClob.getSubString(1, GLO_MAX_SEARCH_LEN);
  return position(searchstr, start);
}

public synchronized int setString(long pos, String str) throws SQLException
{
  if (str == null || str.length() <= 0)
    return 0;

  if (pos < 1)
    throw new IndexOutOfBoundsException();

  int read_len = read_clob_part((int) pos, str.length());

  byte[] bData = string2bytes(str);

  if (read_len < str.length()) {
    if (read_len > 0) {
      gloTruncate(gloPos);
    }

    clobData.setLength(0);
    clobByteLength = gloPos - 1;
    clobLength = clobPos - 1;
    gloNextReadPos = gloPos;

    int append_len = (int) pos - clobPos - clobData.length();
    int glo_start_pos = clobByteLength + append_len + 1;
    gloWrite(glo_start_pos, bData, 0, bData.length);

    clobByteLength = glo_start_pos + bData.length - 1;
    clobLength += (append_len + str.length());
  }
  else {
    int old_byte_len = string2bytes(clobData.substring(0, str.length())).length;

    if (old_byte_len < bData.length) {
      gloWrite(gloPos, bData, 0, old_byte_len);
      gloInsert(gloPos + old_byte_len, bData, old_byte_len, bData.length - old_byte_len);
    }
    else {
      int delete_len = old_byte_len - bData.length;
      gloWrite(gloPos, bData, 0, bData.length);
      if (delete_len > 0) {
	gloDelete(gloPos + bData.length, delete_len);
      }
    }

    clobData.setLength(0);
    clobData.append(str);
    gloNextReadPos = gloPos + bData.length;
  }

  return str.length();
}

public synchronized int setString(long pos, String str, int offset, int len)
	throws SQLException
{
  if (offset < 0)
    throw new IndexOutOfBoundsException();

  if (str == null || len <= 0)
    return 0;

  if (offset + len >= str.length()) {
    if (offset != 0)
      str = str.substring(offset);
  }
  else {
    str = str.substring(offset, len);
  }

  return (setString(pos, str));
}

public synchronized OutputStream setAsciiStream(long pos) throws SQLException
{
  int glo_start_pos;;

  int read_len = read_clob_part((int) pos, 10);

  if (read_len <= 0) {
    glo_start_pos = clobByteLength + (int) pos - clobPos - clobData.length() + 1;
  }
  else {
    glo_start_pos = gloPos;
  }

  return (new CUBRIDGloOutputStream(this, glo_start_pos));
}

public synchronized Writer setCharacterStream(long pos) throws SQLException
{
  return (new CUBRIDGloWriter(this, (int) pos));
}

public synchronized void truncate(long len) throws SQLException
{
  if (len < 0)
    return;

  if (read_clob_part((int) len, 1) <= 0)
    return;

  clobData.setLength(1);
  clobByteLength = gloPos - 1 + string2bytes(clobData.toString()).length;
  gloNextReadPos = clobByteLength + 1;
  clobLength = clobPos;

  gloTruncate(clobByteLength);
}

/*=======================================================================
 |      PACKAGE ACCESS METHODS
 =======================================================================*/

synchronized int getChars(int pos, int length, char[] cbuf, int start)
	throws SQLException
{
  if (length <= 0)
    return 0;

  int read_len = read_clob_part(pos, length);

  clobData.getChars(0, read_len, cbuf, start);
  return read_len;
}

/*=======================================================================
 |      PRIVATE METHODS
 =======================================================================*/

private int read_clob_part(int pos, int length) throws SQLException
{
  if (clobLength >= 0 && pos > clobLength) {
    gloPos = gloNextReadPos = clobByteLength + 1;
    clobPos = clobLength + 1;
    clobData.setLength(0);
    return 0;
  }

  if (clobPos == 0 || pos < clobPos) {
    gloPos = gloNextReadPos = 1;
    clobPos = 1;
    clobData.setLength(0);
    glo_read();
  }

  while (pos >= clobPos + clobData.length()) {
    gloPos = gloNextReadPos;
    clobPos += clobData.length();
    clobData.setLength(0);
    if (end_of_glo)
      return 0;
    glo_read();
  }

  int delete_len = pos - clobPos;
  if (delete_len > 0) {
    clobPos = pos;
    gloPos += string2bytes(clobData.substring(0, delete_len)).length;
    clobData.delete(0, delete_len);
  }

  while (length > clobData.length()) {
    if (end_of_glo)
      return clobData.length();
    glo_read();
  }

  return length;
}

private void glo_read() throws SQLException
{
  int read_len = gloRead(gloNextReadPos, READ_BYTE_BUFFER_SIZE, byteBuffer, 0);

  gloNextReadPos += read_len;

  if (read_len < READ_BYTE_BUFFER_SIZE)
    end_of_glo = true;
  else
    end_of_glo = false;

  StringBuffer sb = new StringBuffer(bytes2string(byteBuffer, 0, read_len));

  if (end_of_glo == true) {
    clobLength = clobPos + clobData.length() + sb.length() - 1;
    clobByteLength = gloNextReadPos - 1;
  }
  else {
    gloNextReadPos -= string2bytes(sb.substring(sb.length() - 1)).length;
    sb.setLength(sb.length() - 1);
  }

  clobData.append(sb);
}

private byte[] string2bytes(String s) throws SQLException
{
  try {
    return (s.getBytes(charsetName));
  } catch (UnsupportedEncodingException e) {
    throw new CUBRIDException(CUBRIDJDBCErrorCode.unknown, e.getMessage());
  }
}

private String bytes2string(byte[] b, int start, int len) throws SQLException
{
  try {
    return (new String(b, start, len, charsetName));
  } catch (UnsupportedEncodingException e) {
    throw new CUBRIDException(CUBRIDJDBCErrorCode.unknown, e.getMessage());
  }
}

} // end of class CUBRIDClob
