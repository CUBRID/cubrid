package nbench.parse;
import nbench.common.*;
import java.util.List;
import java.util.Map;

public class Transaction
{
  public Transaction(String name) { this.name = name; }
  public String name;
  public String description;
  /* pre condition */
  public Condition cond;
  /* for input */
  public List<NameAndType> input_args;
  public List<String> input_exprs;
  /* for output */
  public List<NameAndType> output_cols;
  public Map<NameAndType, String> export_map;
  public String toString()
  {
    StringBuffer sb = new StringBuffer();
    sb.append("name=" + name + "\n");
    sb.append("description=" + description + "\n");
    sb.append("condition=\n");
    if(cond != null)
      sb.append(cond);
    sb.append("inputs=\n");
    for(int i = 0; i < input_args.size(); i++)
      sb.append("\t" + input_args.get(i) + ",expr=" + input_exprs.get(i)+"\n");
    sb.append("outputs=\n");
    for (NameAndType s : output_cols)
      {
	String expname = export_map.get(s);
	sb.append("\t" + s);
	if(expname != null)
	  sb.append("(exported as[" + expname + "])\n");
	else
	  sb.append("\n");
      }
    return sb.toString();
  }
}
