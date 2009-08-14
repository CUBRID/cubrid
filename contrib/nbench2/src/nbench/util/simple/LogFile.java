package nbench.util.simple;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.BufferedReader;

public class LogFile {
	String[] keys;
	BufferedReader reader;

	// for safe iteration over time partition [begin, end) ...
	String saved_line;
	boolean reach_eof;
	TimeZone zone;
	
	LogFile(String[] keys, File file, TimeZone zone)
			throws Exception {
		this.keys = keys;
		reader = new BufferedReader(new InputStreamReader(new FileInputStream(
				file)));
		
		this.zone = zone;
		saved_line = null;
		reach_eof = false;
	}

	public boolean hasNext() {
		return !(saved_line == null && reach_eof);
	}
	
	public LogLine next() throws Exception {
		String aline;
		while (true) {
			if (saved_line != null) {
				aline = saved_line;
				saved_line = null;
			} else {
				aline = reader.readLine();
			}
			
			if (aline == null) {
				reach_eof = true;
				return null;
			}

			int idx = aline.indexOf(':');
			String ts = aline.substring(0, idx);
			
			int last_time = Integer.valueOf(ts);
			TimeZone.ZoneRelation r = zone.getRelation(last_time);
			
			if (r == TimeZone.ZoneRelation.LEFT) {
				continue;
			} else if (r == TimeZone.ZoneRelation.IN) {
				return new LogLine(aline);
			} else {
				saved_line = aline;
				return null;
			}
		}
	}
	
	public void close() {
		try {
			if (reader != null) {
				reader.close();
			}
		} catch (IOException e) {
			;
		} finally {
			reader = null;
		}
		zone = null;
	}
}
