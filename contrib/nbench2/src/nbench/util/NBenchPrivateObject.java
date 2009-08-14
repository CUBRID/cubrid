package nbench.util;

import nbench.common.ResourceProviderIfs;
import nbench.common.UserHostVarIfs;
import java.util.HashMap;
import java.util.Map;

public class NBenchPrivateObject implements UserHostVarIfs {
	private HashMap<String, Object> map;

	public NBenchPrivateObject() {
		map = new HashMap<String, Object>();
	}

	public Object getPrivateObject(String name) {
		return map.get(name);
	}

	public void setPrivateObject(String name, Object obj) {
		map.put(name, obj);
	}

	@Override
	public void prepareSetup(Map<String, String> map, ResourceProviderIfs rp)
			throws Exception {
		for(String k : map.keySet()) {
			this.map.put(k, map.get(k));
		}
	}
}
