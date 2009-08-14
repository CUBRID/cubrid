package nbench.engine.sql;

import java.sql.Connection;
import java.sql.SQLException;
import javax.sql.DataSource;
import java.util.HashMap;
import java.util.Map;

import javax.script.Bindings;
import javax.script.Compilable;
import javax.script.CompiledScript;
import javax.script.ScriptEngine;

import nbench.common.BackendEngineClientIfs;
import nbench.common.PerfLogIfs;
import nbench.common.NBenchTransactionFailException;
import nbench.common.PerfLogIfs.LogType;
import nbench.common.NBenchException;

public class SQLClientImpl implements BackendEngineClientIfs {
	PerfLogIfs listener;
	Connection conn;
	DataSource ds;
	boolean reconnect;
	ScriptEngine se;
	FrameImpl impl;
	Bindings binds;
	TemplatePreparedStatementMap stmtMap;
	HashMap<String, CompiledScript> scriptMap;

	public class SQLClientControl {
		/* forward control to the Connection */

		public void commit() throws Exception {
			conn.commit();
		}

		public void rollback() throws Exception {
			conn.rollback();
		}

		public void setAutoCommit(Object obj) throws Exception {
			if (obj instanceof Boolean) {
				Boolean bv = (Boolean) obj;
				conn.setAutoCommit(bv);
			} else {
				throw new Exception("invalid argument for setAutoCommit:" + obj);
			}
		}

		public Boolean getAutoCommit() throws Exception {
			return conn.getAutoCommit();
		}
	}

	SQLClientImpl(ScriptEngine se, FrameImpl impl, PerfLogIfs listener,
			Connection conn, DataSource ds, boolean reconnect) throws Exception {
		int i;

		this.se = se;
		this.impl = impl;
		this.listener = listener;
		this.conn = conn;
		this.ds = ds;
		this.reconnect = reconnect;

		if (impl.scripts != null) {
			scriptMap = new HashMap<String, CompiledScript>(impl.scripts.length);

			for (i = 0; i < impl.scripts.length; i++) {
				Compilable comp = (Compilable) se;
				CompiledScript script = comp.compile(impl.scripts[i].script);
				scriptMap.put(impl.scripts[i].impl, script);
			}
		}
		// make host variables Statement
		binds = se.createBindings();
		initStatements(binds, conn);
	}

	private void initStatements(Bindings binds, Connection c) throws Exception {
		if (impl.statements != null) {
			stmtMap = new TemplatePreparedStatementMap(impl.statements.length,
					conn, listener);

			for (int i = 0; i < impl.statements.length; i++) {
				FrameStatement stmt = impl.statements[i];

				if (stmt.template == null) {
					stmtMap.addPrepared(stmt.name, stmt.data);
				} else {
					stmtMap.addTemplatePrepared(stmt.name, stmt.data,
							stmt.template);
				}
			}
			binds.put("Statement", stmtMap);
		}
	}

	@Override
	public void close() throws NBenchException {
		try {
			conn.close();
		} catch (SQLException e) {
			throw new NBenchException(e);
		}
	}

	@Override
	public Object getControlObject() throws NBenchException {
		return new SQLClientControl();
	}

	@Override
	public void execute(String name, Map<String, Object> in,
			Map<String, Object> out) throws NBenchException {
		try {
			CompiledScript script = scriptMap.get(name);

			if (script != null) {
				binds.put("IN", in);
				binds.put("OUT", out);
				listener.startLogItem(System.currentTimeMillis(),
						LogType.FRAME, name);
				script.eval(binds);
				listener.endLogItem(System.currentTimeMillis(), LogType.FRAME,
						name);
			}
		} catch (Exception e) {
			Throwable t = e.getCause();
			Throwable prev_t = null;

			while (t != null && (prev_t != t)) {
				if (t instanceof SQLException) {
					// debug stuff
					// e.printStackTrace();
					SQLException sql_e = (SQLException) t;

					try {
						if (reconnect && conn.isClosed()) {
							conn = ds.getConnection();
							initStatements(binds, conn);
						}
						throw new NBenchTransactionFailException(sql_e, ""
								+ sql_e.getErrorCode());
					} catch (Exception e1) {
						throw new NBenchException(e1);
					}
				}
				prev_t = t;
				t = t.getCause();
			}
			throw new NBenchException(e);
		}
	}

	@Override
	public void handleSessionAbort(Throwable t) {
		try {
			if (conn != null) {
				conn.rollback();
				conn.close();
			}
		} catch (Exception e) {
			;
		}
	}

	@Override
	public void handleTransactionAbort(Throwable t) {
		try {
			if (conn != null) {
				conn.rollback();
			}
		} catch (Exception e) {
			;
		}
	}
}
