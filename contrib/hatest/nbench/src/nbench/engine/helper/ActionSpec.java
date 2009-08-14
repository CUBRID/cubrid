package nbench.engine.helper;
import nbench.common.*;
import java.io.FileInputStream;
import java.util.regex.*;
import java.util.ArrayList;
import java.util.List;
import java.util.HashMap;

//
//  'spec' format is like this..  <id>:<inout>:<statement_type>
//  <id> : as of name attribute of <statement> element in XML spec
//  <inout> : i | o | io
//  <statement_type> : select | insert | delete | update
//  example) tr1:io:select
//
public class ActionSpec 
{
  private static Pattern pattern;
  public static final int OTHER = 0;
  public static final int SELECT = 1;
  public static final int INSERT = 2;
  public static final int DELETE = 3;
  public static final int UPDATE = 4;
  static
  {
    pattern = Pattern.compile("(#\\w+?#)"); 
  }
  public String id;
  public boolean in;
  public boolean out;
  public int stmt_type;
  public String body;
  public String pstmt_str;
  public ArrayList<String> arg_names; /* index preserved */
  public String sqlmapid; // TODO refactor this..

  private void prepare_for_statement()
  {
    arg_names = new ArrayList<String>();
    Matcher matcher = pattern.matcher(body);
    while(matcher.find())
    {
      arg_names.add(matcher.group().split("#")[1]);
    }
    pstmt_str = matcher.replaceAll("?");
  }

  public ActionSpec(String spec, String body)
  {
    this.body = body;
    String[] fields = spec.split(":");
    this.id = fields[0];
    if(fields[1].equals("i")) 
    {
      in = true; 
      out = false;
    }
    else if(fields[1].equals("o")) 
    {
      in = false; 
      out = true; 
    }
    else if(fields[1].equals("io")) 
    { 
      in = true; 
      out = true;
    }
    String st = fields[2].toLowerCase();
    if(st.equals("select"))
      stmt_type = SELECT;
    else if (st.equals("insert"))
      stmt_type = INSERT;
    else if (st.equals("delete"))
      stmt_type = DELETE;
    else if (st.equals("update"))
      stmt_type = UPDATE;
    else
      stmt_type = OTHER;
    prepare_for_statement();
  }

private static void 
add_action_spec(String spec, String body, 
	HashMap<String, List<ActionSpec>> action_specs_map)
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

public static HashMap<String, List<ActionSpec>> 
load(String path) 
throws NBenchException
{
  HashMap<String, List<ActionSpec>> action_specs_map = 
  	new HashMap<String, List<ActionSpec>>();
  FileInputStream fin = null;
  try 
  {
    fin = new FileInputStream(path);
    StringBuffer action = new StringBuffer();
    StringBuffer spec = new StringBuffer();
    int PS = 0; 
    int d;
    boolean maybe_new = false;
    // simple state based parsing 
    while((d = fin.read()) >= 0)
    {
      char C = (char)d;
      if(PS == 0)
      {
	if(C == '[')
	  PS = 1;
      }
      else if(PS == 1)
      {
	if (C == ']')
	  PS = 2;
	else 
	  spec.append(C);
      }
      else if(PS == 2)
      {
	if(C == '\n')
	{
	  maybe_new = true;
	  action.append(C);
	}
	else
	{
	  if (C == '[' && maybe_new)
	  {
	    PS = 1;
	    add_action_spec(spec.toString(), action.toString(), 
		action_specs_map);
	    spec = new StringBuffer();
	    action = new StringBuffer();
	  }
	  else
	  {
	    maybe_new = false;
	    action.append(C);
	  }
	}
      }
    } /* end while */
    if(PS == 2)
      add_action_spec(spec.toString(), action.toString(),
	action_specs_map);
  }
  catch (Exception e)
  {
    throw new NBenchException(e.toString());
  }
  finally 
  {
    if(fin != null) try {fin.close();} catch(Exception e){;}
  }
  return action_specs_map;
}


  public static String getId(String spec)
  {
    return spec.split(":")[0];
  }
}
