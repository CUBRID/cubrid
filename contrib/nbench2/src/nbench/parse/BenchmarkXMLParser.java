package nbench.parse;

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
import nbench.common.NameAndTypeIfs;
import nbench.common.ValueType;
import nbench.common.ResourceIfs;

import org.xml.sax.Attributes;
import org.xml.sax.InputSource;
import org.xml.sax.SAXException;
import org.xml.sax.SAXParseException;
import org.xml.sax.XMLReader;
import org.xml.sax.helpers.DefaultHandler;
import org.xml.sax.helpers.XMLReaderFactory;

public class BenchmarkXMLParser extends DefaultHandler {

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
		Benchmark out;

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

	class PNAT implements NameAndTypeIfs {
		String name;
		int type;

		public PNAT(String name, int type) {
			this.name = name;
			this.type = type;
		}

		public String getName() {
			return name;
		}

		public int getType() {
			return type;
		}
	}

	ParseState ps;

	final static int PS_BENCHMARK = 1;
	final static int PS_TRANSACTION_FRAME = 2;
	final static int PS_FRAME = 20;
	final static int PS_INPUT_MAP = 200;
	final static int PS_OUTPUT_MAP = 201;
	final static int PS_MAP = 2000;
	final static int PS_SAMPLE_SPACE = 3;
	final static int PS_SAMPLE = 30;
	final static int PS_TRANSACTION_COMMON = 4;
	final static int PS_TRANSACTION_DEFINITION = 5;
	final static int PS_TRANSACTION = 50;
	final static int PS_TRANSACTION_MIX = 6;
	final static int PS_MIX = 60;

	final static Map<String, Integer> ename_eid_map;
	final static Map<Integer, String> eid_ename_map;
	final static Map<Integer, Set<Integer>> eid_contain_map;
	final static Map<Integer, Set<String>> eid_attr_map;

	static {
		/*
		 * Setup element name <--> element id hash maps
		 */
		ename_eid_map = new HashMap<String, Integer>(16);
		eid_ename_map = new HashMap<Integer, String>(16);

		ename_eid_map.put("benchmark", PS_BENCHMARK);
		eid_ename_map.put(PS_BENCHMARK, "benchmark");

		ename_eid_map.put("transaction-frame", PS_TRANSACTION_FRAME);
		eid_ename_map.put(PS_TRANSACTION_FRAME, "transaction-frame");

		ename_eid_map.put("frame", PS_FRAME);
		eid_ename_map.put(PS_TRANSACTION_FRAME, "frame");

		ename_eid_map.put("input-map", PS_INPUT_MAP);
		eid_ename_map.put(PS_INPUT_MAP, "input-map");

		ename_eid_map.put("output-map", PS_OUTPUT_MAP);
		eid_ename_map.put(PS_OUTPUT_MAP, "output-map");

		ename_eid_map.put("map", PS_MAP);
		eid_ename_map.put(PS_MAP, "map");

		ename_eid_map.put("sample-space", PS_SAMPLE_SPACE);
		eid_ename_map.put(PS_SAMPLE_SPACE, "sample-space");

		ename_eid_map.put("sample", PS_SAMPLE);
		eid_ename_map.put(PS_SAMPLE, "sample");

		ename_eid_map.put("transaction-definition", PS_TRANSACTION_DEFINITION);
		eid_ename_map.put(PS_TRANSACTION_DEFINITION, "transaction-definition");

		ename_eid_map.put("transaction-common", PS_TRANSACTION_COMMON);
		eid_ename_map.put(PS_TRANSACTION_COMMON, "transaction-common");

		ename_eid_map.put("transaction", PS_TRANSACTION);
		eid_ename_map.put(PS_TRANSACTION, "transaction");

		ename_eid_map.put("transaction-mix", PS_TRANSACTION_MIX);
		eid_ename_map.put(PS_TRANSACTION_MIX, "transaction-mix");

		ename_eid_map.put("mix", PS_MIX);
		eid_ename_map.put(PS_MIX, "mix");

		/*
		 * Setup element/attribute relationship. Do not set null set object for
		 * each map. An empty set act as a mock object.
		 */
		eid_contain_map = new HashMap<Integer, Set<Integer>>(16);
		eid_attr_map = new HashMap<Integer, Set<String>>(16);
		Set<Integer> elements;
		Set<String> attrs;

		// PS_BENCHMARK
		elements = new HashSet<Integer>();
		attrs = new HashSet<String>();
		eid_contain_map.put(PS_BENCHMARK, elements);
		eid_attr_map.put(PS_BENCHMARK, attrs);
		elements.add(PS_TRANSACTION_FRAME);
		elements.add(PS_SAMPLE_SPACE);
		elements.add(PS_TRANSACTION_COMMON);
		elements.add(PS_TRANSACTION_DEFINITION);
		elements.add(PS_TRANSACTION_MIX);
		attrs.add("name");

		// PS_TRANSACTION_FRAME
		elements = new HashSet<Integer>();
		attrs = new HashSet<String>();
		eid_contain_map.put(PS_TRANSACTION_FRAME, elements);
		eid_attr_map.put(PS_TRANSACTION_FRAME, attrs);
		elements.add(PS_FRAME);

		// PS_FRAME
		elements = new HashSet<Integer>();
		attrs = new HashSet<String>();
		eid_contain_map.put(PS_FRAME, elements);
		eid_attr_map.put(PS_FRAME, attrs);
		elements.add(PS_INPUT_MAP);
		elements.add(PS_OUTPUT_MAP);
		attrs.add("name");
		attrs.add("impl");

		// PS_INPUT_MAP
		elements = new HashSet<Integer>();
		attrs = new HashSet<String>();
		eid_contain_map.put(PS_INPUT_MAP, elements);
		eid_attr_map.put(PS_INPUT_MAP, attrs);
		elements.add(PS_MAP);

		// PS_OUTPUT_MAP
		elements = new HashSet<Integer>();
		attrs = new HashSet<String>();
		eid_contain_map.put(PS_OUTPUT_MAP, elements);
		eid_attr_map.put(PS_OUTPUT_MAP, attrs);
		elements.add(PS_MAP);

		// PS_MAP
		elements = new HashSet<Integer>();
		attrs = new HashSet<String>();
		eid_contain_map.put(PS_MAP, elements);
		eid_attr_map.put(PS_MAP, attrs);
		attrs.add("name");
		attrs.add("type");

		// PS_SAMPLE_SPACE
		elements = new HashSet<Integer>();
		attrs = new HashSet<String>();
		eid_contain_map.put(PS_SAMPLE_SPACE, elements);
		eid_attr_map.put(PS_SAMPLE_SPACE, attrs);
		elements.add(PS_SAMPLE);

		// PS_SAMPLE
		elements = new HashSet<Integer>();
		attrs = new HashSet<String>();
		eid_contain_map.put(PS_SAMPLE, elements);
		eid_attr_map.put(PS_SAMPLE, attrs);
		attrs.add("name");
		attrs.add("type");
		attrs.add("elem-type");
		attrs.add("value");

		// PS_TRANSACTION_COMMON
		elements = new HashSet<Integer>();
		attrs = new HashSet<String>();
		eid_contain_map.put(PS_TRANSACTION_COMMON, elements);
		eid_attr_map.put(PS_TRANSACTION_COMMON, attrs);

		// PS_TRANSACTION_DEFINITION
		elements = new HashSet<Integer>();
		attrs = new HashSet<String>();
		eid_contain_map.put(PS_TRANSACTION_DEFINITION, elements);
		eid_attr_map.put(PS_TRANSACTION_DEFINITION, attrs);
		elements.add(PS_TRANSACTION);

		// PS_TRANSACTION
		elements = new HashSet<Integer>();
		attrs = new HashSet<String>();
		eid_contain_map.put(PS_TRANSACTION, elements);
		eid_attr_map.put(PS_TRANSACTION, attrs);
		attrs.add("name");
		attrs.add("sla");

		// PS_TRANSACTION_MIX
		elements = new HashSet<Integer>();
		attrs = new HashSet<String>();
		eid_contain_map.put(PS_TRANSACTION_MIX, elements);
		eid_attr_map.put(PS_TRANSACTION_MIX, attrs);
		elements.add(PS_MIX);

		// PS_MIX
		elements = new HashSet<Integer>();
		attrs = new HashSet<String>();
		eid_contain_map.put(PS_MIX, elements);
		eid_attr_map.put(PS_MIX, attrs);
		attrs.add("name");
		attrs.add("nthread");
		// attrs.add("setup"); optional property
		attrs.add("target");
		attrs.add("type");
		attrs.add("value");
		attrs.add("think-time");
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
		if (false) {
			for (int i = 0; i < attrs.getLength(); i++) {
				String name = attrs.getLocalName(i);
				if (!set.contains(name)) {
					throw new SAXException("unknown attribute " + name
							+ " is specified");
				}
			}
		}
	}

	private void checkContainments(int eid) throws SAXException {
		if (ps.stack.empty()) {
			if (eid != PS_BENCHMARK) {
				throw new SAXException(id2name(PS_BENCHMARK)
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

	public BenchmarkXMLParser() {
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
		case PS_BENCHMARK: {
			Benchmark b = new Benchmark();
			b.name = attrs.getValue("name");
			ParseItem item = new ParseItem(eid, b);
			ps.push(item);
			break;
		}
		case PS_TRANSACTION_FRAME: {
			ParseItem item = new ParseItem(eid, null);
			item.data = new LinkedList<Frame>();
			ps.push(item);
			break;
		}
		case PS_FRAME: {
			Frame frame = new Frame();
			frame.name = attrs.getValue("name");
			frame.impl = attrs.getValue("impl");
			ParseItem item = new ParseItem(eid, frame);
			ps.push(item);
			break;
		}
		case PS_INPUT_MAP: {
			ParseItem item = new ParseItem(eid, null);
			item.data = new LinkedList<NameAndTypeIfs>();
			ps.push(item);
			break;
		}
		case PS_OUTPUT_MAP: {
			ParseItem item = new ParseItem(eid, null);
			item.data = new LinkedList<NameAndTypeIfs>();
			ps.push(item);
			break;
		}
		case PS_MAP: {
			try {
				String n = attrs.getValue("name");
				String t = attrs.getValue("type");
				NameAndTypeIfs nat = new PNAT(n, ValueType.typeOfName(t));
				ps.push(new ParseItem(eid, nat));
			} catch (NBenchException e) {
				throw new SAXException(e);
			}
			break;
		}
		case PS_SAMPLE_SPACE: {
			ParseItem item = new ParseItem(eid, null);
			item.data = new LinkedList<Sample>();
			ps.push(item);
			break;
		}
		case PS_SAMPLE: {
			try {
				Sample sample = new Sample();
				sample.name = attrs.getValue("name");
				sample.type = attrs.getValue("type");
				sample.elem_type = ValueType.typeOfName(attrs
						.getValue("elem-type"));
				sample.value = attrs.getValue("value");
				ps.push(new ParseItem(eid, sample));
			} catch (NBenchException e) {
				throw new SAXException(e);
			}
			break;
		}

		case PS_TRANSACTION_COMMON: {
			ParseItem item = new ParseItem(eid, null);
			item.data = null;
			ps.push(item);
			ps.handle_character = true;
			break;
		}

		case PS_TRANSACTION_DEFINITION: {
			ParseItem item = new ParseItem(eid, null);
			item.data = new LinkedList<Transaction>();
			ps.push(item);
			break;
		}
		case PS_TRANSACTION: {
			Transaction transaction = new Transaction();
			transaction.name = attrs.getValue("name");
			transaction.sla = Integer.valueOf(attrs.getValue("sla"));
			ParseItem item = new ParseItem(eid, transaction);
			ps.push(item);
			ps.handle_character = true;
			break;
		}

		case PS_TRANSACTION_MIX: {
			ParseItem item = new ParseItem(eid, null);
			item.data = new LinkedList<Mix>();
			ps.push(item);
			break;
		}
		case PS_MIX: {
			try {
				Mix mix = new Mix();
				mix.name = attrs.getValue("name");
				mix.nthread = Integer.valueOf(attrs.getValue("nthread"));
				mix.setup = attrs.getValue("setup");
				mix.trs = attrs.getValue("target").split(":");
				mix.type = attrs.getValue("type");
				mix.value = attrs.getValue("value");
				String tts[] = attrs.getValue("think-time").split(":");
				if (tts.length != mix.trs.length) {
					throw new SAXException(
							"number of target and think-time mismatch");
				}
				mix.think_times = new int[tts.length];
				for (int i = 0; i < tts.length; i++) {
					mix.think_times[i] = Integer.valueOf(tts[i]);
				}
				ps.push(new ParseItem(eid, mix));
			} catch (Exception e) {
				throw new SAXException(e);
			}
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
		case PS_BENCHMARK: {
			ps.out = (Benchmark) curr.node;
			break;
		}
		case PS_TRANSACTION_FRAME: {
			LinkedList<Frame> list = (LinkedList<Frame>) curr.data;
			Benchmark benchmark = (Benchmark) ps.peek().node;
			benchmark.frames = list.toArray(new Frame[0]);
			break;
		}
		case PS_FRAME: {
			Frame frame = (Frame) curr.node;
			LinkedList<Frame> list = (LinkedList<Frame>) ps.peek().data;
			list.add(frame);
			break;
		}
		case PS_INPUT_MAP: {
			LinkedList<NameAndTypeIfs> list = (LinkedList<NameAndTypeIfs>) curr.data;
			Frame frame = (Frame) ps.peek().node;
			frame.in = list.toArray(new NameAndTypeIfs[0]);
			break;
		}
		case PS_OUTPUT_MAP: {
			LinkedList<NameAndTypeIfs> list = (LinkedList<NameAndTypeIfs>) curr.data;
			Frame frame = (Frame) ps.peek().node;
			frame.out = list.toArray(new NameAndTypeIfs[0]);
			break;
		}
		case PS_MAP: {
			NameAndTypeIfs nat = (NameAndTypeIfs) curr.node;
			LinkedList<NameAndTypeIfs> list = (LinkedList<NameAndTypeIfs>) ps
					.peek().data;
			list.add(nat);
			break;
		}
		case PS_SAMPLE_SPACE: {
			LinkedList<Sample> list = (LinkedList<Sample>) curr.data;
			Benchmark benchmark = (Benchmark) ps.peek().node;
			benchmark.samples = list.toArray(new Sample[0]);
			break;
		}
		case PS_SAMPLE: {
			Sample sample = (Sample) curr.node;
			LinkedList<Sample> list = (LinkedList<Sample>) ps.peek().data;
			list.add(sample);
			break;
		}
		case PS_TRANSACTION_COMMON: {
			String script = ps.sb.toString();
			ps.sb = new StringBuffer();
			ps.handle_character = false;
			Benchmark benchmark = (Benchmark) ps.peek().node;
			benchmark.transaction_common = script;
			break;
		}
		case PS_TRANSACTION_DEFINITION: {
			LinkedList<Transaction> list = (LinkedList<Transaction>) curr.data;
			Benchmark benchmark = (Benchmark) ps.peek().node;
			benchmark.transactions = list.toArray(new Transaction[0]);
			break;
		}
		case PS_TRANSACTION: {
			Transaction transaction = (Transaction) curr.node;
			transaction.script = ps.sb.toString();
			ps.sb = new StringBuffer();
			ps.handle_character = false;
			LinkedList<Transaction> list = (LinkedList<Transaction>) ps.peek().data;
			list.add(transaction);
			break;
		}

		case PS_TRANSACTION_MIX: {
			LinkedList<Mix> list = (LinkedList<Mix>) curr.data;
			Benchmark benchmark = (Benchmark) ps.peek().node;
			benchmark.mixes = list.toArray(new Mix[0]);
			break;
		}
		case PS_MIX: {
			Mix mix = (Mix) curr.node;
			LinkedList<Mix> list = (LinkedList<Mix>) ps.peek().data;
			list.add(mix);
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
		System.out.println("ERROR!!");
		System.out.println("line:" + e.getLineNumber() + ", col:"
				+ e.getColumnNumber());
		throw (e);
	}

	public Benchmark parseBenchmarkXML(File f) throws NBenchException {
		try {
			XMLReader xr = XMLReaderFactory.createXMLReader();
			BenchmarkXMLParser handler = new BenchmarkXMLParser();
			xr.setContentHandler(handler);
			xr.setErrorHandler(handler);
			FileReader r = new FileReader(f);
			xr.parse(new InputSource(r));
			r.close();
			return handler.ps.out;
		} catch (Exception e) {
			throw new NBenchException(e);
		}
	}

	public Benchmark parseBenchmarkXML(ResourceIfs resourceIfs)
			throws NBenchException {
		try {
			XMLReader xr = XMLReaderFactory.createXMLReader();
			BenchmarkXMLParser handler = new BenchmarkXMLParser();
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
			Benchmark benchmark;
			BenchmarkXMLParser parser = new BenchmarkXMLParser();
			BenchmarkDumpVisitor dumper = new BenchmarkDumpVisitor(System.out);
			if (args.length == 0) {
				System.out.println("BenchmarkXMLParser <benchmark xml file>");
				return;
			}
			benchmark = parser.parseBenchmarkXML(new File(args[0]));
			dumper.visit(benchmark);

		} catch (Exception e) {
			e.printStackTrace();
		}
	}
}
