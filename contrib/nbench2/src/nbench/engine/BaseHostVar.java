package nbench.engine;
import java.util.HashMap;
import nbench.common.NBenchException;
import nbench.common.ValueType;

public class BaseHostVar extends HashMap<String, Object> implements HostVarImpl {
	public final static long serialVersionUID = 2L;
	protected final static Object undefined = new Object();
	
	protected BaseHostVar(int sz) {
		super(sz);
	}
	
	protected BaseHostVar() {
		super(16);
	}
	
	protected Object attrGet(String name) throws NBenchException {
		Object obj = get(name);
		if(obj == undefined) {
			throw new NBenchException("dereferencing undefined rattribute:" + name);
		}
		return obj;
	}

	protected void attrPut(String name, int type, Object obj) throws NBenchException  {
		put(name, ValueType.convertTo(type, obj));
	}
}
