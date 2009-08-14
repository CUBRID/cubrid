package nbench.sample;

import nbench.common.SampleIfs;
import org.apache.commons.math.random.RandomDataImpl;


public class StringSample implements SampleIfs {
	private int minsz;
	private int maxsz;
	private RandomDataImpl rand;

	public StringSample(int minsz, int maxsz) {
		this.minsz = minsz;
		this.maxsz = maxsz;
		rand = new RandomDataImpl();
	}

	@Override
	public Object nextValue() throws Exception {
		int r = rand.nextInt(minsz, maxsz);
		return rand.nextHexString(r);
	}
}
