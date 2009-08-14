package nbench.parser;
import java.io.FileReader;
import org.xml.sax.*;
import org.xml.sax.helpers.XMLReaderFactory;
import org.xml.sax.helpers.DefaultHandler;
import java.util.Map;
import java.util.HashMap;
import java.util.ArrayList;
import nbench.parse.*;
import nbench.common.*;

public class NBenchXMLParser extends DefaultHandler
{
  /* ---------------------------------------------------------------------- */
  /* */
  /* ---------------------------------------------------------------------- */
	/*
  public class PNAT implements nbench.common.NameAndType
  {
    private String name;
    private int type;
    public PNAT(String name, String type)
    {
      this.name = name;
      if(type == null)
	this.type = ValueType.STRING;
      else
	this.type = TypeMap.typeIdOf(type);
    }
    public String getName() { return name; }
    public int getType() { return type; }
    public String toString() { return name + ":" + type; }
  }
*/
  /* ---------------------------------------------------------------------- */
  /* PARSE STATE */
  /* ---------------------------------------------------------------------- */
  class ParseState 
  {
    WorkLoad wl = null;
    boolean handle_data = false;
    StringBuffer sb = null;
    SampleSpace space = null;
    Transaction tr = null;
    Mix mix = null;
  }
  ParseState ps = null;
  // top
  private final static int PS_WORKLOAD       = 1;
  // sample space
  private final static int PS_SAMPLESPACE    = 10;
  private final static int PS_SPACE          = 11;
  private final static int PS_SAMPLE         = 12;
  // transaction definition;
  private final static int PS_TRDEF          = 100;
  private final static int PS_TRANSACTION    = 101;
  private final static int PS_DESC           = 102;
  private final static int PS_COND           = 103;
  private final static int PS_ITEM           = 104;
  private final static int PS_INPUT          = 105;
  private final static int PS_MAP            = 106;
  private final static int PS_OUTPUT         = 107;
  private final static int PS_COL            = 108;
  // mix
  private final static int PS_TRMIX          = 1000;
  private final static int PS_MIX            = 1001;
  private final static int PS_STEP           = 1002;
  private final static Map<String, java.lang.Integer> e_map;
  
  static 
  {
    e_map = new HashMap<String, Integer>(32);
    e_map.put("workload", new Integer(PS_WORKLOAD));
    e_map.put("samplespace", new Integer(PS_SAMPLESPACE));
    e_map.put("space", new Integer(PS_SPACE));
    e_map.put("sample", new Integer(PS_SAMPLE));
    e_map.put("trdef", new Integer(PS_TRDEF));
    e_map.put("transaction", new Integer(PS_TRANSACTION));
    e_map.put("desc", new Integer(PS_DESC));
    e_map.put("cond", new Integer(PS_COND));
    e_map.put("item", new Integer(PS_ITEM));
    e_map.put("input", new Integer(PS_INPUT));
    e_map.put("map", new Integer(PS_MAP));
    e_map.put("output", new Integer(PS_OUTPUT));
    e_map.put("col", new Integer(PS_COL));
    e_map.put("trmix", new Integer(PS_TRMIX));
    e_map.put("mix", new Integer(PS_MIX));
    e_map.put("step", new Integer(PS_STEP));
  }
  private int str2id(String name)
  {
    Integer i_obj = e_map.get(name.toLowerCase());
    if(i_obj == null)
      return -1;
    return i_obj.intValue();
  }
  private void debug(Object s)
  {
    System.out.println(s.toString());
  }
  //-------------------------------------------------------------------------
  // 
  //-------------------------------------------------------------------------
  public NBenchXMLParser ()
  {
    super();
  } 
  //-------------------------------------------------------------------------
  // API
  //-------------------------------------------------------------------------
  public WorkLoad parseXML(String f)
  {
    ParseState s = new ParseState();
    try 
      {
	XMLReader xr = XMLReaderFactory.createXMLReader ();
	nbench.parser.NBenchXMLParser 
		handler = new nbench.parser.NBenchXMLParser ();
	xr.setContentHandler (handler);
	xr.setErrorHandler (handler);
	FileReader r = new FileReader (f);
	synchronized(this) {
	  handler.ps = s;
	  xr.parse (new InputSource (r));
	  handler.ps = null;
	}
      }
    catch (Exception e)
      {
	e.printStackTrace();
	return null;
      }
    return s.wl;
  }

  //-------------------------------------------------------------------------
  // org.xml.sax.ContentHandler Implementation
  //-------------------------------------------------------------------------
  public void 
  startDocument () 
  throws SAXException
  {
    //optional : start time
  }

  public void 
  endDocument () 
  throws SAXException
  {
    //optional : end time
  }

  public void 
  characters (char ch[], int start, int length)
  throws SAXException
  {
    if(ps.handle_data)
      {
	ps.sb.append(ch, start, length);
	//debug("---" + length + "---");
	//debug(sb.toString());
	//passed
      }
  }

  public void 
  startElement (String uri, String name, String qName, Attributes attrs) 
  throws SAXException
  {
    NameAndType nat;
    switch(str2id(name))
    {
      case PS_WORKLOAD:
	ps.wl = new WorkLoad();
	ps.wl.name = attrs.getValue("name");
	break;
      case PS_SAMPLESPACE:
	ps.wl.ss = new HashMap<String, SampleSpace>(5);
	break;
      case PS_SPACE:
	ps.space = new SampleSpace(attrs.getValue("name"));
	break;
      case PS_SAMPLE:
	ps.space.S.put(new PNAT(attrs.getValue("name"), 
				     attrs.getValue("type")), 
			attrs.getValue("value"));
	break;
      case PS_TRDEF:
	ps.wl.trs = new HashMap<String, Transaction>();
	break;
      case PS_TRANSACTION:
	ps.tr = new Transaction(attrs.getValue("name"));
	break;
      case PS_DESC:
	ps.handle_data = true;
	ps.sb = new StringBuffer();
	break;
      case PS_COND:
	ps.tr.cond = new Condition();
	break;
      case PS_ITEM:
        int ct;
	if(attrs.getValue("type") == null)
	  ct = ValueType.STRING;
	else
	  ct = TypeMap.typeIdOf(attrs.getValue("type"));
	try 
	{
	  ConditionItem ci = new ConditionItem(attrs.getValue("expr"),
			attrs.getValue("op"), attrs.getValue("value"), ct);
	  ps.tr.cond.conds.add(ci);
	}
	catch (Exception e)
	{
	  throw new SAXException(e.toString());
	}
	break;
      case PS_INPUT:
	ps.tr.input_args = new ArrayList<NameAndType>();
	ps.tr.input_exprs = new ArrayList<String>();
	break;
      case PS_MAP:
	nat = new PNAT(attrs.getValue("name"), attrs.getValue("type"));
	ps.tr.input_args.add(nat);
	ps.tr.input_exprs.add(attrs.getValue("expr"));
	break;
      case PS_OUTPUT:
	ps.tr.output_cols = new ArrayList<NameAndType>();
        ps.tr.export_map = new HashMap<NameAndType,String>();
	break;
      case PS_COL:
	nat = new PNAT(attrs.getValue("name"),
					 attrs.getValue("type"));
	ps.tr.output_cols.add(nat);
	if(attrs.getValue("export") != null)
	  ps.tr.export_map.put(nat, attrs.getValue("export"));
	break;
      case PS_TRMIX:
	ps.wl.mixes = new ArrayList<Mix>();
	break;
      case PS_MIX:
	ps.mix = new Mix(attrs.getValue("ss"));
	break;
      case PS_STEP:
	Step st = new Step(attrs.getLocalName(0), attrs.getValue(0));
	ps.mix.steps.add(st);
	break;
      default: 
	throw new SAXException("unsupported element:" + name);
    }
  }
  public void 
  endElement (String uri, String name, String qName) 
  throws SAXException
  {
    switch(str2id(name))
    {
      case PS_WORKLOAD:
	break;
      case PS_SAMPLESPACE:
	break;
      case PS_SPACE:
	ps.wl.ss.put(ps.space.name, ps.space);
	ps.space = null;
	break;
      case PS_SAMPLE:
	break;
      case PS_TRDEF:
	break;
      case PS_TRANSACTION:
	ps.wl.trs.put(ps.tr.name, ps.tr);
	ps.tr = null;
	break;
      case PS_DESC:
	ps.tr.description = ps.sb.toString();
	ps.sb = null;
	ps.handle_data = false;
	break;
      case PS_COND:
	break;
      case PS_ITEM:
	break;
      case PS_INPUT:
	break;
      case PS_MAP:
	break;
      case PS_OUTPUT:
	break;
      case PS_COL:
	break;
      case PS_TRMIX:
	break;
      case PS_MIX:
	ps.wl.mixes.add(ps.mix);
	ps.mix = null;
	break;
      case PS_STEP:
	break;
      default: 
	throw new SAXException("unsupported element:" + name);
    }
  }

  //-------------------------------------------------------------------------
  // org.xml.sax.ErrorHandler implementation
  //-------------------------------------------------------------------------
  public void warning(SAXParseException e) throws SAXException
  {
    throw (e);
  }
  public void error(SAXParseException e) throws SAXException
  {
    throw (e);
  }
  public void fatalError(SAXParseException e) throws SAXException
  {
    System.out.println(">>>>>>>>>>>>> EE <<<<<<<<<<<<<<<<<<<");
    System.out.println("line:" + e.getLineNumber() + 
		    ", col:" + e.getColumnNumber());
    throw (e);
  }
}
