package nbench.parse;

import java.io.PrintStream;
import nbench.common.NBenchException;
import nbench.common.NameAndTypeIfs;
import nbench.common.ValueType;

public class BenchmarkDumpVisitor extends DumpVisitor implements BenchmarkVisitor {
	
	public BenchmarkDumpVisitor(PrintStream stream) {
		super(stream);
	}
	
	@Override
	public void visit(Benchmark benchmark) throws NBenchException {
		int i;
		enter();
		println("NBENCHMARK:");
		println("\tname:" + benchmark.name);

		if (benchmark.frames != null) {
			println("TRANSACTION-FRAME:");
			for (i = 0; i < benchmark.frames.length; i++) {
				benchmark.frames[i].accept(this);
			}
		}
		if (benchmark.transaction_common != null) {
			println("TRANSACTION-COMMON:");
			println("\t" + benchmark.transaction_common);
		}
		if (benchmark.transactions != null) {
			println("TRANSACTION-DEFINITION:");
			for (i = 0; i < benchmark.transactions.length; i++) {
				benchmark.transactions[i].accept(this);
			}
		}
		if (benchmark.samples != null) {
			println("SAMPLE-SPACE:");
			for (i = 0; i < benchmark.samples.length; i++) {
				benchmark.samples[i].accept(this);
			}
		}
		if (benchmark.mixes != null) {
			println("TRANSACTION-MIX:");
			for (i = 0; i < benchmark.mixes.length; i++) {
				benchmark.mixes[i].accept(this);
			}
		}
		leave();
	}

	@Override
	public void visit(Frame frame) throws NBenchException {
		int i;

		enter();
		println("FRAME:");
		println("\tname:" + frame.name);
		println("\timpl:" + frame.impl);
		println("\tin:");
		if (frame.in == null) {
			println("<none>");
		} else {
			for (i = 0; i < frame.in.length; i++) {
				NameAndTypeIfs nat = frame.in[i];
				println("\t\t" + nat.getName() + ":"
						+ ValueType.typeOfId(nat.getType()));
			}
		}
		println("\tout:");
		if (frame.out == null) {
			println("<none>");
		} else {
			for (i = 0; i < frame.out.length; i++) {
				NameAndTypeIfs nat = frame.out[i];
				println("\t\t" + nat.getName() + ":"
						+ ValueType.typeOfId(nat.getType()));
			}
		}
		leave();
	}

	@Override
	public void visit(Mix mix) throws NBenchException {
		int i;
		
		enter();
		println("MIX:");
		println("\tname:" + mix.name);
		println("\tnthread:" + mix.nthread);
		println("\tsetup:" + mix.setup);
		print("trs:");
		for(i = 0; i < mix.trs.length; i++) {
			print(mix.trs[i]);
			if(i + 1 != mix.trs.length) {
				print(",");
			}
		}
		print("\n");
		println("type:" + mix.type);
		println("value:" + mix.value);
		print("think-time:");
		for(i = 0; i < mix.think_times.length; i++) {
			print("" + mix.think_times[i]);
			if(i + 1 != mix.think_times.length) {
				print(",");
			}
		}
		leave();
	}

	@Override
	public void visit(Sample sample) throws NBenchException {
		enter();
		println("SAMPLE:");
		println("\tname:" + sample.name);
		println("\ttype:" + sample.type);
		println("\telem-type:" + ValueType.typeOfId(sample.elem_type));
		println("\tvalue:" + sample.value);
		leave();
	}

	@Override
	public void visit(Transaction transaction) throws NBenchException {
		enter();
		println("TRANSACTION:");
		println("\tname:" + transaction.name);
		println("\tsla:" + transaction.sla);
		println("\tscript:" + transaction.script);
		leave();
	}
}
