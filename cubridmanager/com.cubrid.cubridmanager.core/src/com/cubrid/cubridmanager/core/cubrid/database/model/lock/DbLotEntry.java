package com.cubrid.cubridmanager.core.cubrid.database.model.lock;

import java.util.ArrayList;
import java.util.List;

public class DbLotEntry {
	private String open;
	private String oid;
	private String ob_type;
	private int num_holders;
	private int num_b_holders;
	private int num_waiters;

	private List<LockHolders> lockHoldersList;

	private List<LockWaiters> lockWaitersList;
	
	private List<BlockedHolders> blockHoldersList;

	public String getOpen() {
		return open;
	}

	public void setOpen(String open) {
		this.open = open;
	}

	public String getOid() {
		return oid;
	}

	public void setOid(String oid) {
		this.oid = oid;
	}

	public String getOb_type() {
		return ob_type;
	}

	public void setOb_type(String ob_type) {
		this.ob_type = ob_type;
	}

	public int getNum_holders() {
		return num_holders;
	}

	public void setNum_holders(int num_holders) {
		this.num_holders = num_holders;
	}

	public int getNum_b_holders() {
		return num_b_holders;
	}

	public void setNum_b_holders(int num_b_holders) {
		this.num_b_holders = num_b_holders;
	}

	public int getNum_waiters() {
		return num_waiters;
	}

	public void setNum_waiters(int num_waiters) {
		this.num_waiters = num_waiters;
	}

	public List<LockHolders> getLockHoldersList() {
		return lockHoldersList;
	}

	public void addLock_holders(LockHolders bean) {
		if (lockHoldersList == null)
			lockHoldersList = new ArrayList<LockHolders>();
		this.lockHoldersList.add(bean);
	}

	public List<LockWaiters> getLockWaitersList() {
		return lockWaitersList;
	}

	public void addWaiters(LockWaiters bean) {

		if (lockWaitersList == null)
			lockWaitersList = new ArrayList<LockWaiters>();
		this.lockWaitersList.add(bean);
	}

	public List<BlockedHolders> getBlockHoldersList() {
    	return blockHoldersList;
    }

	public void addB_holders(BlockedHolders bean) {
		if (blockHoldersList == null)
			blockHoldersList = new ArrayList<BlockedHolders>();
		this.blockHoldersList.add(bean);

	}
}
