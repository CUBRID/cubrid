package nbench.parse;
import java.util.Map;
import java.util.List;

public class WorkLoad
{
  public String name;
  public Map<String, SampleSpace> ss;
  public Map<String, Transaction> trs;
  public List<Mix> mixes;

  public String toString()
  {
    StringBuffer sb = new StringBuffer();
    sb.append("name = " + name + "\n");

    sb.append("========== ss ==========\n");
    for(Object o : ss.keySet())
	sb.append(ss.get(o) + "\n");

    sb.append("========== trs ==========\n");
    for(Object o : trs.keySet())
	sb.append(trs.get(o) + "\n");

    sb.append("========== mixes ==========\n");
    for(Object o : mixes)
	sb.append(o.toString()+"\n");
    return sb.toString();
  }
}
