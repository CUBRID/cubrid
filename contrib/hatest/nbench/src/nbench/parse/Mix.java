package nbench.parse;
import java.util.List;
import java.util.LinkedList;

public class Mix
{
  public Mix(String ss)
  {
    this.ss = ss;
    this.steps = new LinkedList<Step>();
  }
  public String ss;
  public List<Step> steps;
  public String toString()
  {
    StringBuffer sb = new StringBuffer();
    sb.append("ss="+ss+"steps={");
    for(Object l : steps)
      sb.append(l+",");
    sb.append("}\n");
    return sb.toString();
  }
}
