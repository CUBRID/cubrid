package nbench.parser;

import nbench.common.NameAndType;
import nbench.common.*;

public class PNAT implements NameAndType {

    private String name;
    private int type;
    public PNAT(String name, String type)
    {
      this.name = name;
      if(type == null)
	this.type = ValueType.STRING;
      else
	this.type = TypeMap.typeIdOf(type);
    }
    /* nbench.common.NameAndType implementation */
    public String getName() { return name; }
    public int getType() { return type; }
    public String toString() { return name + ":" + type;}
}
