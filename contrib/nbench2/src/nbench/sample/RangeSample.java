package nbench.sample;

import java.util.Random;
import nbench.common.SampleIfs;

public class RangeSample implements SampleIfs {
	private int low;
	private int high;
	private Random rand;
	
	public RangeSample(int low, int high) {
		this.low = low;
		this.high = high;
		this.rand = new Random();
	}
	@Override
	public Object nextValue() throws Exception {
		return new Integer(low + rand.nextInt(high - low + 1));
	}

}
