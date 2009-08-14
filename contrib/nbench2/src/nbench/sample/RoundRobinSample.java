package nbench.sample;

import nbench.common.SampleIfs;
import nbench.common.ValueType;

public class RoundRobinSample implements SampleIfs {
	private Object[] values;
	private int index;
	private int len;
	
	public RoundRobinSample(int type, String spec) throws Exception {
		String[] vals = spec.split(",");
		this.len = vals.length;
		this.values = new Object[len];
		for(int i = 0; i < len; i++) {
			values[i] = ValueType.convertTo(type, vals[i]);
		}
		this.index = -1;
	}
	
	@Override
	public Object nextValue() throws Exception {
		index = index + 1;
		if(index >= values.length) {
			index = 0;
		}
		return values[index];
	}
}
