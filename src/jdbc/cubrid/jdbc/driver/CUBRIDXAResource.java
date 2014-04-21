/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */

package cubrid.jdbc.driver;

import java.io.PrintStream;

import javax.transaction.xa.XAException;
import javax.transaction.xa.XAResource;
import javax.transaction.xa.Xid;

import cubrid.jdbc.jci.UConnection;
import cubrid.jdbc.jci.UErrorCode;

/**
 * Title: CUBRID JDBC Driver Description:
 * 
 * @version 3.0
 */

public class CUBRIDXAResource implements XAResource {
	private CUBRIDXAConnection xacon;
	private PrintStream debug_out;
	private String xacon_key;

	protected CUBRIDXAResource(CUBRIDXAConnection xacon, String xacon_key) {
		this.xacon = xacon;
		this.xacon_key = xacon_key;

		debug_out = null;
		if (debug_out != null) {
			debug_out.println("CUBRIDXAResource(" + xacon_key + ")");
		}
	}

	/*
	 * javax.transaction.xa.XAResource interface
	 */

	public void commit(Xid xid, boolean onePhase) throws XAException {
		if (debug_out != null) {
			debug_out.println("CUBRIDXAResource.commit(" + xid + "," + onePhase
					+ ")");
		}

		CUBRIDXidInfo xidInfo = CUBRIDXidTable.getXid(xacon_key, xid);
		if (xidInfo == null)
			throw new XAException(XAException.XAER_NOTA);

		synchronized (xidInfo) {
			if (onePhase == true) {
				if (xidInfo.status != CUBRIDXidInfo.STATUS_NOFLAG)
					throw new XAException(XAException.XAER_PROTO);
			} else {
				if ((xidInfo.status != CUBRIDXidInfo.STATUS_PREPARED)
						&& (xidInfo.status != CUBRIDXidInfo.STATUS_RECOVERED)) {
					throw new XAException(XAException.XAER_PROTO);
				}
			}

			end_tran(xidInfo.ucon, xid, xidInfo.status, true);
			xidInfo.status = CUBRIDXidInfo.STATUS_COMPLETED;
		}

		CUBRIDXidTable.removeXid(xacon_key, xid);
	}

	public void end(Xid xid, int flags) throws XAException {
		if (debug_out != null) {
			debug_out
					.println("CUBRIDXAResource.end(" + xid + "," + flags + ")");
		}

		if ((flags != TMSUCCESS) && (flags != TMFAIL) && (flags != TMSUSPEND)) {
			throw new XAException(XAException.XAER_INVAL);
		}

		CUBRIDXidInfo xidInfo = CUBRIDXidTable.getXid(xacon_key, xid);
		if (xidInfo == null)
			throw new XAException(XAException.XAER_NOTA);

		synchronized (xidInfo) {
			if (xidInfo.status != CUBRIDXidInfo.STATUS_STARTED
					&& xidInfo.status != CUBRIDXidInfo.STATUS_SUSPENDED
					&& xidInfo.status != CUBRIDXidInfo.STATUS_NOFLAG) {
				throw new XAException(XAException.XAER_PROTO);
			}

			if (xacon.xa_end() == false)
				throw new XAException(XAException.XAER_RMERR);

			if (flags == TMSUSPEND)
				xidInfo.status = CUBRIDXidInfo.STATUS_SUSPENDED;
			else
				xidInfo.status = CUBRIDXidInfo.STATUS_NOFLAG;
		}
	}

	public void forget(Xid xid) throws XAException {
		if (debug_out != null) {
			debug_out.println("CUBRIDXAResource.forget()");
		}
	}

	public int getTransactionTimeout() throws XAException {
		if (debug_out != null) {
			debug_out.println("CUBRIDXAResource.getTransactionTimeout()");
		}

		return 0;
	}

	public boolean isSameRM(XAResource xares) throws XAException {
		if (debug_out != null) {
			debug_out.println("CUBRIDXAResource.isSameRM()");
		}

		if (xares instanceof CUBRIDXAResource) {
			if (xacon_key.compareTo(((CUBRIDXAResource) xares).xacon_key) == 0) {
				return true;
			}
		}

		return false;
	}

	public int prepare(Xid xid) throws XAException {
		if (debug_out != null) {
			debug_out.println("CUBRIDXAResource.prepare()");
		}

		CUBRIDXidInfo xidInfo = CUBRIDXidTable.getXid(xacon_key, xid);
		if (xidInfo == null)
			throw new XAException(XAException.XAER_NOTA);

		synchronized (xidInfo) {
			if (xidInfo.status != CUBRIDXidInfo.STATUS_NOFLAG)
				throw new XAException(XAException.XAER_PROTO);

			UConnection ucon = xidInfo.ucon;
			if (ucon == null)
				throw new XAException(XAException.XAER_RMERR);

			synchronized (ucon) {
				ucon.xa_prepare(xid);
				if (ucon.getRecentError().getErrorCode() != UErrorCode.ER_NO_ERROR)
					throw new XAException(XAException.XAER_RMERR);
			}

			xidInfo.status = CUBRIDXidInfo.STATUS_PREPARED;
		}

		return XA_OK;
	}

	public Xid[] recover(int flag) throws XAException {
		if (debug_out != null) {
			debug_out.println("CUBRIDXAResource.recover()");
		}

		UConnection ucon;
		try {
			ucon = xacon.createUConnection();
		} catch (Exception e) {
			throw new XAException(XAException.XAER_RMERR);
		}

		Xid[] xid = ucon.xa_recover();
		if (ucon.getRecentError().getErrorCode() != UErrorCode.ER_NO_ERROR)
			throw new XAException(XAException.XAER_RMERR);

		for (int i = 0; i < xid.length; i++) {
			CUBRIDXidInfo xidInfo = new CUBRIDXidInfo(xid[i], null,
					CUBRIDXidInfo.STATUS_RECOVERED);
			CUBRIDXidTable.putXidInfo(xacon_key, xidInfo);
		}
		ucon.close();

		return xid;
	}

	public void rollback(Xid xid) throws XAException {
		if (debug_out != null) {
			debug_out.println("CUBRIDXAResource.rollback()");
		}

		CUBRIDXidInfo xidInfo = CUBRIDXidTable.getXid(xacon_key, xid);
		if (xidInfo == null)
			throw new XAException(XAException.XAER_NOTA);

		synchronized (xidInfo) {
			/*
			 * if ((xidInfo.status != CUBRIDXidInfo.STATUS_PREPARED) &&
			 * (xidInfo.status != CUBRIDXidInfo.STATUS_RECOVERED)) { throw new
			 * XAException(XAException.XAER_PROTO); }
			 */

			end_tran(xidInfo.ucon, xid, xidInfo.status, false);
			xidInfo.status = CUBRIDXidInfo.STATUS_COMPLETED;
		}

		CUBRIDXidTable.removeXid(xacon_key, xid);
	}

	public boolean setTransactionTimeout(int seconds) throws XAException {
		if (debug_out != null) {
			debug_out.println("CUBRIDXAResource.setTransactionTimeout()");
		}

		return false;
	}

	public void start(Xid xid, int flags) throws XAException {
		if (debug_out != null) {
			debug_out.println("CUBRIDXAResource.start(" + xid + "," + flags
					+ ")");
		}

		if (checkXid(xid) == false)
			throw new XAException(XAException.XAER_INVAL);

		CUBRIDXidInfo xidInfo = CUBRIDXidTable.getXid(xacon_key, xid);

		if (flags == TMNOFLAGS) {
			if (xidInfo != null)
				throw new XAException(XAException.XAER_DUPID);

			UConnection ucon = xacon.xa_start(flags, null);
			if (ucon == null)
				throw new XAException(XAException.XAER_RMERR);

			xidInfo = new CUBRIDXidInfo(xid, ucon, CUBRIDXidInfo.STATUS_STARTED);
			CUBRIDXidTable.putXidInfo(xacon_key, xidInfo);
		} else if (flags == TMJOIN || flags == TMRESUME) {
			if (xidInfo == null)
				throw new XAException(XAException.XAER_NOTA);

			synchronized (xidInfo) {
				if (xidInfo.status != CUBRIDXidInfo.STATUS_NOFLAG
						&& xidInfo.status != CUBRIDXidInfo.STATUS_SUSPENDED) {
					throw new XAException(XAException.XAER_PROTO);
				}

				if (xacon.xa_start(flags, xidInfo.ucon) == null)
					throw new XAException(XAException.XAER_RMERR);

				xidInfo.status = CUBRIDXidInfo.STATUS_STARTED;
			}
		} else {
			throw new XAException(XAException.XAER_INVAL);
		}
	}

	private boolean checkXid(Xid xid) {
		byte[] gid = xid.getGlobalTransactionId();
		byte[] bid = xid.getBranchQualifier();

		if (gid == null || gid.length == 0 || gid.length > Xid.MAXGTRIDSIZE)
			return false;
		if (bid == null || bid.length == 0 || bid.length > Xid.MAXBQUALSIZE)
			return false;

		return true;
	}

	private void end_tran(UConnection ucon, Xid xid, int status, boolean type)
			throws XAException {
		if (ucon == null) {
			try {
				ucon = xacon.createUConnection();
			} catch (Exception e) {
				throw new XAException(XAException.XAER_RMERR);
			}
		}

		synchronized (ucon) {
			if (status == CUBRIDXidInfo.STATUS_RECOVERED)
				ucon.xa_endTransaction(xid, type);
			else
				ucon.endTransaction(type);

			if (ucon.getRecentError().getErrorCode() != UErrorCode.ER_NO_ERROR)
				throw new XAException(XAException.XAER_RMFAIL);
		}

		ucon = xacon.xa_end_tran(ucon);
		if (ucon != null) {
			synchronized (ucon) {
				ucon.close();
			}
		}
	}
}
