import nbench.parser.NBenchXMLParser;
import nbench.parse.*;
import nbench.common.*;
import nbench.engine.*;
import java.util.Set;
import java.util.Enumeration;
import java.util.Properties;
import java.io.FileInputStream;

/* ------------------------------------------------------------------------- */
public class NBench {
/* ------------------------------------------------------------------------- */
public static void main(String args[])
{
  Properties props;
  NBenchXMLParser parser = new NBenchXMLParser();
  WorkLoad wl;
  FileInputStream fin = null;
  String runmix = null;
  try
  {
	if (args.length == 0)
	{
		System.out.println("java NBench Propertyfile [Propertyfile2]");
		return;
	}
    fin = new FileInputStream(args[0]);
    props = new Properties();
    props.load(fin);
    System.out.println("________________________________________________________________________________");
    System.out.println("[PARSE AND DUMP]");
    wl = parser.parseXML(props.getProperty("benchmark"));
    if(wl != null)
      System.out.println(wl);
    else
      System.out.println("Parse failed..");
    System.out.println("________________________________________________________________________________");
    System.out.println("[BENCHMARK TEST]");
    if(args.length > 1)
    {
      Properties oprops = new Properties();
      oprops.load(new FileInputStream(args[1]));
      for(Enumeration e = oprops.propertyNames(); e.hasMoreElements();) 
      {
	String k = (String)e.nextElement();
	String v = oprops.getProperty(k);
	props.setProperty(k, v);
      }
    }
    if(args.length > 2)
      props.setProperty("runtime", args[2]);
    NFrontEngine fe = new NFrontEngine(wl, props);
    fe.prepare();
    fe.runBenchmark();
  }
  catch (Exception e) 
  {
    e.printStackTrace();
  }
  finally
  {
    if(fin != null)
      try { fin.close(); } catch (Exception e) {;}
  }
}

/* ------------------------------------------------------------------------- */
}
/* ------------------------------------------------------------------------- */
