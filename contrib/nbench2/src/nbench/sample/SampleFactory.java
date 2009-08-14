package nbench.sample;

import nbench.common.NBenchException;
import nbench.common.SampleIfs;
import nbench.common.ValueType;

public class SampleFactory {
	public static SampleIfs createSample(String type, int elem_type,
			String value) throws Exception {
		assert (value != null);

		// NOTE make below fast if there are many 'type' instance
		if (type.equals("round")) {
			return new RoundRobinSample(elem_type, value);
		} else if (type.equals("poisson")) {
			if (elem_type != ValueType.INTEGER) {
				throw new NBenchException("elem-type for " + type
						+ " should be INTEGER");
			}
			double d = Double.valueOf(value);
			return new PoissonSample(d);
		} else if (type.equals("random-string")) {
			if (elem_type != ValueType.STRING) {
				throw new NBenchException("elem-type for " + type
						+ " should be STRING");
			}
			String[] vals = value.split(":");
			if (vals.length != 2) {
				throw new NBenchException("value specification error:" + value);
			}
			int low = Integer.valueOf(vals[0]);
			int high = Integer.valueOf(vals[1]);
			return new StringSample(low, high);
		} else if (type.equals("random-ip")) {
			if (elem_type != ValueType.STRING) {
				throw new NBenchException("elem-type for " + type
						+ " should be STRING");
			}
			return new RandomIPSample();
		} else {
			throw new NBenchException("unsupported sample type:" + type);
		}
	}
}