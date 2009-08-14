package nbench.sample;
import nbench.common.SampleIfs;

public class SingleSample implements SampleIfs {
	private Object obj;
	
	public SingleSample(Object obj) {
		this.obj = obj;
	}
	@Override
	public Object nextValue() throws Exception {
		return obj;
	}
}
