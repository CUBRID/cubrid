package nbench.benchmark.cc;

import java.util.PriorityQueue;

public class QEngine implements Runnable {
	private long curr_time;
	private GenContext context;
	private PriorityQueue<QItem> queue;

	public QEngine(GenContext context) {
		this.context = context;
		this.queue = new PriorityQueue<QItem>();
	}

	public void run() {
		QItem item;
		curr_time = context.getStartTime();
		int emit_count = 0;
		try {
			while ((item = queue.poll()) != null) {
				curr_time = item.getTime();
				emit_count += item.process(this, curr_time, context);
				if (emit_count > 0 && emit_count % 1000 == 0) {
					System.out.println("QSZ:" + queue.size() + " EMT:"
							+ emit_count);
				}
			}
		} catch (Exception e) {
			e.printStackTrace();
			return;
		}
		System.out.println("total " + emit_count + " comments issued");
	}

	public void registerQItem(QItem item) {
		queue.add(item);
	}

	public long getCurrentTime() {
		return curr_time;
	}
}
