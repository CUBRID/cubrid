package nbench.benchmark.cc;

// Object implementing this interface should be thread safe
public interface ObjectStoreIfs {
	Object getObject();
	void setObject(Object object, boolean replace);
	int currCapacity();
}
