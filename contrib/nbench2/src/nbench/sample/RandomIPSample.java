package nbench.sample;
import nbench.common.SampleIfs;
import java.util.Random;
public class RandomIPSample implements SampleIfs {
	private Random rand;
	
	public RandomIPSample() {
		this.rand = new Random();
	}
	@Override
	public Object nextValue() throws Exception {
		//do not over accurate
		int a = rand.nextInt(256);
		int b = rand.nextInt(256);
		int c = rand.nextInt(256);
		int d = rand.nextInt(256);
		return a + "." + b + "." + c + "." + d;
	}

}
