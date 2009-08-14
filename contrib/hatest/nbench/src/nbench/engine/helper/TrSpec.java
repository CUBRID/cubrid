package nbench.engine.helper;
import nbench.common.*;
import java.util.List;
import java.util.ArrayList;

public class TrSpec
{
  public String name;
  public List<NameAndType> in;
  public List<NameAndType> out;
  public List<ActionSpec> action_specs;
  public TrSpec(String name, List<NameAndType> in, 
	List<NameAndType> out, List<ActionSpec> action_specs)
  {
    this.name = name;
    this.in= in;
    this.out= out;
    this.action_specs = action_specs;
  }
}
