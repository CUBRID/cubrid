package nbench.sample;

import java.util.Random;
import nbench.common.SampleIfs;

public class BooleanSample implements SampleIfs {
	static Boolean trueObj = new Boolean(true);
	static Boolean falseObj = new Boolean(false);

	private double d;
	private Random rand;
	
	public BooleanSample(double d) {
		this.d = d;
		rand = new Random();
	}

	@Override
	public Object nextValue() throws Exception {
		if(rand.nextDouble() < d) {
			return trueObj;
		}
		return falseObj;
	}

}
