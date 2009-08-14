package nbench.util.simple;

import java.io.OutputStream;
import java.util.HashMap;

import nbench.common.PerfLogIfs;

public class NBenchSimpleLogger implements PerfLogIfs {
	private OutputStream ous;
	private long base_time;
	private HashMap<LogType, HashMap<String, StatItem>> map;
	private int level;
	private LogItem[] log_items;
	// for periodic logging
	long prev_log_time;

	private class LogItem {
		long st;
		StatItem item;
	}

	public NBenchSimpleLogger() {
		map = null;
		log_items = null;
		base_time = System.currentTimeMillis();
	}

	private void emitMapItem(LogType type, StatItem item, long curr)
			throws Exception {
		StringBuffer sb = new StringBuffer();
		sb.append(curr - base_time);
		sb.append(':');
		sb.append(item.toLogString(type));
		sb.append('\n');
		ous.write(sb.toString().getBytes());
	}

	private void emitPeriodicLog(boolean forceAll, long curr) throws Exception {
		// per second
		if (curr > prev_log_time + 1000 || forceAll) {
			HashMap<String, StatItem> tmap = map.get(LogType.TRANSACTION);
			for (StatItem mi : tmap.values()) {
				emitMapItem(LogType.TRANSACTION, mi, curr);
			}

			tmap = map.get(LogType.FRAME);
			for (StatItem mi : tmap.values()) {
				emitMapItem(LogType.FRAME, mi, curr);
			}

			tmap = map.get(LogType.QUERY);
			for (StatItem mi : tmap.values()) {
				emitMapItem(LogType.QUERY, mi, curr);
			}
			prev_log_time = curr;
			initMap();
		}
	}

	@Override
	public void setupLog(OutputStream ous, long base_time) {
		this.ous = ous;
		this.base_time = base_time;
		log_items = new LogItem[3];
		log_items[0] = new LogItem();
		log_items[1] = new LogItem();
		log_items[2] = new LogItem();
		initMap();
		prev_log_time = base_time;
	}

	private void initMap() {
		this.map = new HashMap<LogType, HashMap<String, StatItem>>();
		map.put(LogType.TRANSACTION, new HashMap<String, StatItem>());
		map.put(LogType.FRAME, new HashMap<String, StatItem>());
		map.put(LogType.QUERY, new HashMap<String, StatItem>());
	}

	@Override
	public void teardownLog() {
		try {
			emitPeriodicLog(true, System.currentTimeMillis());
		} catch (Exception e) {
			;
		}
	}

	@Override
	public void startLogItem(long time, LogType type, String name) throws Exception {
		long curr = time;
		level++;
		LogItem li = log_items[level - 1];
		HashMap<String, StatItem> tmap = map.get(type);
		StatItem mi = tmap.get(name);

		if (mi == null) {
			mi = new StatItem(name);
			tmap.put(name, mi);
		}
		li.st = curr;
		li.item = mi;
	}

	@Override
	public void endLogItem(long time, LogType type, String name) throws Exception {
		long curr = time;
		LogItem li = log_items[level - 1];
		level--;
		int d = (int) (curr - li.st);
		li.item.succItem(d);
		if (type == LogType.TRANSACTION) {
			emitPeriodicLog(false, curr);
		}
	}

	@Override
	public void endsWithError(long time, LogType type, String emsg) throws Exception {
		for (int i = level - 1; i >= 0; i--) {
			log_items[i].item.failItem();
		}
		level = 0;
		// ends to transaction
		emitPeriodicLog(false, System.currentTimeMillis());
	}
}

