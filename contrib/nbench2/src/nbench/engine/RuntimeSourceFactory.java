package nbench.engine;

import nbench.common.*;
import nbench.engine.sql.FrameImpl;
import nbench.parse.*;

class BenchmarkSourceGenerator implements BenchmarkVisitor {
	// input
	private final Benchmark bm_;
	// output
	private RuntimeSource output;
	// segments for java file generation
	private StringBuffer java_package;
	private StringBuffer frame_inout;
	private StringBuffer frame_seg;
	// segments for sample file generation
	private StringBuffer sample_seg;
	// and other stuff
	private int serial = 1779;

	BenchmarkSourceGenerator(Benchmark bm, String pkgname) {
		this.bm_ = bm;
		java_package = new StringBuffer();
		java_package.append("package " + pkgname + ";\n");

		frame_inout = new StringBuffer();
		frame_seg = new StringBuffer();

		sample_seg = new StringBuffer();
	}

	RuntimeSource translate() throws NBenchException {
		output = new RuntimeSource();
		this.bm_.accept(this);
		output.frame_file = frame_seg;
		output.sample_file = sample_seg;
		return output;
	}

	@Override
	public void visit(Benchmark benchmark) throws NBenchException {
		int i;
		//
		// Frame host variable
		//
		if (benchmark.frames != null) {
			frame_seg.append(java_package);
			frame_seg.append("import java.util.Map;\n");
			frame_seg.append("import nbench.common.ValueType;\n");
			frame_seg.append("import nbench.common.NBenchException;\n");
			frame_seg.append("import nbench.common.BackendEngineClientIfs;\n");
			frame_seg.append("import nbench.engine.HostVarImpl;\n");
			frame_seg.append("import nbench.engine.BaseHostVar;\n");
			frame_seg.append("\n\n");

			frame_seg.append("public class FrameHostVar {\n");
			frame_seg.append("private BackendEngineClientIfs be;\n");
			// constructor
			frame_seg
					.append("public FrameHostVar(BackendEngineClientIfs be) {this.be = be;}\n");
			for (i = 0; i < benchmark.frames.length; i++) {
				benchmark.frames[i].accept(this);
			}

			if (frame_inout != null) {
				frame_seg.append(frame_inout);
			}
			frame_seg.append("}\n");
		}

		//
		// Sample host variable
		//
		if (benchmark.samples != null) {
			sample_seg.append(java_package);
			sample_seg.append("import nbench.common.SampleIfs;\n");
			sample_seg.append("import nbench.common.ValueType;\n");
			sample_seg.append("import nbench.common.NBenchException;\n");
			sample_seg.append("import nbench.sample.SampleFactory;\n");
			sample_seg.append("\n\n");

			// start
			sample_seg.append("public class SampleHostVar {\n");
			// public member
			for (i = 0; i < benchmark.samples.length; i++) {
				sample_seg.append("public SampleIfs "
						+ benchmark.samples[i].name + ";\n");
			}
			// constructor
			sample_seg.append("public SampleHostVar() throws Exception {\n");
			for (i = 0; i < benchmark.samples.length; i++) {
				sample_seg.append(benchmark.samples[i].name
						+ "= SampleFactory.createSample(");
				sample_seg.append("\"" + benchmark.samples[i].type + "\"");
				sample_seg.append(", ValueType."
						+ ValueType.typeOfId(benchmark.samples[i].elem_type));
				sample_seg.append(", \"" + benchmark.samples[i].value + "\"");
				sample_seg.append(");\n");
			}
			sample_seg.append("}\n");
			// end
			sample_seg.append("}\n\n");
		}
	}

	private String str2AttrName(String str) {
		return Character.toUpperCase(str.charAt(0)) + str.substring(1);
	}

	private void makeArgList(StringBuffer seg, int numArg, String prefix) {
		int i;
		for (i = 0; i < numArg; i++) {
			if (i > 0) {
				seg.append(", ");
			}
			seg.append(prefix + i);
		}
	}

	private void makeParameterClass(NameAndTypeIfs nat[], String stuff)
			throws NBenchException {
		int i;

		frame_inout.append("class " + stuff + " extends BaseHostVar {\n");
		frame_inout.append("public static final long serialVersionUID = "
				+ serial++ + "L;\n");
		if (nat == null) {
			frame_inout.append(stuff
					+ "() throws Exception { super(0); }\n}\n\n");
		} else {
			// type 1 constructor
			frame_inout.append(stuff + "(");
			makeArgList(frame_inout, nat.length, "Object arg");
			frame_inout.append(") throws Exception {\n");

			frame_inout.append("super(" + nat.length + ");\n");
			for (i = 0; i < nat.length; i++) {
				frame_inout.append("attrPut(");
				frame_inout.append("\"" + nat[i].getName() + "\"");
				frame_inout.append(", ValueType."
						+ ValueType.typeOfId(nat[i].getType()));
				frame_inout.append(", arg" + i + ");\n");
			}
			frame_inout.append("}\n\n");
			// type 2 constructor
			frame_inout.append(stuff + "() {\n");
			frame_inout.append("super(" + nat.length + ");\n");
			for (i = 0; i < nat.length; i++) {
				frame_inout.append("put(");
				frame_inout.append("\"" + nat[i].getName()
						+ "\", undefined);\n");
			}
			frame_inout.append("}\n\n");
			// JavaBeans style attribute setter/getter
			for (i = 0; i < nat.length; i++) {
				// getter
				frame_inout.append("public Object get");
				frame_inout.append(str2AttrName(nat[i].getName())
						+ "() throws Exception {\n");
				frame_inout.append("return attrGet(\"" + nat[i].getName()
						+ "\");\n");
				frame_inout.append("}\n\n");
				// setter
				frame_inout.append("public void set"
						+ str2AttrName(nat[i].getName()));
				frame_inout.append("(Object arg) throws Exception {\n");
				frame_inout.append("attrPut(");
				frame_inout.append("\"" + nat[i].getName() + "\"");
				frame_inout.append(", ValueType."
						+ ValueType.typeOfId(nat[i].getType()));
				frame_inout.append(", arg);\n");
				frame_inout.append("}\n\n");
			}

			frame_inout.append("}\n\n");
		}
	}

	@Override
	public void visit(Frame frame) throws NBenchException {
		// make in/out object template
		makeParameterClass(frame.in, frame.name + "In");
		makeParameterClass(frame.out, frame.name + "Out");

		// make frame method signature
		frame_seg.append("public ");
		if (frame.out != null) {
			frame_seg.append("Object ");
		} else {
			frame_seg.append("void ");
		}
		frame_seg.append(frame.name);
		frame_seg.append("(");
		if (frame.in != null) {
			makeArgList(frame_seg, frame.in.length, "Object arg");
		}
		frame_seg.append(") throws Exception {\n");

		if (frame.in != null) {
			// declare in/out object
			frame_seg.append(frame.name + "In" + " IN;\n");
			frame_seg.append(frame.name + "Out" + " OUT;\n");
			// make new input/output object instance
			frame_seg.append("IN = new " + frame.name + "In(");
			makeArgList(frame_seg, frame.in.length, "arg");
			frame_seg.append(");\n");
			frame_seg.append("OUT = new " + frame.name + "Out();\n");
			// call back-end engine client interface function
			frame_seg.append(String.format("be.execute(\"%s\", IN, OUT);\n",
					frame.impl));
		} else {
			frame_seg.append(String.format(
					"Map<String, Object> OUT = be.execute(\"%s\", null);\n",
					frame.impl));
		}

		// return part
		if (frame.out != null) {
			frame_seg.append("return (HostVarImpl) OUT;");
		} else {
			frame_seg.append("return;\n");
		}
		frame_seg.append("}\n\n");
	}

	@Override
	public void visit(Mix mix) throws NBenchException {
		throw new NBenchException("should not be called");
	}

	@Override
	public void visit(Sample sample) throws NBenchException {
		throw new NBenchException("should not be called");
	}

	@Override
	public void visit(Transaction transaction) throws NBenchException {
		throw new NBenchException("should not be called");
	}
}

public class RuntimeSourceFactory {

	public static RuntimeSource compile(String pkgname, Benchmark benchmark)
			throws NBenchException {
		BenchmarkSourceGenerator sg = new BenchmarkSourceGenerator(benchmark,
				pkgname);
		return sg.translate();
	}

	public static RuntimeSource compile(FrameImpl fi) throws NBenchException {
		throw new NBenchException("implement this");
	}
}
