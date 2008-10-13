package cubrid.jdbc.driver;

import java.util.*;

abstract public class CUBRIDJdbcInfoTable {

/*=======================================================================
 |      CONSTANT VALUES
 =======================================================================*/

/*=======================================================================
 |      PRIVATE VARIABLES
 =======================================================================*/

static private Hashtable ht;

/*=======================================================================
 |      PUBLIC METHODS
 =======================================================================*/

static {
  ht = new Hashtable();
}

public static void putValue(String key, String value)
{
  synchronized (ht) {
    ht.put(key, value);
  }
}

public static void putValue(String value)
{
  putValue(Thread.currentThread().getName(), value);
}

public static String getValue()
{
  return (getValue(Thread.currentThread().getName()));
}

public static String getValue(String key)
{
  synchronized (ht) {
    return ((String) (ht.get(key)));
  }
}

public static void removeValue(String key)
{
  synchronized (ht) {
    ht.remove(key);
  }
}

public static void removeValue()
{
  removeValue(Thread.currentThread().getName());
}

}  // end of class CUBRIDKeyTable
