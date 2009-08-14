package nbench.parse;

import nbench.common.NBenchException;
import nbench.common.NameAndTypeIfs;

public class Frame {
	public String name;
	public String impl;
	public NameAndTypeIfs in[];
	public NameAndTypeIfs out[];
	public void accept(BenchmarkVisitor visitor) throws NBenchException {
		visitor.visit(this);
	}
}