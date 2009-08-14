package nbench.engine.sql;

import nbench.common.NBenchException;

public class FrameImpl {
	FrameStatement[] statements;
	FrameScript[] scripts;

	public void accept(FrameImplVisitor visitor) throws NBenchException {
		visitor.visit(this);
	}
}
