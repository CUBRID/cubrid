package nbench.parse;
import nbench.common.NBenchException;

public interface BenchmarkVisitor {
	void visit(Benchmark benchmark) throws NBenchException;
	void visit(Frame frame) throws NBenchException;
	void visit(Mix mix) throws NBenchException;
	void visit(Sample sample) throws NBenchException;
	void visit(Transaction transaction) throws NBenchException;
}
