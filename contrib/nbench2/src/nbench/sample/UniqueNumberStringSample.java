package nbench.sample;
import nbench.common.SampleIfs;

public class UniqueNumberStringSample implements SampleIfs {
	private long num;

	public UniqueNumberStringSample(long seed) {
		num = seed;
	}

	@Override
	public Object nextValue() throws Exception {
		num++;
		if(num < 0) {
			throw new Exception("Wrap around");
		}
		String str = new Long(num).toString();
		return str;
	}

}
