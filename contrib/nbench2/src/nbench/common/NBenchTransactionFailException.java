package nbench.common;

import java.sql.SQLException;

public class NBenchTransactionFailException extends NBenchException {
	private static final long serialVersionUID = 5764351772328772935L;

	private SQLException sqlException;

	public NBenchTransactionFailException() {
		super();
	}

	public NBenchTransactionFailException(SQLException e, String s) {
		super(s);
		this.sqlException = e;
	}

	public SQLException getSQLException() {
		return sqlException;
	}
}
