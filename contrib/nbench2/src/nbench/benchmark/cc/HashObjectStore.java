package nbench.benchmark.cc;

import java.util.HashMap;
import java.util.Random;

public class HashObjectStore implements ObjectStoreIfs {
	private HashMap<Integer, Object> map;
	private int next_idx;
	private Random rand;

	public HashObjectStore() {
		map = new HashMap<Integer, Object>();
		rand = new Random();
	}

	@Override
	public int currCapacity() {
		return next_idx;
	}

	@Override
	public Object getObject() {
		if (next_idx > 0) {
			return map.get(rand.nextInt(next_idx));
		}
		return null;
	}

	@Override
	public void setObject(Object object, boolean replace) {
		if (replace && next_idx > 0) {
			map.put(rand.nextInt(next_idx), object);
		} else {
			map.put(next_idx++, object);
		}
	}

}
