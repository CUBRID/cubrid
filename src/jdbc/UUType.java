/**
 * Title:        CUBRID Java Client Interface<p>
 * Description:  CUBRID Java Client Interface<p>
 * @version 2.0
 */

package cubrid.jdbc.jci;

import java.math.BigDecimal;
import java.sql.Date;
import java.sql.Time;
import java.sql.Timestamp;
import cubrid.sql.CUBRIDOID;

/**
* CUBRID Data Type을 정의해 놓은 class이다.
*
* since 1.0
*/

abstract public class UUType {

/*=======================================================================
 |	PUBLIC CONSTANT VALUES
 =======================================================================*/

public static final int U_TYPE_MIN = 0;
public static final int U_TYPE_MAX = 19;

public static final byte U_TYPE_NULL = 0;
public static final byte U_TYPE_CHAR = 1;
public static final byte U_TYPE_STRING = 2;
public static final byte U_TYPE_VARCHAR = 2;
public static final byte U_TYPE_NCHAR = 3;
public static final byte U_TYPE_VARNCHAR = 4;
public static final byte U_TYPE_BIT = 5;
public static final byte U_TYPE_VARBIT = 6;
public static final byte U_TYPE_NUMERIC = 7;
public static final byte U_TYPE_DECIMAL = 7;
public static final byte U_TYPE_INT = 8;
public static final byte U_TYPE_SHORT = 9;
public static final byte U_TYPE_MONETARY = 10;
public static final byte U_TYPE_FLOAT = 11;
public static final byte U_TYPE_DOUBLE = 12;
public static final byte U_TYPE_DATE = 13;
public static final byte U_TYPE_TIME = 14;
public static final byte U_TYPE_TIMESTAMP = 15;
public static final byte U_TYPE_SET = 16;
public static final byte U_TYPE_MULTISET = 17;
public static final byte U_TYPE_SEQUENCE = 18;
public static final byte U_TYPE_OBJECT = 19;
public static final byte U_TYPE_RESULTSET = 20;

/*=======================================================================
 |	PUBLIC METHODS
 =======================================================================*/

static boolean isCollectionType(byte type)
{
  if (type == UUType.U_TYPE_SET ||
      type == UUType.U_TYPE_MULTISET ||
      type == UUType.U_TYPE_SEQUENCE)
  {
    return true;
  }
  return false;
}

static byte getObjArrBaseDBtype(Object values)
{
  if (values instanceof String[])
    return UUType.U_TYPE_VARCHAR;
  else if (values instanceof Byte[])
    return UUType.U_TYPE_SHORT;
  else if (values instanceof byte[][])
    return UUType.U_TYPE_VARBIT;
  else if (values instanceof Boolean[])
    return UUType.U_TYPE_BIT;
  else if (values instanceof Short[])
    return UUType.U_TYPE_SHORT;
  else if (values instanceof Integer[])
    return UUType.U_TYPE_INT;
  else if (values instanceof Double[])
    return UUType.U_TYPE_DOUBLE;
  else if (values instanceof Float[])
    return UUType.U_TYPE_FLOAT;
  else if (values instanceof BigDecimal[])
    return UUType.U_TYPE_NUMERIC;
  else if (values instanceof Date[])
    return UUType.U_TYPE_DATE;
  else if (values instanceof Time[])
    return UUType.U_TYPE_TIME;
  else if (values instanceof Timestamp[])
    return UUType.U_TYPE_TIMESTAMP;
  else if (values instanceof CUBRIDOID[])
    return UUType.U_TYPE_OBJECT;
  else
    return UUType.U_TYPE_NULL;
}

static byte getObjectDBtype(Object value)
{
  if (value == null)
    return UUType.U_TYPE_NULL;
  else if (value instanceof String)
    return UUType.U_TYPE_STRING;
  else if (value instanceof Byte)
    return UUType.U_TYPE_SHORT;
  else if (value instanceof byte[])
    return UUType.U_TYPE_VARBIT;
  else if (value instanceof Boolean)
    return UUType.U_TYPE_BIT;
  else if (value instanceof Short)
    return UUType.U_TYPE_SHORT;
  else if (value instanceof Integer)
    return UUType.U_TYPE_INT;
  else if (value instanceof Double)
    return UUType.U_TYPE_DOUBLE;
  else if (value instanceof Float)
    return UUType.U_TYPE_FLOAT;
  else if (value instanceof BigDecimal || value instanceof Long)
    return UUType.U_TYPE_NUMERIC;
  else if (value instanceof Date)
    return UUType.U_TYPE_DATE;
  else if (value instanceof Time)
    return UUType.U_TYPE_TIME;
  else if (value instanceof Timestamp)
    return UUType.U_TYPE_TIMESTAMP;
  else if (value instanceof CUBRIDOID)
    return UUType.U_TYPE_OBJECT;
  else if (value instanceof Object[])
    return UUType.U_TYPE_SEQUENCE;
  else
    return UUType.U_TYPE_NULL;
}

}  // end of class UUType
