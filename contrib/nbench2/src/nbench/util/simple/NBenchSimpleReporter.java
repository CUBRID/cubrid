package nbench.util.simple;

import java.io.File;
import java.util.LinkedList;
import java.util.ListIterator;

public class NBenchSimpleReporter {

	static LinkedList<File> pivotFiles(File files[], ReportSpec rs) {
		LinkedList<File> file_list = new LinkedList<File>();

		// pivot files from the files array
		for (int i = 0; i < files.length; i++) {
			File file = files[i];
			String path = file.getPath();
			String[] ids = path.split(File.separator);
			String[] keys = new String[2];
			keys[0] = ids[ids.length - 3];
			keys[1] = ids[ids.length - 1];

			boolean match = true;
			for (int j = 0; j < keys.length; j++) {
				if (rs.getFilter(j).getKey(keys[j]) == null) {
					match = false;
					break;
				}

			}
			if (match) {
				file_list.add(file);
			}
		}
		return file_list;
	}

	public static void main(String[] args) {
		String usage = "usage: NBenchSimpleReporter <from> <to> <interval> <spec> <log files> \n"
				+ "  <from>         : seconds from benchmark start\n"
				+ "  <to>           : seconds from benchmark start\n"
				+ "  <interval>     : report interval for the range <from> ~ <to>\n"
				+ "  <filter-spec>  : <gen>/<thr>/<type>/<name>[/<method>]\n"
				+ "      <gen|thr|name|method>   ?|*|<id>\n"
				+ "      <type>                  T|F|Q  (T:transaction, F:frame, Q:query)\n";
		try {

			if (args.length < 5) {
				System.out.println(usage);
				System.exit(0);
			}

			int i;
			int from = Integer.valueOf(args[0]);
			int to = Integer.valueOf(args[1]);
			int interval = Integer.valueOf(args[2]);
			String spec = args[3];
			File[] files = new File[args.length - 4];

			for (i = 0; i + 4 < args.length; i++) {
				files[i] = new File(args[i + 4]);
				if (!files[i].exists() || !files[i].isFile()) {
					throw new Exception("invalid file:" + args[i + 4]);
				}
			}

			// make log file and iterate over time
			ReportSpec report_spec = new ReportSpec(spec);
			LinkedList<File> target_file_list = pivotFiles(files, report_spec);
			LinkedList<LogFile> log_files = new LinkedList<LogFile>();
			TimeZone zone = new TimeZone(from, to, interval);

			/* <gen>/perf_log/<mix_file> */
			for (File tf : target_file_list) {
				String[] segs = tf.getPath().split(File.separator);
				String[] keys = new String[2];
				keys[0] = segs[segs.length - 3];
				keys[1] = segs[segs.length - 1];
				log_files.add(new LogFile(keys, tf, zone));
			}

			Summary top_summary = new Summary("*", null);
			do {
				ListIterator<LogFile> li = log_files.listIterator();
				int num_consume = 0;
				boolean line_remains = false;

				while (li.hasNext()) {
					Summary curr_summary = top_summary;
					LogFile log_file = li.next();
					LogLine log_line;

					// file filtering
					for (i = 0; i < log_file.keys.length; i++) {
						curr_summary = report_spec.getFilter(i).map(
								log_file.keys[i], curr_summary);
						if (curr_summary == null) {
							log_file.close();
							li.remove();
							break;
						}
					}

					if (curr_summary == null) {
						continue;
					}

					Summary file_summary = curr_summary;
					while ((log_line = log_file.next()) != null) {
						int fi = log_file.keys.length;

						for (i = 0; i < log_line.keys.length; i++) {
							curr_summary = report_spec.getFilter(fi + i).map(
									log_line.keys[i], curr_summary);
							if (curr_summary == null) {
								break;
							}
						}

						if (curr_summary != null) {
							curr_summary.consume(log_line.stat);
							num_consume++;
						}
						// restore summary
						curr_summary = file_summary;
					}

					if (log_file.hasNext()) {
						line_remains = true;
					}
				}

				if (!line_remains) {
					break;
				}

				// One iteration done. Print summary
				System.out.println(zone.zoneToString());
				if (num_consume > 0) {
					top_summary.printSummary(System.out);
					top_summary = new Summary("*", null);
				} else {
					System.out.println("<N/A>");
				}

				// Next Iteration
				zone.nextZone();
			} while (!zone.outOfRange());

			for (LogFile log_file : log_files) {
				log_file.close();
			}
		} catch (Exception e) {
			e.printStackTrace();
		}
	}
}
