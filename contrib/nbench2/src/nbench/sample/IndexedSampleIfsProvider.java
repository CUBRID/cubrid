package nbench.sample;

import java.util.HashMap;
import nbench.common.SampleIfs;

public class IndexedSampleIfsProvider {
	private HashMap<Integer, SampleIfsProvider> map;
	
	public IndexedSampleIfsProvider() {
		map = new HashMap<Integer, SampleIfsProvider>();
	}
	
	public void addSampleIfs(int idx, String key, SampleIfs s) {
		SampleIfsProvider sp = map.get(idx);
		if(sp == null) {
			sp = new SampleIfsProvider();
			map.put(idx, sp);
		}
		sp.addSampleIfs(key, s);
	}
	
	public SampleIfsProvider getSampleIfsProvider(int idx) {
		SampleIfsProvider sp = map.get(idx);
		if(sp == null) {
			sp = new SampleIfsProvider();
			map.put(idx, sp);
		}
		return sp;
	}
	
	public SampleIfs getSampleIfs(int idx, String key) throws Exception {
		SampleIfsProvider sp = map.get(idx);
		if(sp == null) {
			throw new Exception("no sample provider of index:" + idx);
		}
		return sp.getSampleIfs(key);
	}
	
	public Object nextValue(int idx, String key) throws Exception {
		SampleIfsProvider sp = map.get(idx);
		if(sp == null) {
			throw new Exception("no sample provider with index:" + idx);
		}
		return sp.nextValue(key);
	}
}
