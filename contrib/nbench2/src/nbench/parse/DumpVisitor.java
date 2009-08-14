package nbench.parse;

import java.io.PrintStream;

public class DumpVisitor {
	private int indent;
	private PrintStream stream;

	public DumpVisitor(PrintStream stream) {
		this.indent = -1;
		this.stream = stream;
	}

	protected void print(String msg) {
		for (int i = 0; i < indent; i++) {
			stream.print('\t');
		}
		stream.print(msg);
	}

	protected void println(String msg) {
		print(msg);
		stream.print('\n');
	}
	
	protected void enter() {
		indent++;
	}
	protected void leave() {
		indent--;
	}

}
