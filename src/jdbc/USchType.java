
/**
 * Title:        CUBRID Java Client Interface<p>
 * Description:  CUBRID Java Client Interface<p>
 * @version 2.0
 */

package cubrid.jdbc.jci;

/**
* CUBRID의 Schema type을 정의하는 class이다.
*
* since 1.0
*/

abstract public class USchType {
  public static final int SCH_MIN = 1;
  public static final int SCH_MAX = 15;

  public static final int SCH_CLASS = 1;
  public static final int SCH_VCLASS = 2;
  public static final int SCH_QUERY_SPEC = 3;
  public static final int SCH_ATTRIBUTE = 4;
  public static final int SCH_CLASS_ATTRIBUTE = 5;
  public static final int SCH_METHOD = 6;
  public static final int SCH_CLASS_METHOD = 7;
  public static final int SCH_METHOD_FILE = 8;
  public static final int SCH_SUPERCLASS = 9;
  public static final int SCH_SUBCLASS = 10;
  public static final int SCH_CONSTRAIT = 11;
  public static final int SCH_TRIGGER = 12;
  public static final int SCH_CLASS_PRIVILEGE = 13;
  public static final int SCH_ATTR_PRIVILEGE = 14;
  public static final int SCH_DIRECT_SUPER_CLASS = 15;
}

