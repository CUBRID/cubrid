package cubrid.jdbc.jci;

public class UStatementEntry {
	public static final int AVAILABLE = 0;
	public static final int HOLDING = 1;
	
	private UStatement stmt;
	int status;
	
	public UStatementEntry (UStatement entry) {
		this.stmt = entry;
		this.status = HOLDING;
	}

	public String getSql() {
		return stmt.getQuery();
	}
	
	public UStatement getStatement() {
		return stmt;
	}
	
	public synchronized void setStatus(int status) {
		this.status = status;
	}
	
	public int getStatus() {
		return status;
	}
	
	public boolean isAvailable () {
		return (status == AVAILABLE);
	}

	@Override
	public String toString() {
		return "UStatementEntry [sql=" + getSql() + ", stmt=" + stmt + ", status=" + status + "]";
	}
}