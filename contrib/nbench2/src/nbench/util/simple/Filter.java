package nbench.util.simple;

import java.util.Collection;
import java.util.HashMap;

public class Filter {
	public enum Type {ALL, EACH, GIVEN}
	
	private Type type;
	private Collection<String> filter;
	
	HashMap<String, HashMap<Summary, Summary>> summaries_map;

	public Filter(Collection<String> filter, Type type) {
		this.filter = filter;
		this.type = type;
		summaries_map = new HashMap<String, HashMap<Summary, Summary>>();
	}

	public String getKey(String name) {
		if (type == Type.ALL) {
			return "*";
		} else if (type == Type.EACH) {
			return name;
		} else {
			if (filter.contains(name)) {
				return name;
			}
		}
		return null;
	}
	
	public Summary map(String name, Summary parent) throws Exception  {
		String key = getKey(name);
		if(key == null) {
			return null;
		}
		HashMap<Summary, Summary> summaries = summaries_map.get(key);
		if(summaries == null) {
			summaries = new HashMap<Summary, Summary>();
			summaries_map.put(key, summaries);
		}
		
		Summary me = summaries.get(parent);
		if(me == null) {
			me = new Summary(key, parent);
			summaries.put(parent, me);
		}
		return me;
	}
}

