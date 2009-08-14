package nbench.engine.sql;

import nbench.common.NBenchException;

public class FrameStatement {
	public String name;
	public String template;
	public String data;

	public void accept(FrameImplVisitor visitor) throws NBenchException {
		visitor.visit(this);
	}
}
