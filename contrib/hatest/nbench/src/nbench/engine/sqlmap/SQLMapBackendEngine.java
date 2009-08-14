package nbench.engine.sqlmap;
import nbench.common.*;
import nbench.engine.helper.ActionSpec;
import nbench.engine.helper.TrSpec;
import nbench.common.helper.NValue;
import java.io.File;
import java.io.IOException;
import java.io.FileReader;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.StringReader;
import java.util.Set;
import java.util.Map;
import java.util.List;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Properties;
import java.net.URL;
import com.ibatis.sqlmap.client.*;

public class SQLMapBackendEngine implements BackendEngine 
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
//xml generation related
private String conf;
private static String conf_header = 
	 "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
       + " <!DOCTYPE sqlMapConfig\n"
       + " PUBLIC \"-//ibatis.apache.org//DTD SQL Map Config 2.0//EN\"\n"
       + " \"http://ibatis.apache.org/dtd/sql-map-config-2.dtd\">\n"
       + "\n<sqlMapConfig>\n";
private static String conf_footer = "</sqlMapConfig>\n";
private static String map_header = 
	"<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
       + " <!DOCTYPE sqlMap\n"
       + " PUBLIC \"-//ibatis.apache.org//DTD SQL Map 2.0//EN\"\n"
       + " \"http://ibatis.apache.org/dtd/sql-map-2.dtd\">\n\n";
private static String map_footer = "</sqlMap>\n";
private HashMap<String, List<ActionSpec>> action_specs_map;
//shared with cliends... so read only..
HashMap<String, TrSpec> prepared_map;
SqlMapClient mapClient; 

/* -------------------------------------------------------------------------- */
/* METHODS */
/* -------------------------------------------------------------------------- */
public SQLMapBackendEngine() 
{
  this.status = STATUS_CREATED;
}
private NBenchException error(String message)
{
  return new NBenchException("[SQLMap BE]" + message);
}
private void add_action_spec(String spec, String body)
{
  ActionSpec as = new ActionSpec(spec, body);
  List<ActionSpec> actions = action_specs_map.get(as.id);
  if(actions == null)
  {
    ArrayList<ActionSpec> nb = new ArrayList<ActionSpec>(2);
    nb.add(as);
    action_specs_map.put(as.id, nb);
  }
  else
    actions.add(as);
}
private void loadActionSpecs(String path) throws NBenchException
{
  action_specs_map = ActionSpec.load(path);
}
/**
 **/
public void configure (Properties props) throws NBenchException
{
  this.props = props;
  if(status != STATUS_CREATED)
    throw error("invalid status");
  conf = props.getProperty("configure");
  if(conf == null)
    throw error("should have 'configure' property");
  prepared_map = new HashMap<String, TrSpec>();
  loadActionSpecs(props.getProperty("actions"));
  status = STATUS_CONFIGURED;
}
/**
 **/
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
//<parameterMap id=¡±parameterMapName¡± [class=¡±com.domain.Product¡±]>
//  <parameter property =¡±propertyName¡± 
//	[jdbcType=¡±VARCHAR¡±] 
//	[javaType=¡±string¡±]
//	[nullValue=¡°-9999¡±]
//	[typeName=¡±{REF or user-defined type}¡±]
//	[resultMap=someResultMap]
//	[mode=IN|OUT|INOUT]
//	[typeHandler=someTypeHandler]
//	[numericScale=2]/>
//  <parameter ¡¦¡¦ />
//  <parameter ¡¦¡¦ />
//</parameterMap>
private void make_parameter_map (StringBuffer sb, TrSpec trs, String name)
throws Exception
{
  sb.append("<parameterMap id=\"" + name + "\" ");
  sb.append("class=\"java.util.Map\">\n");
  for(NameAndType nat : trs.in)
  {
    sb.append("<parameter property=\"" + nat.getName() + "\" ");
    sb.append("javaType=\"" + ValueType.getJavaType(nat.getType()) + "\"");
    sb.append("/>\n");
  }
  sb.append("</parameterMap>\n\n");
}
// <resultMap id=¡±resultMapName¡± class=¡±some.domain.Class¡±
// 	[extends=¡±parent-resultMap¡±]
// 	[groupBy=¡°some property list¡±]>
//  <result property=¡±propertyName¡± 
//	column=¡±COLUMN_NAME¡±
//	[columnIndex=¡±1¡±] [javaType=¡±int¡±] [jdbcType=¡±NUMERIC¡±]
//	[nullValue=¡±-999999¡±] [select=¡±someOtherStatement¡±]
//	[resultMap=¡°someOtherResultMap¡±]
//	[typeHandler=¡°com.mydomain.MyTypehandler¡±] />
//  <result ¡¦¡¦/>
//  <result ¡¦¡¦/>
//  <result ¡¦¡¦/>
//</resultMap>
private void make_result_map(StringBuffer sb, TrSpec trs, String name)
throws Exception
{
  sb.append("<resultMap id=\"" + name + "\" class=\"java.util.HashMap\">\n");
  for(NameAndType nat : trs.out)
  {
    sb.append("<result property=\"" + nat.getName() + "\" ");
    sb.append("javaType=\"" + ValueType.getJavaType(nat.getType()) + "\"");
    sb.append("/>\n");
  }
  sb.append("</resultMap>\n\n");
}
private String trans2xml(String input)
throws Exception
{
  StringBuffer sb = new StringBuffer();
  StringReader sr = new StringReader(input);
  int c;
  while( (c = sr.read()) >= 0)
  {
    char C = (char) c;
    if(c == '>')
      sb.append("<![CDATA[ > ]]>");
    else if (C == '<')
      sb.append("<![CDATA[ < ]]>");
    else 
      sb.append(C);
  }
  return sb.toString();
}
//<statement id=¡±statementName¡±
//	[parameterClass=¡±some.class.Name¡±]
//	[resultClass=¡±some.class.Name¡±]
//	[parameterMap=¡±nameOfParameterMap¡±]
//	[resultMap=¡±nameOfResultMap¡±]
//	[cacheModel=¡±nameOfCache¡±]
//	[timeout=¡°5¡±]>
//   select * from PRODUCT where PRD_ID = [?|#propertyName#]
//   order by [$simpleDynamic$]
//</statement>
private void make_statement(StringBuffer sb, ActionSpec as, String parameter_map_id, String result_map_id)
throws Exception
{
  sb.append("<statement id=\""+ as.sqlmapid + "\" ");
  if(as.in)
    //GUJJI sb.append("parameterMap=\""+ parameter_map_id + "\" ");
    sb.append("parameterClass=\"java.util.Map\" ");
  if(as.out)
    sb.append("resultMap=\""+ result_map_id + "\" ");
  sb.append(">\n");
  sb.append(trans2xml(as.body));
  sb.append("</statement>\n\n");
}
private void make_map(File dir) throws Exception
{
  String dirpath = dir.toString();
  for(TrSpec trs : prepared_map.values())
  {
    StringBuffer sb = new StringBuffer();
    File f = new File(dirpath + File.separator + trs.name + ".xml");
    FileOutputStream fos = new FileOutputStream(f);
    sb.append(map_header);
    sb.append("<sqlMap namespace=\"" + trs.name + "\">\n\n");
    String parameter_map_id  = trs.name + "_param";
    String result_map_id = trs.name + "_res";
    //GUJJI make_parameter_map(sb, trs, parameter_map_id);
    make_result_map(sb, trs, result_map_id);
    int i = 0;
    for(ActionSpec action_spec : trs.action_specs)
    {
      action_spec.sqlmapid = trs.name + "_stmt_" + i;
      i++;
      make_statement(sb, action_spec, parameter_map_id, result_map_id);
    }
    sb.append(map_footer);
    fos.write(sb.toString().getBytes());
    fos.close();
  }
}
private void make_conf(File dir, String name) throws Exception
{
  File conffile = 
	new File(dir.toString() + File.separator + name);
  FileOutputStream fout = new FileOutputStream(conffile);
  fout.write(conf_header.getBytes());
  fout.write(conf.getBytes());
  fout.write("\n\n".getBytes());
  String url = dir.toURL().toString();
  for(TrSpec trs : prepared_map.values())
  {
    fout.write("<sqlMap url=\"".getBytes());
    fout.write(url.getBytes());
    fout.write(trs.name.getBytes());
    fout.write(".xml\" />".getBytes());
    fout.write("\n".getBytes());
  }
  fout.write(conf_footer.getBytes());
  fout.close();
}
private void make_sqlmap(File dir) throws Exception
{
}
public void consolidateForRun() throws NBenchException
{
  try 
  {
    File mydir = 
	new File(props.getProperty("basedir") + File.separator + "engine");
    mydir.mkdir();
    File[] files = mydir.listFiles();
    for(int i = 0; i < files.length; i++)
    {
      File f = files[i];
      if(f.isFile())
	f.delete();
    }
    make_conf(mydir, "sqlMapConf.xml");
    make_map(mydir);
    //
    String res = mydir.toString() + File.separator + "sqlMapConf.xml";
    FileReader fr = new FileReader(res);
    mapClient =SqlMapClientBuilder.buildSqlMapClient(fr); 
  }
  catch (Exception e)
  {
    e.printStackTrace();
    throw error(e.toString());
  }
  status = STATUS_CONSOLIDATED;
}
/**
 **/
public SQLMapBackendEngineClient createClient() throws NBenchException
{
  if(status != STATUS_CONSOLIDATED)
    throw error("invalid status");
  return new SQLMapBackendEngineClient(this);
}
} /* BackendEngine */


