package nbench.common;
import java.sql.Types;

public class ValueType
{
  public final static int INT       = Types.INTEGER;
  public final static int STRING    = Types.VARCHAR;
  public final static int TIMESTAMP = Types.TIMESTAMP;
  public final static int NUMERIC   = Types.NUMERIC;


  public static String getJavaType(int t)
  {
    switch(t)
    {
    case ValueType.INT:
      return "java.lang.Integer";
    case ValueType.STRING:
      return "java.lang.String";
    case ValueType.TIMESTAMP:
      return "java.sql.Timestamp";
    case ValueType.NUMERIC:
      return "java.math.BigDecimal";
    default:
      return null;
    }
  }
}
