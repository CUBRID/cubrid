package nbench.parse;
import nbench.common.NBenchException;

public class Benchmark {
	public String name;
	public Frame frames[];
	public String transaction_common;
	public Transaction transactions[];
	public Sample samples[];
	public Mix mixes[];
	
	public void accept(BenchmarkVisitor visitor) throws NBenchException {
		visitor.visit(this);
	}
}
