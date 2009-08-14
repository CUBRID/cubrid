package nbench.util.simple;

import java.io.PrintStream;
import java.util.LinkedList;

public class Summary {
	class PrintContext {
		String prefix;
		PrintStream out;

		PrintContext(PrintStream out) {
			this.out = out;
			prefix = "";
		}
	}

	private int level;
	private Summary parent;
	private LinkedList<Summary> children;
	StatItem item;

	public Summary(String name, Summary parent) {
		this.parent = parent;
		this.item = new StatItem(name);
		if (parent != null) {
			level = parent.level + 1;
			parent.addChild(this);
		} else {
			level = 0;
		}
		children = new LinkedList<Summary>();
	}

	public int getLevel() {
		return level;
	}

	private void addChild(Summary c) {
		children.add(c);
	}

	public synchronized void consume(StatItem item) {
		this.item.merge(item);

		if (parent != null) {
			parent.consume(item);
		}
	}

	private boolean isStartTrack(Summary summary) {
		if(summary.item.name.equals("*")) {
			int sz = summary.children.size();
			if(sz == 0) {
				return true;
			} else if(children.size() == 1) {
				return isStartTrack(summary.children.get(0));
			} else {
				return false;
			}
		} else {
			return false;
		}
	}
	
	private boolean isPrint() {
		int sz = children.size();
		if (sz != 1) {
			// should aggregate the below result
			return true;
		}
		Summary child = children.get(0);
		// check this pattern : * - * - * ==> true (I'm the most specific one)
		// false otherwise
		return isStartTrack(child);
	}

	void printSummaryProc(PrintContext pcontext) {
		String name = item.name;
		String prefix = pcontext.prefix;

		if (!name.equals("*")) {
			pcontext.prefix = pcontext.prefix + "/" + item.name;
			if (isPrint()) {
				System.out.println(pcontext.prefix + " "
						+ item.toReportString());
			}
		}

		for (Summary c : children) {
			c.printSummaryProc(pcontext);
		}
		pcontext.prefix = prefix;
	}

	public void printSummary(PrintStream out) {
		PrintContext pcontext = new PrintContext(out);
		printSummaryProc(pcontext);
	}
}
