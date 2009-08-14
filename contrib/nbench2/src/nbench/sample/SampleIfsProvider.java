package nbench.sample;

import java.util.HashMap;
import nbench.common.SampleIfs;

public class SampleIfsProvider {
	private HashMap<String, SampleIfs> map;
	
	public SampleIfsProvider() {
		map = new HashMap<String, SampleIfs>();
	}
	
	public void addSampleIfs(String key, SampleIfs sample) {
		map.put(key, sample);
	}
	
	public SampleIfs getSampleIfs(String key) throws Exception {
		if(!map.containsKey(key)) {
			throw new Exception("no key:" + key);
		}
		return map.get(key);
	}
	
	public Object nextValue(String key) throws Exception {
		return map.get(key).nextValue();
	}
}
