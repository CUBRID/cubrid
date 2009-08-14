package nbench.parse;
import nbench.common.NBenchException;

public class Mix {
	public String name;
	public int nthread;
	public String setup;
	public String trs[];
	public String type;
	public String value;
	public int think_times[];
	public void accept(BenchmarkVisitor visitor) throws NBenchException {
		visitor.visit(this);
	}
}
