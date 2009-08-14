package nbench.sample;
import nbench.common.SampleIfs;
import org.apache.commons.math.random.RandomDataImpl;


public class PositivePoissonSample implements SampleIfs {
	private int lambda;
	RandomDataImpl rand = new RandomDataImpl();
	
	public PositivePoissonSample(int lambda) {
		this.lambda = lambda;
	}
	
	@Override
	public Object nextValue() throws Exception {
		int d = (int) rand.nextPoisson(lambda);
		if(d <= 0) { 
			return 1;
		}
		return new Integer(d);
	}
}
