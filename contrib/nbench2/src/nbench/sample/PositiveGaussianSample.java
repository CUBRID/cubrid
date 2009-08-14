package nbench.sample;
import nbench.common.SampleIfs;
import org.apache.commons.math.random.RandomDataImpl;


public class PositiveGaussianSample implements SampleIfs {
	private int mean;
	private int stddev;
	RandomDataImpl rand = new RandomDataImpl();
	
	public PositiveGaussianSample(int mean, int stddev) {
		this.mean = mean;
		this.stddev = stddev;
	}
	
	@Override
	public Object nextValue() throws Exception {
		int d = (int) rand.nextGaussian(mean, stddev);
		if(d < 1) {
			d = 1;
		}
		return new Integer(d);
	}
}
