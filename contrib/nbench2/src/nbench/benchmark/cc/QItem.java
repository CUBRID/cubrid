package nbench.benchmark.cc;

public abstract class QItem implements Comparable<QItem> {
	long time;

	public QItem(long time) {
		this.time = time;
	}

	long getTime() {
		return time;
	}

	public int compareTo(QItem item) {
		long t = item.getTime();

		if (time > t) {
			return 1;
		} else if (time == t) {
			return 0;
		} else {
			return -1;
		}
	}

	abstract int process(QEngine engine, long vt, GenContext context)
			throws Exception;
}
