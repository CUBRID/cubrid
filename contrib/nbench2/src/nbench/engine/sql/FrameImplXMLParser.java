package nbench.engine.sql;

import java.io.File;
import java.io.FileReader;
import java.util.EmptyStackException;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedList;
import java.util.Map;
import java.util.Set;
import java.util.Stack;

import nbench.common.NBenchException;
import nbench.common.ResourceIfs;

import org.xml.sax.Attributes;
import org.xml.sax.InputSource;
import org.xml.sax.SAXException;
import org.xml.sax.SAXParseException;
import org.xml.sax.XMLReader;
import org.xml.sax.helpers.DefaultHandler;
import org.xml.sax.helpers.XMLReaderFactory;

public class FrameImplXMLParser extends DefaultHandler {

	class ParseItem {
		int eid;
		Object node;
		Object data;

		ParseItem(int eid, Object node) {
			this.eid = eid;
			this.node = node;
			this.data = null;
		}
	}

	class ParseState {
		private Stack<ParseItem> stack;
		boolean handle_character;
		StringBuffer sb;
		FrameImpl out;

		ParseState() {
			stack = new Stack<ParseItem>();
			handle_character = false;
			sb = new StringBuffer();
		}

		void push(ParseItem item) {
			stack.push(item);
		}

		ParseItem pop() throws SAXException {
			try {
				return stack.pop();
			} catch (EmptyStackException e) {
				throw new SAXException("element stack should not be empty");
			}
		}

		ParseItem peek() throws SAXException {
			try {
				return stack.peek();
			} catch (EmptyStackException e) {
				throw new SAXException("element stack should not be empty");
			}
		}
	}

	ParseState ps;

	final static int PS_FRAME_IMPL = 1;
	final static int PS_FRAME_STATEMENTS = 10;
	final static int PS_FRAME_STATEMENT = 100;
	final static int PS_FRAME_SCRIPTS = 20;
	final static int PS_FRAME_SCRIPT = 200;

	final static Map<String, Integer> ename_eid_map;
	final static Map<Integer, String> eid_ename_map;
	final static Map<Integer, Set<Integer>> eid_contain_map;
	final static Map<Integer, Set<String>> eid_attr_map;

	static {
		/*
		 * Setup element name <--> element id hash maps
		 */
		ename_eid_map = new HashMap<String, Integer>(8);
		eid_ename_map = new HashMap<Integer, String>(8);

		ename_eid_map.put("frame-impl", PS_FRAME_IMPL);
		eid_ename_map.put(PS_FRAME_IMPL, "frame-impl");

		ename_eid_map.put("frame-statements", PS_FRAME_STATEMENTS);
		eid_ename_map.put(PS_FRAME_STATEMENTS, "frame-statements");

		ename_eid_map.put("frame-statement", PS_FRAME_STATEMENT);
		eid_ename_map.put(PS_FRAME_STATEMENT, "frame-statement");

		ename_eid_map.put("frame-scripts", PS_FRAME_SCRIPTS);
		eid_ename_map.put(PS_FRAME_SCRIPTS, "frame-scripts");

		ename_eid_map.put("frame-script", PS_FRAME_SCRIPT);
		eid_ename_map.put(PS_FRAME_SCRIPT, "frame-script");

		/*
		 * Setup element/attribute relationship. Do not set null set object for
		 * each map. An empty set act as a mock object.
		 */
		eid_contain_map = new HashMap<Integer, Set<Integer>>(8);
		eid_attr_map = new HashMap<Integer, Set<String>>(8);
		Set<Integer> elements;
		Set<String> attrs;

		// PS_FRAME_IMPL
		elements = new HashSet<Integer>();
		attrs = new HashSet<String>();
		eid_contain_map.put(PS_FRAME_IMPL, elements);
		eid_attr_map.put(PS_FRAME_IMPL, attrs);
		elements.add(PS_FRAME_STATEMENTS);
		elements.add(PS_FRAME_SCRIPTS);

		// PS_FRAME_STATEMENTS
		elements = new HashSet<Integer>();
		attrs = new HashSet<String>();
		eid_contain_map.put(PS_FRAME_STATEMENTS, elements);
		eid_attr_map.put(PS_FRAME_STATEMENTS, attrs);
		elements.add(PS_FRAME_STATEMENT);

		// PS_FRAME_STATEMENT
		elements = new HashSet<Integer>();
		attrs = new HashSet<String>();
		eid_contain_map.put(PS_FRAME_STATEMENT, elements);
		eid_attr_map.put(PS_FRAME_STATEMENT, attrs);
		attrs.add("name");
		// NOT a must attrs.add("template");

		// PS_FRAME_SCRIPTS
		elements = new HashSet<Integer>();
		attrs = new HashSet<String>();
		eid_contain_map.put(PS_FRAME_SCRIPTS, elements);
		eid_attr_map.put(PS_FRAME_SCRIPTS, attrs);
		elements.add(PS_FRAME_SCRIPT);

		// PS_FRAME_SCRIPT
		elements = new HashSet<Integer>();
		attrs = new HashSet<String>();
		eid_contain_map.put(PS_FRAME_SCRIPT, elements);
		eid_attr_map.put(PS_FRAME_SCRIPT, attrs);
		attrs.add("impl");
	}

	int name2id(String name) throws SAXException {
		Integer i_obj = ename_eid_map.get(name.toLowerCase());
		if (i_obj == null) {
			throw new SAXException("unknown element" + name);
		}
		return i_obj.intValue();
	}

	String id2name(int id) throws SAXException {
		String name = eid_ename_map.get(id);
		if (name == null) {
			throw new SAXException("unknown element id:" + id);
		}
		return name;
	}

	private void checkAttributes(int eid, Attributes attrs) throws SAXException {
		Set<String> set = eid_attr_map.get(eid);
		Iterator<String> itor = set.iterator();
		while (itor.hasNext()) {
			String attr = itor.next();
			if (attrs.getValue(attr) == null) {
				throw new SAXException("attribute:" + attr
						+ " must be specified");
			}
		}
		// Let optional parameters can be specified
		if (false) {
			for (int i = 0; i < attrs.getLength(); i++) {
				String name = attrs.getLocalName(i);
				if (!set.contains(name)) {
					throw new SAXException("unknown attribute '" + name
							+ "' is specified");
				}
			}
		}
	}

	private void checkContainments(int eid) throws SAXException {
		if (ps.stack.empty()) {
			if (eid != PS_FRAME_IMPL) {
				throw new SAXException(id2name(PS_FRAME_IMPL)
						+ " should be top level element");
			}
			return;
		}
		ParseItem parent_item = ps.stack.peek();
		Set<Integer> set = eid_contain_map.get(parent_item.eid);
		if (!set.contains(eid)) {
			throw new SAXException("element " + id2name(eid)
					+ " can not be a child of the " + id2name(parent_item.eid));
		}
	}

	/* ------------------------------------------------------------------------- */

	public FrameImplXMLParser() {
		super();
		ps = new ParseState();
	}

	public void startDocument() throws SAXException {
	}

	public void endDocument() throws SAXException {
	}

	public void characters(char ch[], int start, int length)
			throws SAXException {
		if (ps.handle_character) {
			ps.sb.append(ch, start, length);
		}
	}

	public void startElement(String uri, String name, String qName,
			Attributes attrs) throws SAXException {
		int eid = name2id(name);
		checkContainments(eid);
		checkAttributes(eid, attrs);

		switch (eid) {
		case PS_FRAME_IMPL: {
			FrameImpl fi = new FrameImpl();
			ParseItem item = new ParseItem(eid, fi);
			ps.push(item);
			break;
		}
		case PS_FRAME_STATEMENTS: {
			ParseItem item = new ParseItem(eid, null);
			item.data = new LinkedList<FrameStatement>();
			ps.push(item);
			break;
		}
		case PS_FRAME_STATEMENT: {
			FrameStatement stmt = new FrameStatement();
			stmt.name = attrs.getValue("name");
			stmt.template = attrs.getValue("template");
			ParseItem item = new ParseItem(eid, stmt);
			ps.push(item);
			ps.handle_character = true;
			break;
		}
		case PS_FRAME_SCRIPTS: {
			ParseItem item = new ParseItem(eid, null);
			item.data = new LinkedList<FrameScript>();
			ps.push(item);
			break;
		}
		case PS_FRAME_SCRIPT: {
			FrameScript script = new FrameScript();
			script.impl = attrs.getValue("impl");
			ParseItem item = new ParseItem(eid, script);
			ps.push(item);
			ps.handle_character = true;
			break;
		}
		default:
			throw new SAXException("unhandled element:" + name);
		}
	}

	@SuppressWarnings("unchecked")
	public void endElement(String uri, String name, String qName)
			throws SAXException {

		ParseItem curr = ps.pop();
		// now ps.peek() returns parent item of this element

		switch (name2id(name)) {
		case PS_FRAME_IMPL: {
			ps.out = (FrameImpl) curr.node;
			break;
		}
		case PS_FRAME_STATEMENTS: {
			LinkedList<FrameStatement> list = (LinkedList<FrameStatement>) curr.data;
			FrameImpl impl = (FrameImpl) ps.peek().node;
			impl.statements = list.toArray(new FrameStatement[0]);
			break;
		}
		case PS_FRAME_STATEMENT: {
			FrameStatement stmt = (FrameStatement) curr.node;
			stmt.data = ps.sb.toString();
			ps.sb = new StringBuffer();
			ps.handle_character = false;
			LinkedList<FrameStatement> list = (LinkedList<FrameStatement>) ps
					.peek().data;
			list.add(stmt);
			break;
		}
		case PS_FRAME_SCRIPTS: {
			LinkedList<FrameScript> list = (LinkedList<FrameScript>) curr.data;
			FrameImpl impl = (FrameImpl) ps.peek().node;
			impl.scripts = list.toArray(new FrameScript[0]);
			break;
		}
		case PS_FRAME_SCRIPT: {
			FrameScript script = (FrameScript) curr.node;
			script.script = ps.sb.toString();
			ps.sb = new StringBuffer();
			ps.handle_character = false;
			LinkedList<FrameScript> list = (LinkedList<FrameScript>) ps.peek().data;
			list.add(script);
			break;
		}
		default:
			throw new SAXException("unhandled element:" + name);
		}
	}

	public void warning(SAXParseException e) throws SAXException {
		throw (e);
	}

	public void error(SAXParseException e) throws SAXException {
		throw (e);
	}

	public void fatalError(SAXParseException e) throws SAXException {
		// System.out.println("ERROR!!");
		// System.out.println("line:" + e.getLineNumber() + ", col:"
		// + e.getColumnNumber());
		throw (e);
	}

	public FrameImpl parseFrameImplXML(File f) {
		try {
			XMLReader xr = XMLReaderFactory.createXMLReader();
			FrameImplXMLParser handler = new FrameImplXMLParser();
			xr.setContentHandler(handler);
			xr.setErrorHandler(handler);
			FileReader r = new FileReader(f);
			xr.parse(new InputSource(r));
			r.close();
			return handler.ps.out;
		} catch (Exception e) {
			e.printStackTrace();
			return null;
		}
	}

	public FrameImpl parseFrameImplXML(ResourceIfs resourceIfs)
			throws NBenchException {
		try {
			XMLReader xr = XMLReaderFactory.createXMLReader();
			FrameImplXMLParser handler = new FrameImplXMLParser();
			xr.setContentHandler(handler);
			xr.setErrorHandler(handler);
			xr.parse(new InputSource(resourceIfs.getResourceInputStream()));
			resourceIfs.close();
			return handler.ps.out;
		} catch (Exception e) {
			throw new NBenchException(e);
		}
	}

	public static void main(String args[]) {

		try {
			FrameImpl frameImpl;
			FrameImplXMLParser parser = new FrameImplXMLParser();
			FrameImplDumpVisitor dumper = new FrameImplDumpVisitor(System.out);
			if (args.length == 0) {
				System.out
						.println("FrameImplXMLParser <frame implementation xml file>");
				return;
			}
			frameImpl = parser.parseFrameImplXML(new File(args[0]));
			dumper.visit(frameImpl);

		} catch (Exception e) {
			e.printStackTrace();
		}
	}
}
