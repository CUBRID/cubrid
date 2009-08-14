package nbench.parser;
import java.util.HashMap;
import nbench.common.ValueType;

public class TypeMap
{
  private static HashMap<String, Integer> db_type_map = null;
  static {
    db_type_map = new HashMap<String, Integer>();
    db_type_map.put("INT", new Integer(ValueType.INT));
    db_type_map.put("STRING", new Integer(ValueType.STRING));
    db_type_map.put("TIMESTAMP", new Integer(ValueType.TIMESTAMP));
    db_type_map.put("NUMERIC", new Integer(ValueType.NUMERIC));
  }
  public static int typeIdOf(String name)
  {
    if(name == null)
      name = "STRING";
    Integer iv = db_type_map.get(name.toUpperCase());
    if(iv != null)
      return iv.intValue();
    else
      return 0; //TODO throw exception.
  }
}

