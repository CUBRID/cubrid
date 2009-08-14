package nbench.sample;

import nbench.common.SampleIfs;

public class AutoIncrementSample implements SampleIfs {
	private int s;
	
	public AutoIncrementSample(int s) {
		this.s = s;
	}
	@Override
	public Object nextValue() throws Exception {
		int d = s;
		s = s + 1;
		return new Integer(d);
	}
}
