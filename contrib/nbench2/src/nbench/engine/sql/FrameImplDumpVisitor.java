package nbench.engine.sql;

import java.io.PrintStream;

import nbench.common.NBenchException;
import nbench.parse.DumpVisitor;

public class FrameImplDumpVisitor extends DumpVisitor implements
		FrameImplVisitor {
	public FrameImplDumpVisitor(PrintStream stream) {
		super(stream);
	}

	@Override
	public void visit(FrameImpl impl) throws NBenchException {
		int i;
		println("FRAME-IMPL:");
		if (impl.statements != null) {
			enter();
			println("FRAME-STATEMENTS:");
			for (i = 0; i < impl.statements.length; i++) {
				impl.statements[i].accept(this);
			}
			leave();
		}
		if (impl.scripts != null) {
			enter();
			println("FRAME-SCRIPTS:");
			for (i = 0; i < impl.scripts.length; i++) {
				impl.scripts[i].accept(this);
			}
			leave();
		}
	}

	@Override
	public void visit(FrameStatement stmt) throws NBenchException {
		enter();
		println("FRAME-STATEMENT:");
		enter();
		println("name:" + stmt.name);
		println("template:" + stmt.template);
		println("data:" + stmt.data);
		leave();
		leave();
	}

	@Override
	public void visit(FrameScript script) throws NBenchException {
		enter();
		println("FRAME-SCRIPT:");
		enter();
		println("impl:" + script.impl);
		println("script:" + script.script);
		leave();
		leave();
	}

}
