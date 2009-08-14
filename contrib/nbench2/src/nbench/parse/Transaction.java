package nbench.parse;
import nbench.common.NBenchException;

public class Transaction {
	public String name;
	public String script;
	public int sla;
	public void accept(BenchmarkVisitor visitor) throws NBenchException {
		visitor.visit(this);
	}
}
