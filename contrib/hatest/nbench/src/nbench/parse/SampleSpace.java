package nbench.parse;
import nbench.common.NameAndType;
import java.util.Map;
import java.util.HashMap;

public class SampleSpace
{
  public SampleSpace(String name)
  {
    this.name = name;
    S = new HashMap<NameAndType,String>();
  }
  public String name; //sample space name
  public Map<NameAndType, String> S; 
  public String toString()
  {
    StringBuffer sb = new StringBuffer();
    sb.append("name="+name+"\n");
    for(NameAndType s : S.keySet())
	sb.append("\t"+s+"="+S.get(s)+"\n");
    return sb.toString();
  }
}
