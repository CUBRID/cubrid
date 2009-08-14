package nbench.common;
import java.util.List;
import java.util.Properties;

//
// backend engine requirement.. 
//
public interface BackendEngine
{
  void 
  configure (Properties props) 
  throws NBenchException;

  void 
  prepareForStatement(String name,List<NameAndType> in, List<NameAndType> out) 
  throws NBenchException; 

  void
  consolidateForRun()
  throws NBenchException;

  BackendEngineClient 
  createClient() 
  throws NBenchException;
}
