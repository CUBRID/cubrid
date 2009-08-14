package nbench.util;

import java.io.OutputStream;
import java.util.Stack;

import nbench.common.PerfLogIfs;

public class NBenchFullLogger implements PerfLogIfs {
	private OutputStream ous;
	private long base_time;

	class StackItem {
		long st;
		long et;
		String name;

		StackItem(long st, long et, String name) {
			this.st = st;
			this.et = et;
			this.name = name;
		}
	}

	Stack<StackItem> logstack;

	@Override
	public void setupLog(OutputStream os, long base_time) {
		ous = os;
		this.base_time = base_time;
		logstack = new Stack<StackItem>();
	}

	@Override
	public void teardownLog() {
	}

	@Override
	public void startLogItem(long time, LogType type, String name) {
		long curr = time;
		try {
			StackItem item = new StackItem(curr, 0L, name);
			ous.write(("> " + item.name + " " + (item.st - base_time) + "\n")
					.getBytes());
			logstack.push(item);
		} catch (Exception e) {
			;
		}
	}

	@Override
	public void endLogItem(long time, LogType type, String name) {
		long curr = time;
		try {
			StackItem item = logstack.pop();
			if (!item.name.equals(name)) {
				;
			}
			item.et = curr;
			ous.write(("< " + item.name + " " + (item.et - item.st) + "\n")
					.getBytes());
		} catch (Exception e) {
			;
		}
	}

	@Override
	public void endsWithError(long time, LogType type, String emsg) {
		try {
			while (!logstack.empty()) {
				StackItem item = logstack.pop();
				ous
						.write(("<" + item.name + " " + (time - item.st) + "E:" + emsg)
								.getBytes());
			}
		} catch (Exception e) {
			;
		}
	}
}
