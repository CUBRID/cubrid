package nbench.sample;

import java.util.Random;
import nbench.common.SampleIfs;

public class RandomObjSample implements SampleIfs {
	private Object [] objs;
	private Random rand;
	
	public RandomObjSample(Object [] objs) {
		this.objs = objs;
		rand = new Random();
	}
	
	@Override
	public Object nextValue() throws Exception {
		int index = rand.nextInt(objs.length);
		return objs[index];
	}

}
