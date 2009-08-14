package nbench.engine.sqlmap;
import nbench.engine.helper.ActionSpec;
import nbench.engine.helper.TrSpec;
import nbench.common.*;
import nbench.common.helper.NValue;
import nbench.common.helper.MapVariableScope;
import java.util.*;
import com.ibatis.sqlmap.client.SqlMapSession;

class SQLMapBackendEngineClient implements BackendEngineClient 
{
  SQLMapBackendEngine be;
  SqlMapSession session;
  /** 
   */
  private NBenchException error(String s)
  {
    return new NBenchException("[SQL BEC]" + s);
  }
  /** 
   */
  public SQLMapBackendEngineClient(SQLMapBackendEngine be)
  {
    this.be = be;
    this.session = be.mapClient.openSession(); //GUJJI TODO close..
  }
  /** 
   */
  Map vs2map (VariableScope vs)
  {
    HashMap<String,Object> map = new HashMap<String, Object>();
    for(String name : vs.getVariableNames())
    {
      Variable var = vs.getVariable(name);
      map.put(name, var.getValue().getAs(var.getType()));
    }
    return map;
  }
  /** 
   */
  VariableScope map2vs (Map map)
  {
    return new MapVariableScope(map);
  }
  /** 
   */
  public List<VariableScope>
  execute(String name, VariableScope in) throws NBenchException
  {
    try {
      return execute_int(name, in);
    }
    catch (Exception e) {
      e.printStackTrace();
      throw new NBenchException(e.toString());
    }
  }
  /** 
   */
  private List<VariableScope>
  execute_int(String name, VariableScope in) throws Exception
  {
    List<VariableScope> rmap = null;
    Map inmap = null;
    List res = null;
    TrSpec spec = be.prepared_map.get(name);
    if (in != null)
    {
      if(in instanceof MapVariableScope) //fast
	inmap = ((MapVariableScope)in).getMap();
      else
        inmap = vs2map(in);
    }
    boolean autocommitable = (spec.action_specs.size() > 1);
    /* -------------------- */
    /* BACKEND ENGINE START */
    /* -------------------- */
    try {
      if(autocommitable == false)
	session.startTransaction();
      for(ActionSpec as : spec.action_specs)
      { 
	switch(as.stmt_type)
	{
	case ActionSpec.SELECT:
	  if(as.in)
	  {
	    //TODO 128
	    res = session.queryForList(as.sqlmapid, inmap, 0, 128);
	  }
	  else
	    res = session.queryForList(as.sqlmapid, 0, 128);
	  break;
	case ActionSpec.INSERT:
	  if(as.in)
	    session.insert(as.sqlmapid, inmap);
	  else
	    session.insert(as.sqlmapid);
	  break;
	case ActionSpec.DELETE:
	  if(as.in)
	    session.delete(as.sqlmapid, inmap);
	  else
	    session.delete(as.sqlmapid);
	  break;
	case ActionSpec.UPDATE:
	  if(as.in)
	    session.update(as.sqlmapid, inmap);
	  else
	    session.update(as.sqlmapid);
	  break;
	default:
	  throw error("unkown action type:" + as.stmt_type);
	}
      }
      if(autocommitable == false)
	session.commitTransaction();
    }
    finally {
      if(autocommitable == false)
	session.endTransaction();
    }
    /* ------------------ */
    /* BACKEND ENGINE END */
    /* ------------------ */
    if(res != null)
    {  
      rmap = new ArrayList<VariableScope>();
      for(Object m : res)
      {
	if(!(m instanceof Map))
	  throw error("result is not map");
	rmap.add(map2vs((Map)m));
      }
    }
    return rmap;
  }
}
