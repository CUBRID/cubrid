package nbench.benchmark.cc;

public class SynchronizedObjectStore implements ObjectStoreIfs {
	private ObjectStoreIfs impl;
	
	public SynchronizedObjectStore(ObjectStoreIfs impl) {
		this.impl = impl;
	}
	
	@Override
	public synchronized int currCapacity() {
		return impl.currCapacity();
	}

	@Override
	public synchronized Object getObject() {
		return impl.getObject();
	}

	@Override
	public synchronized void setObject(Object object, boolean replace) {
		impl.setObject(object, replace);
	}

}
