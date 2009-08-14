package nbench.common;
import java.util.Map;
import java.util.List;
import nbench.report.NStat;

public interface BackendEngineClient
{
  List<VariableScope> 
  execute(String name, VariableScope in) throws NBenchException;
}
