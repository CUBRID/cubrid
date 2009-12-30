package nbench.util.simple;
import nbench.common.PerfLogIfs.LogType;


public class StatItem {
	String name;
	int tot;
	int succ;
	int fail;
	int min;
	int max;
	long sum;
	long sum2;

	public StatItem(String name) {
		this.name = name;
		init();
	}

	private void init() {
		tot = succ = fail = 0;
		min = Integer.MAX_VALUE;
		max = Integer.MIN_VALUE;
		sum = sum2 = 0;
	}

	public void merge(StatItem item) {
		tot += item.tot;
		succ += item.succ;
		fail += item.fail;
		if (min > item.min) {
			min = item.min;
		}
		if (max < item.max) {
			max = item.max;
		}
		sum += item.sum;
		sum2 += item.sum2;
	}

	public String logTypePrefix(LogType type) {
		if (type == LogType.TRANSACTION) {
			return "T";
		} else if (type == LogType.FRAME) {
			return "F";
		} else {
			return "Q";
		}
	}

	void succItem(int value) {
		tot++;
		succ++;
		if (value > max) {
			max = value;
		}
		if (value < min) {
			min = value;
		}
		sum += value;
		sum2 += value * value;
	}

	void failItem() {
		tot++;
		fail++;
	}

	private String statString() {
		return String.format(
				"tot:%d,succ:%d,fail:%d,min:%d,max:%d,sum:%d,sum2:%d", tot,
				succ, fail, min, max, sum, sum2);
	}

	public String toReportString() {
		
		String ous;
		
		if(succ == 0) {
			ous = String.format("tot:%d,succ:%d,fail:%d,min:<n/a>,max:<n/a>,avg:<n/a>,std:<n/a>",
					tot, succ, fail);
		} else {
			double mean;
			double stddev;
			mean = (double)sum/(double)succ;
			stddev = Math.sqrt(((double)sum2  - mean*mean)/(double)succ);
			ous = String.format("tot:%d,succ:%d,fail:%d,min:%d,max:%d,avg:%.3f,std:%.3f",
					tot, succ, fail, min, max, mean, stddev);
		}
		return ous;
	}
	
	public void readStatString(String format) throws Exception {
		String[] pairs = format.split(",");
		tot = Integer.valueOf(pairs[0].split(":")[1]);
		succ = Integer.valueOf(pairs[1].split(":")[1]);
		fail = Integer.valueOf(pairs[2].split(":")[1]);
		min = Integer.valueOf(pairs[3].split(":")[1]);
		max = Integer.valueOf(pairs[4].split(":")[1]);
		sum = Long.valueOf(pairs[5].split(":")[1]);
		sum2 = Long.valueOf(pairs[6].split(":")[1]);
	}

	public String toLogString(LogType type) {
		return logTypePrefix(type) + ":" + name + "=" + statString();
	}
}
