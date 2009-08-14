package nbench.util.simple;

public class LogLine {
	int time;
	String[] keys;
	StatItem stat;

	// Divided into two parts (key ==, stat ~~~)
	// 1035:T:tr3=T:4,S:4,F:0,m:9,M:68,A:25.25,Std:24.80
	// -----===== ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// 1035:F:impl_4=T:15,S:15,F:0,m:7,M:19,A:9.40,Std:2.89
	// -----======== ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// 1035:Q:sql_5:executeQuery()=T:18,S:18,F:0,m:1,M:2,A:1.56,Std:0.50
	// -----====================== ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	public LogLine(String line) throws Exception {

		int i = line.indexOf('=');
		String my = line.substring(0, i);
		my = my.substring(my.indexOf(':') + 1);
		keys = my.split(":");

		if (!(keys[0].equals("T") || keys[0].equals("F") || keys[0].equals("Q"))) {
			throw new Exception("invalid line format" + line);
		}
		stat = new StatItem(null);
		String yours = line.substring(i + 1);
		stat.readStatString(yours);
	}
}
