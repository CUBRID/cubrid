package nbench.util.simple;

import java.util.ArrayList;

public class ReportSpec {

	private Filter[] filters;

	public ReportSpec(String spec) throws Exception {
		String[] fs = spec.split("/");
		String tqf_string = null;
		
		boolean all_all = true;
		boolean tqf_found = false;
		for (int i = 0; i < fs.length; i++) {
			if (!fs[i].equals("*")) {
				all_all = false;
			}
			if (fs[i].equals("T") || fs[i].equals("Q") || fs[i].equals("F")) {
				tqf_found = true;
				tqf_string = fs[i];
			}
		}
		
		if (all_all) {
			throw new Exception("at least T | Q | F should be specified");
		}
		
		if (!tqf_found) {
			throw new Exception("invalid filter spec:" + spec);
		}
		
		if(tqf_string.equals("Q") && fs.length != 5) {
			throw new Exception("Q specifier needs to filters below");
		}

		filters = new Filter[fs.length];
		for (int i = 0; i < fs.length; i++) {
			parseSpec(i, fs[i]);
		}
	}

	private void parseSpec(int i, String s) throws Exception {
		if (s.equals("*")) {
			filters[i] = new Filter(null, Filter.Type.ALL);
		} else if (s.equals("?")) {
			filters[i] = new Filter(null, Filter.Type.EACH);
		} else {
			ArrayList<String> list = new ArrayList<String>(1);
			list.add(s);
			filters[i] = new Filter(list, Filter.Type.GIVEN);
		}
	}

	public Filter getFilter(int i) {
		return filters[i];
	}
}
