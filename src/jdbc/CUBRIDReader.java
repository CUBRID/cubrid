package cubrid.jdbc.driver;

import java.io.*;


/**
 * Title:        CUBRID JDBC Driver
 * Description:
 * @version 2.0
 */

class CUBRIDReader extends Reader {

/*=======================================================================
 |      PRIVATE VARIABLES 
 =======================================================================*/

private int position;
private String valueBuffer;

/*=======================================================================
 |      CONSTRUCTOR
 =======================================================================*/

CUBRIDReader(String v)
{
  valueBuffer = v;
  position = 0;
}

/*=======================================================================
 |      PUBLIC METHODS
 =======================================================================*/

public synchronized int available() throws java.io.IOException
{
  if (valueBuffer == null) return 0;
  return valueBuffer.length() - position;
}

public synchronized int read(char[] cbuf, int off, int len) throws java.io.IOException
{
  if (cbuf == null)
    throw new NullPointerException();
  else if (off < 0 || off > cbuf.length || len < 0 ||
	   off + len > cbuf.length || off + len < 0)
    throw new IndexOutOfBoundsException();
  else if (len == 0)
    return 0;

  if (valueBuffer == null) return -1;

  int i;
  for (i=position ; i<len+position && i<valueBuffer.length() ; i++) {
    cbuf[i-position+off] = valueBuffer.charAt(i);
  }

  int temp = position;
  position = i;
  if (position == valueBuffer.length()) close();

  return i-temp;
}

public synchronized void close() throws java.io.IOException
{
  valueBuffer = null;
}

public boolean ready()
{
  return true;
}

}  // end of class CUBRIDReader
