package nbench.sample;
import nbench.common.SampleIfs;

public class SynchronizedSampleIfs implements SampleIfs {
	private SampleIfs sample;

	public SynchronizedSampleIfs(SampleIfs sample) {
		this.sample = sample;
	}

	@Override
	public Object nextValue() throws Exception {
		synchronized (sample) {
			return sample.nextValue();
		}
	}
}
