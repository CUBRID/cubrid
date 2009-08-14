package nbench.parse;
import nbench.common.NBenchException;

public class Sample {
	public String name;
	public String type;
	public int elem_type;
	public String value;
	public void accept(BenchmarkVisitor visitor) throws NBenchException {
		visitor.visit(this);
	}
}
