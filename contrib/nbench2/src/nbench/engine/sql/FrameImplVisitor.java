package nbench.engine.sql;

import nbench.common.NBenchException;

public interface FrameImplVisitor {
	void visit(FrameImpl fimpl) throws NBenchException;

	void visit(FrameStatement stmt) throws NBenchException;

	void visit(FrameScript script) throws NBenchException;
}
