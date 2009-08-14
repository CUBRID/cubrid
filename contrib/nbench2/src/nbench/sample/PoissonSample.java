package nbench.sample;

import nbench.common.SampleIfs;
import org.apache.commons.math.random.RandomDataImpl;

/* ISSUE
 * It seems to be the better way to provide Sample.Random host object which is 
 * RandomDataImpl instance
 */
public class PoissonSample implements SampleIfs {
	RandomDataImpl impl;
	double lambda;
	Integer curr;
	
	public PoissonSample(double lambda) {
		impl = new RandomDataImpl();
		this.lambda = lambda;
	}
	
	@Override
	public Object nextValue() throws Exception {
		int v = (int)impl.nextPoisson(lambda);
		curr = new Integer(v);
		return curr;
	}
}
