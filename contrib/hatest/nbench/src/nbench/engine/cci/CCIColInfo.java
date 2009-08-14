package nbench.engine.cci;

public class CCIColInfo
{
  /* from (xdbms/jdbc/src/jci/UUType.java) */
  public static final int U_TYPE_MIN = 0;
  public static final int U_TYPE_MAX = 19;
  public static final byte U_TYPE_NULL = 0;             //not supported
  public static final byte U_TYPE_CHAR = 1;             //not supported
  public static final byte U_TYPE_STRING = 2;           //java.lang.String
  public static final byte U_TYPE_VARCHAR = 2;          //java.lang.String
  public static final byte U_TYPE_NCHAR = 3;            //java.lang.String
  public static final byte U_TYPE_VARNCHAR = 4;         //java.lang.String
  public static final byte U_TYPE_BIT = 5;              //not supported
  public static final byte U_TYPE_VARBIT = 6;           //not supported
  public static final byte U_TYPE_NUMERIC = 7;          //java.math.BigDecimal
  public static final byte U_TYPE_DECIMAL = 7;          //not supported
  public static final byte U_TYPE_INT = 8;              //java.lang.Integer
  public static final byte U_TYPE_SHORT = 9;            //not supported
  public static final byte U_TYPE_MONETARY = 10;        //not supported
  public static final byte U_TYPE_FLOAT = 11;           //not supported
  public static final byte U_TYPE_DOUBLE = 12;          //not supported
  public static final byte U_TYPE_DATE = 13;            //not supported
  public static final byte U_TYPE_TIME = 14;            //not supported
  public static final byte U_TYPE_TIMESTAMP = 15;       //java.sql.Timestamp
  public static final byte U_TYPE_SET = 16;             //not supported
  public static final byte U_TYPE_MULTISET = 17;        //not supported
  public static final byte U_TYPE_SEQUENCE = 18;        //not supported
  public static final byte U_TYPE_OBJECT = 19;          //not supported
  public static final byte U_TYPE_RESULTSET = 20;       //not supported
  /* column info mation */
  public int index;
  public int u_type;
  public boolean is_non_null;
  public String col_name;
  public String real_attr;
}
