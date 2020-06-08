package cubrid.jdbc.jci;

public class UStatementHandlerCacheEntry {
	public static final int AVAILABLE = 0;
	public static final int HOLDING = 1;
	
	private UStatement stmt;
	boolean isAvailable;
	
	public UStatementHandlerCacheEntry (UStatement entry) {
		this.stmt = entry;
		this.isAvailable = false;
	}

	public String getSql() {
		return stmt.getQuery();
	}
	
	public UStatement getStatement() {
		return stmt;
	}
	
	public synchronized void setAvailable (boolean isAvailable) {
		this.isAvailable = isAvailable;
	}
	
	public boolean isAvailable () {
		return isAvailable;
	}
}