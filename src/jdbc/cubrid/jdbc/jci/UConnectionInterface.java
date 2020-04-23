package cubrid.jdbc.jci;

import java.io.IOException;

import javax.transaction.xa.Xid;

import cubrid.sql.CUBRIDOID;

public interface UConnectionInterface {
	
	// UFunctionCode.EXECUTE_BATCH_STATEMENT
	public UBatchResult batchExecute(String batchSqlStmt[], int queryTimeout);
	
	// UFunctionCode.RELATED_TO_COLLECTION
	public void dropElementInSequence(CUBRIDOID oid, String attributeName, int index);
	
	// UFunctionCode.END_TRANSACTION
	public void endTransaction(boolean type);
	
	// UFunctionCode.GET_BY_OID
	public UStatement getByOID(CUBRIDOID oid, String[] attributeName);

	// UFunctionCode.GET_DB_VERSION
	public String getDatabaseProductVersion();
	
	// UFunctionCode.GET_DB_PARAMETER
	public int getIsolationLevel();
	
	// UFunctionCode.GET_QUERY_INFO
	public String getQueryplanOnly(String sql);
	
	// UFunctionCode.GET_SCHEMA_INFO
	public UStatement getSchemaInfo(int type, String arg1, String arg2, byte flag, int shard_id);
	
	// UFunctionCode.RELATED_TO_COLLECTION
	public int getSizeOfCollection(CUBRIDOID oid, String attributeName);
	
	// UFunctionCode.PREPARE
	// private UStatement prepareInternal(String sql, byte flag, boolean recompile) throws IOException, UJciException
	
	// UFunctionCode.PUT_BY_OID
	public void putByOID(CUBRIDOID oid, String attributeName[], Object values[]);
	
	// UFunctionCode.SET_DB_PARAMETER
	public void setIsolationLevel(int level);
	public void setLockTimeout(int timeout);
	
	// UFunctionCode.SET_CAS_CHANGE_MODE
	public int setCASChangeMode(int mode);
	
	// UFunctionCode.XA_END_TRAN
	public void xa_endTransaction(Xid xid, boolean type);
	
	// UFunctionCode.XA_PREPARE
	public void xa_prepare(Xid xid);
	
	// UFunctionCode.XA_RECOVER
	public Xid[] xa_recover();
	
	// UFunctionCode.CHECK_CAS
	public boolean check_cas();
	public boolean check_cas(String msg);
	
	// UFunctionCode.RELATED_TO_OID
	public Object oidCmd(CUBRIDOID oid, byte cmd);
	
	// UFunctionCode.NEW_LOB
	public byte[] lobNew(int lob_type);
	
	// UFunctionCode.WRITE_LOB
	public int lobWrite(byte[] packedLobHandle, long offset, byte[] buf, int start, int len);
	
	// UFunctionCode.READ_LOB
	public int lobRead(byte[] packedLobHandle, long offset, byte[] buf, int start, int len);
	
	// UFunctionCode.RELATED_TO_COLLECTION
	// private void manageElementOfSequence(CUBRIDOID oid, String attributeName, int index, Object value, byte flag);
	// private void manageElementOfSet(CUBRIDOID oid, String attributeName, Object value, byte flag) throws UJciException, IOException
	
	// UFunctionCode.END_SESSION
	public void closeSession();
	
	// UFunctionCode.CON_CLOSE
	// public void close();
	// private void disconnect();
	
	// UFunctionCode.GET_SHARD_INFO
	public int shardInfo();
}
