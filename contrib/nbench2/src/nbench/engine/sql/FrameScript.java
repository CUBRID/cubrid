package nbench.engine.sql;

import nbench.common.NBenchException;

public class FrameScript {
	public String impl;
	public String script;

	public void accept(FrameImplVisitor visitor) throws NBenchException {
		visitor.visit(this);
	}
}
