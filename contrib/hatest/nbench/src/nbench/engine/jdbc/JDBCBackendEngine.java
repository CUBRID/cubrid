package nbench.engine.jdbc;
import nbench.common.*;
import nbench.engine.helper.ActionSpec;
import nbench.engine.helper.TrSpec;
import java.util.Map;
import java.util.List;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Properties;
import java.sql.*;
import org.apache.commons.dbcp.BasicDataSource;


/* -------------------------------------------------------------------------- */
public class JDBCBackendEngine implements BackendEngine 
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
BasicDataSource DS;
/* -------------------------------------------------------------------------- */
/* METHODS */
/* -------------------------------------------------------------------------- */
public JDBCBackendEngine() 
{
  this.status = STATUS_CREATED;
}
private NBenchException error(String message)
{
  return new NBenchException("[JDBC BE]" + message);
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
    DS = new BasicDataSource();
    DS.setDriverClassName(props.getProperty("driver_path"));
    DS.setUrl(props.getProperty("url"));
    DS.setUsername(props.getProperty("id"));
    DS.setPassword(props.getProperty("password"));
    //DS.setInitialSize(Integer.parseInt(props.getProperty("initialSize")));
    DS.setMaxActive(Integer.parseInt(props.getProperty("maxConnectionPool")));
    DS.setMinIdle(Integer.parseInt(props.getProperty("minIdle")));
    String pp = props.getProperty("poolPreparedStatements");
    if(pp != null && pp.toLowerCase().equals("true"))
    {
      DS.setPoolPreparedStatements(true);
      DS.setMaxOpenPreparedStatements(
	Integer.parseInt(props.getProperty("maxOpenPreparedstatements")));
    }
    DS.setDefaultAutoCommit(true);
  }
  catch (Exception e) {
    throw error(e.toString());
  }
  status = STATUS_CONSOLIDATED;
}
/**
 */
public JDBCBackendEngineClient createClient() throws NBenchException
{
  if(status != STATUS_CONSOLIDATED)
    throw error("invalid status");
  try 
  {
    return new JDBCBackendEngineClient(this, DS.getConnection());
  }
  catch (Exception e)
  {
    throw new NBenchException(e.toString());
  }
}
/* -------------------------------------------------------------------------- */
} /* BackendEngine */
/* -------------------------------------------------------------------------- */
