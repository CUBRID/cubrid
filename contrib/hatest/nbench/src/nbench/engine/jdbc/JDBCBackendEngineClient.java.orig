package nbench.engine.jdbc;
import nbench.common.*;
import nbench.common.helper.NValue;
import nbench.common.helper.MapVariableScope;
import nbench.engine.helper.ActionSpec;
import nbench.engine.helper.TrSpec;
import java.util.*;
import java.math.BigDecimal;
import java.sql.*;

class JDBCBackendEngineClient implements BackendEngineClient 
{
  private JDBCBackendEngine be;
  private Connection conn;
  private HashMap<ActionSpec, PreparedStatement> action_prepared_map;
  /**
   */
  public JDBCBackendEngineClient(JDBCBackendEngine be, Connection conn)
  throws NBenchException
  {
    this.be = be;
    this.conn = conn;
    action_prepared_map = new HashMap<ActionSpec, PreparedStatement>();
  }
  /**
   */
  private PreparedStatement lazy_get_pstmt(ActionSpec as)
  throws NBenchException
  {
    PreparedStatement pstmt = action_prepared_map.get(as);
    if(pstmt == null)
    {
      try {
	pstmt = conn.prepareStatement(as.pstmt_str);
      }
      catch (Exception e) {
	throw error(e.toString());
      }
      action_prepared_map.put(as, pstmt);
    }
    return pstmt;
  }
  /**
   */
  private NBenchException error(String s)
  {
    return new NBenchException("[SQL JDBC BEC]" + s);
  }
  /**
   */
  VariableScope res2vs (List<NameAndType> out, ResultSet r)
  throws Exception
  {
    HashMap<String, Object> map = new HashMap<String, Object>();
    for(NameAndType nat : out)
    {
      String name = nat.getName();
      Object obj;
      switch(nat.getType())
      {
      case ValueType.INT:
	obj = new Integer(r.getInt(name));
	break;
      case ValueType.STRING:
	obj = r.getString(name);
	break;
      case ValueType.TIMESTAMP:
	obj = r.getTimestamp(name);
	break;
      case ValueType.NUMERIC:
	obj = r.getBigDecimal(name);
	break;
      default:
	throw error("unsupported type" + nat.getType());
      }
      if(obj != null && map.get(name) == null)
      {
	map.put(name, obj);
      }
    }
    VariableScope retvs = new MapVariableScope(map);
    return retvs;
  }
  /**
   */
  public  List<VariableScope>
  execute(String name, VariableScope in) throws NBenchException
  {
    TrSpec spec = be.prepared_map.get(name);
    boolean autocommitable = (spec.action_specs.size() == 1);
    boolean dirty = false;
    List<VariableScope> rmap = new ArrayList<VariableScope>();
    int ret_val;
    try 
    {
    conn.setAutoCommit(false);
//      if(autocommitable == false)
//      {
//	conn.setAutoCommit(false);
//      }
      for(ActionSpec as : spec.action_specs)
      { 
	// get prepared statement and populate args form in VariableScope
	PreparedStatement pstmt = lazy_get_pstmt(as);
	int pi = 1;
	for(String arg : as.arg_names)
	{
	  Variable var = in.getVariable(arg);
	  Value val = var.getValue();
	  switch (var.getType())
	  {
	  case ValueType.STRING:
	    pstmt.setString(pi++, (String)val.getAs(ValueType.STRING));
	    break;
	  case ValueType.INT:
	    pstmt.setInt(pi++, ((Integer)val.getAs(ValueType.INT)).intValue());
	    break;
	  case ValueType.TIMESTAMP:
	    pstmt.setTimestamp(pi++, (Timestamp)val.getAs(ValueType.TIMESTAMP));
	    break;
	  case ValueType.NUMERIC:
	    pstmt.setBigDecimal(pi++, (BigDecimal)val.getAs(ValueType.NUMERIC));
	    break;
	  default:
	    throw error("unsupported variable" + var.toString());
	  }
	}
	// execute query
	switch(as.stmt_type)
	{
	case ActionSpec.SELECT:
	  if(as.out)
	  {
	    ResultSet rs = pstmt.executeQuery();
	    while(rs != null && rs.next())
	    {
	      rmap.add(res2vs(spec.out, rs));
	      rs.close();
	    }
	  }
	  else
	  {
	    ResultSet rs = pstmt.executeQuery();
	    if(rs != null)
	      rs.close();
	  }
	  break;
	case ActionSpec.INSERT:
	case ActionSpec.DELETE:
	case ActionSpec.UPDATE:
	  ret_val = pstmt.executeUpdate();
	  dirty = true;
	  break;
	default:
	  throw error("unkown action type:" + as.stmt_type);
	}
      }
//      if(autocommitable == false)
//      {
//	conn.commit();
//	conn.setAutoCommit(true);
//      }
      if(dirty)
    	  conn.commit();
      else
    	  conn.rollback();
      return rmap;
    }
    catch (Exception e) {
      e.printStackTrace();
      throw error(e.toString());
    }
  }
}
