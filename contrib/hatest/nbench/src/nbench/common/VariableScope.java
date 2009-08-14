package nbench.common;
import java.util.Set;

public interface VariableScope
{
  Variable getVariable(String name);
  Set<String> getVariableNames();
}
