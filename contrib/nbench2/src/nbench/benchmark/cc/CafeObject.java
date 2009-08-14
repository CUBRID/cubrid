package nbench.benchmark.cc;

public class CafeObject {
	private int table_id;
	private int cafe_id;
	private int cafe_type;
	private ObjectStoreIfs obj_store;

	CafeObject(int table_id, int cafe_id, int cafe_type) {
		this.table_id = table_id;
		this.cafe_id = cafe_id;
		this.cafe_type = cafe_type;
		this.obj_store = new SynchronizedObjectStore(new HashObjectStore());
	}

	public int getTableID() {
		return table_id;
	}

	public int getCafeID() {
		return cafe_id;
	}

	public int getCafeType() {
		return cafe_type;
	}

	public Object getPrivateObject() {
		return obj_store.getObject();
	}

	public void setNewPrivateObject(Object obj) {
		obj_store.setObject(obj, false);
	}
	
	public void setPrivateObject(Object obj) {
		obj_store.setObject(obj, true);
	}
}
