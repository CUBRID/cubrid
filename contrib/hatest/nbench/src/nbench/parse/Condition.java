package nbench.parse;
import java.util.List;
import java.util.LinkedList;

public class Condition
{
  public Condition()
  {
    conds = new LinkedList<ConditionItem>();
  }
  public List<ConditionItem> conds;
  public String toString()
  {
    StringBuffer sb = new StringBuffer();
    for(ConditionItem i : conds)
      sb.append("\t" + i + "\n");
    return sb.toString();
  }
}
