package cubrid.jdbc.driver;

import java.util.*;

abstract public class CUBRIDKeyTable {

/*=======================================================================
 |      CONSTANT VALUES
 =======================================================================*/

public static final String sessionKeyName = "CUBRIDConnectionKey";

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

public static void putValue(String key, CUBRIDConnectionKey value)
{
  synchronized (ht) {
    ht.put(key, value);
  }
}

public static void putValue(CUBRIDConnectionKey value)
{
  putValue(Thread.currentThread().getName(), value);
}

public static CUBRIDConnectionKey getValue()
{
  return (getValue(Thread.currentThread().getName()));
}

public static CUBRIDConnectionKey getValue(String key)
{
  synchronized (ht) {
    return ((CUBRIDConnectionKey) (ht.get(key)));
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
