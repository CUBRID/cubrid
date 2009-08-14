package nbench.engine.cci;
import nbench.common.*;
import nbench.engine.helper.ActionSpec;
import nbench.engine.helper.TrSpec;
import java.util.Map;
import java.util.List;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Properties;

/* -------------------------------------------------------------------------- */
public class CCIBackendEngine implements BackendEngine 
/* -------------------------------------------------------------------------- */
{
/* -------------------------------------------------------------------------- */
/* FIELDS */
/* -------------------------------------------------------------------------- */
//state
private Properties props;
private static int STATUS_CREATED = 0;
private static int STATUS_CONFIGURED = 1;
private static int STATUS_CONSOLIDATED = 2;
private int status;
HashMap<String, TrSpec> prepared_map;
HashMap<String, List<ActionSpec>> action_specs_map;
//TODO CCI SPECIFIC HERE...
CCIDataSource DS;
/* -------------------------------------------------------------------------- */
/* METHODS */
/* -------------------------------------------------------------------------- */
public CCIBackendEngine() 
{
  this.status = STATUS_CREATED;
}
private NBenchException error(String message)
{
  return new NBenchException("[CCI BE]" + message);
}
private void loadActionSpecs(String path) throws NBenchException
{
  action_specs_map = ActionSpec.load(path);
}
/**
 */
public void configure (Properties props) throws NBenchException
{
  this.props = props;
  if(status != STATUS_CREATED)
    throw error("invalid status");
  prepared_map = new HashMap<String, TrSpec>();
  loadActionSpecs(props.getProperty("actions"));
  status = STATUS_CONFIGURED;
}
/**
 */
public void
prepareForStatement(String name, List<NameAndType> in, List<NameAndType> out)
throws NBenchException
{
  if(status != STATUS_CONFIGURED)
    throw error("invalid status");
  List<ActionSpec> actions = action_specs_map.get(name);
  if(actions == null)
    throw error("no action is specified for action:" + name);
  TrSpec pre = new TrSpec(name, in, out, actions);
  prepared_map.put(name, pre);
}
/**
 */
public void
consolidateForRun()
throws NBenchException 
{ 
  try {
    String driver_path = props.getProperty("driver_path");
    String ip = props.getProperty("ip");
    int port = Integer.valueOf(props.getProperty("port"));
    String dbname = props.getProperty("dbname");
    String dbuser = props.getProperty("dbuser");
    String dbpassword = props.getProperty("dbpassword");
    DS = new CCIDataSource(driver_path, ip, port, dbname, dbuser, dbpassword);
  }
  catch (Exception e) {
    e.printStackTrace();
    throw error(e.toString());
  }
  status = STATUS_CONSOLIDATED;
}
/**
 */
public BackendEngineClient createClient() throws NBenchException
{
  if(status != STATUS_CONSOLIDATED)
    throw error("invalid status");
  try 
  {
    return new CCIBackendEngineClient(this, DS.getConnection());
  }
  catch (Exception e)
  {
    throw new NBenchException(e.toString());
  }
}
/* -------------------------------------------------------------------------- */
} /* BackendEngine */
/* -------------------------------------------------------------------------- */
