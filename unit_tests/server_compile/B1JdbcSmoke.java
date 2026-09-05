import java.math.BigDecimal;
import java.sql.Connection;
import java.sql.DatabaseMetaData;
import java.sql.Date;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Savepoint;
import java.sql.Statement;
import java.sql.Time;
import java.sql.Timestamp;
import javax.sql.XAConnection;
import javax.transaction.xa.XAResource;
import javax.transaction.xa.Xid;

/**
 * B1JdbcSmoke - the track-B standard smoke over the real JDBC driver (workspace#122 D5): connect ->
 * DDL -> DML -> cancel -> reconnect, against a DIRECT_HANDOFF broker, i.e. the driver's wire lands
 * 1-hop on cub_server's folded CAS speaker (stage B1).
 *
 * <p>Stage B2 (workspace#139) accumulates the "JDBC actually works" battery on top: handoff
 * stability across the driver's internal CHECK_CAS (B2-D11), statement pooling, the autocommit
 * state machine, savepoints, isolation, schema info (DatabaseMetaData), a bind/fetch type battery,
 * batches, generated keys, LOB, query timeout, and XA 2PC (B2-D12).
 *
 * <p>usage: java B1JdbcSmoke <broker_port> <dbname> <dbuser> <dbpasswd> prints "B1_JDBC: SUCCESS"
 * and exits 0 only if every step behaved.
 */
public class B1JdbcSmoke {
    static String url;
    static String user;
    static String pass;

    static Connection connect() throws SQLException {
        return DriverManager.getConnection(url, user, pass);
    }

    static void step(String name) {
        System.out.println("B1_JDBC: step " + name);
    }

    /**
     * The server-issued cancel token rides in the connect reply's pid slot (B1-D5) and the driver
     * keeps it as UConnection.casProcessId. Every handoff issues a distinct token, so a change
     * across requests proves the driver silently discarded the first connection (a second handoff).
     */
    static int casToken(Connection c) throws Exception {
        java.lang.reflect.Field f = c.getClass().getDeclaredField("u_con");
        f.setAccessible(true);
        Object ucon = f.get(c);
        return ucon.getClass().getField("casProcessId").getInt(ucon);
    }

    static boolean sslMode;

    public static void main(String[] args) throws Exception {
        if (args.length != 4 && args.length != 5) {
            System.err.println(
                    "usage: B1JdbcSmoke <broker_port> <dbname> <dbuser> <dbpasswd> [ssl]");
            System.exit(2);
        }
        sslMode = args.length == 5 && "ssl".equals(args[4]);
        boolean roMode = args.length == 5 && "ro".equals(args[4]);
        Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
        url =
                "jdbc:cubrid:127.0.0.1:"
                        + args[0]
                        + ":"
                        + args[1]
                        + ":::"
                        + (sslMode ? "?useSSL=true" : "");
        user = args[2];
        pass = args[3];

        if (roMode) {
            // stage B3 (#121 D1/D7/D8): against an ACCESS_MODE=RO broker the
            // adopted session must carry DB_CLIENT_TYPE_READ_ONLY_BROKER —
            // reads work, writes fail with ER_DB_NO_MODIFICATIONS (-581)
            step("ro_connect");
            Connection roc = connect();
            Statement ros = roc.createStatement();
            ResultSet rrs = ros.executeQuery("SELECT 1 FROM db_root");
            if (!rrs.next() || rrs.getInt(1) != 1) {
                throw new RuntimeException("read-only broker cannot read");
            }
            rrs.close();
            step("ro_write_refused");
            boolean refused = false;
            try {
                ros.executeUpdate("CREATE TABLE b3_ro_probe (id INT)");
            } catch (SQLException e) {
                refused = true;
                System.out.println(
                        "B1_JDBC: ro write raised: "
                                + e.getErrorCode()
                                + " "
                                + e.getMessage().trim());
                if (e.getErrorCode() != -581) {
                    throw new RuntimeException(
                            "expected ER_DB_NO_MODIFICATIONS (-581), got " + e.getErrorCode());
                }
            }
            if (!refused) {
                throw new RuntimeException("read-only broker accepted a write");
            }
            // the session must survive the refusal
            rrs = ros.executeQuery("SELECT 1 FROM db_root");
            if (!rrs.next()) {
                throw new RuntimeException("read-only session died after the refusal");
            }
            rrs.close();
            ros.close();
            roc.close();
            System.out.println("B1_JDBC: SUCCESS");
            return;
        }

        // 1. connect; the first statement makes the driver run CHECK_CAS on
        // the idle (OUT_TRAN) connection - if the folded speaker answers it
        // wrong, the driver discards this socket and forces a second handoff
        // (B2-D11), which the token comparison catches
        step("connect");
        Connection con = connect();
        int token0 = casToken(con);

        // 2. DDL (autocommit on by default)
        step("ddl");
        Statement stmt = con.createStatement();
        for (String t : new String[] {"b1_smoke", "b2_types", "b2_gk", "b2_lob"}) {
            try {
                stmt.executeUpdate("DROP TABLE IF EXISTS " + t);
            } catch (SQLException ignored) {
            }
        }
        stmt.executeUpdate("CREATE TABLE b1_smoke (id INT PRIMARY KEY, v VARCHAR(32))");

        if (casToken(con) != token0) {
            throw new RuntimeException(
                    "handoff not stable: token changed "
                            + token0
                            + " -> "
                            + casToken(con)
                            + " (the driver reconnected after CHECK_CAS)");
        }

        // 3. DML: bound inserts, count, update, verify
        step("dml");
        PreparedStatement ins = con.prepareStatement("INSERT INTO b1_smoke VALUES (?, ?)");
        for (int i = 1; i <= 2; i++) {
            ins.setInt(1, i);
            ins.setString(2, "row" + i);
            if (ins.executeUpdate() != 1) {
                throw new RuntimeException("insert " + i + " did not report 1 row");
            }
        }
        ins.close();
        ResultSet rs = stmt.executeQuery("SELECT COUNT(*) FROM b1_smoke");
        rs.next();
        if (rs.getInt(1) != 2) {
            throw new RuntimeException("count expected 2, got " + rs.getInt(1));
        }
        rs.close();

        // explicit transaction: update + commit, then verify
        con.setAutoCommit(false);
        stmt.executeUpdate("UPDATE b1_smoke SET v = 'updated' WHERE id = 1");
        con.commit();
        con.setAutoCommit(true);
        rs = stmt.executeQuery("SELECT v FROM b1_smoke WHERE id = 1");
        rs.next();
        if (!"updated".equals(rs.getString(1))) {
            throw new RuntimeException("update not visible after commit: " + rs.getString(1));
        }
        rs.close();

        // 4. statement pooling: the same SQL text prepared repeatedly must
        // come back from the pool (CAS_ER_STMT_POOLING server half) and keep
        // answering correctly as data changes underneath
        step("pooling");
        final String pooledSql = "SELECT COUNT(*) FROM b1_smoke WHERE id <= ?";
        for (int round = 1; round <= 3; round++) {
            PreparedStatement ps = con.prepareStatement(pooledSql);
            ps.setInt(1, 2);
            rs = ps.executeQuery();
            rs.next();
            if (rs.getInt(1) != 2) {
                throw new RuntimeException(
                        "pooling round " + round + ": expected 2, got " + rs.getInt(1));
            }
            rs.close();
            ps.close(); // returns the handle to the driver's pool
        }

        // 5. autocommit state machine: rollback discards, commit persists
        step("autocommit");
        con.setAutoCommit(false);
        stmt.executeUpdate("INSERT INTO b1_smoke VALUES (3, 'ghost')");
        con.rollback();
        rs = stmt.executeQuery("SELECT COUNT(*) FROM b1_smoke WHERE id = 3");
        rs.next();
        if (rs.getInt(1) != 0) {
            throw new RuntimeException("rolled-back insert is visible");
        }
        rs.close();
        stmt.executeUpdate("INSERT INTO b1_smoke VALUES (3, 'kept')");
        con.commit();
        con.setAutoCommit(true);
        rs = stmt.executeQuery("SELECT v FROM b1_smoke WHERE id = 3");
        rs.next();
        if (!"kept".equals(rs.getString(1))) {
            throw new RuntimeException("committed insert not visible");
        }
        rs.close();
        stmt.executeUpdate("DELETE FROM b1_smoke WHERE id = 3");

        // 6. savepoint: partial rollback inside an explicit transaction
        step("savepoint");
        con.setAutoCommit(false);
        stmt.executeUpdate("INSERT INTO b1_smoke VALUES (10, 'before_sp')");
        Savepoint sp = con.setSavepoint("b2sp");
        stmt.executeUpdate("INSERT INTO b1_smoke VALUES (11, 'after_sp')");
        con.rollback(sp);
        con.commit();
        con.setAutoCommit(true);
        rs = stmt.executeQuery("SELECT COUNT(*) FROM b1_smoke WHERE id IN (10, 11)");
        rs.next();
        if (rs.getInt(1) != 1) {
            throw new RuntimeException(
                    "savepoint: expected only id 10 to survive, got " + rs.getInt(1) + " rows");
        }
        rs.close();
        rs = stmt.executeQuery("SELECT id FROM b1_smoke WHERE id IN (10, 11)");
        rs.next();
        if (rs.getInt(1) != 10) {
            throw new RuntimeException(
                    "savepoint: survivor is id " + rs.getInt(1) + ", expected 10");
        }
        rs.close();
        stmt.executeUpdate("DELETE FROM b1_smoke WHERE id = 10");

        // 7. isolation get/set round-trip (fn_get/set_db_parameter)
        step("isolation");
        int iso0 = con.getTransactionIsolation();
        con.setTransactionIsolation(Connection.TRANSACTION_REPEATABLE_READ);
        if (con.getTransactionIsolation() != Connection.TRANSACTION_REPEATABLE_READ) {
            throw new RuntimeException("isolation did not round-trip to REPEATABLE_READ");
        }
        con.setTransactionIsolation(iso0);
        if (con.getTransactionIsolation() != iso0) {
            throw new RuntimeException("isolation did not restore to " + iso0);
        }

        // 8. schema info: DatabaseMetaData rides CAS_FC_SCHEMA_INFO
        step("metadata");
        DatabaseMetaData md = con.getMetaData();
        rs = md.getTables(null, null, "b1_smoke", null);
        if (!rs.next()) {
            throw new RuntimeException("getTables did not find b1_smoke");
        }
        rs.close();
        int cols = 0;
        rs = md.getColumns(null, null, "b1_smoke", "%");
        while (rs.next()) {
            cols++;
        }
        rs.close();
        if (cols != 2) {
            throw new RuntimeException("getColumns expected 2 columns, got " + cols);
        }

        // wf160: cas_stripped_column_name=ON (broker STRIPPED_COLUMN_NAME parity) —
        // a select-list column the user wrote qualified must come back bare
        rs = stmt.executeQuery("SELECT y.v FROM b1_smoke y ORDER BY y.id");
        String label = rs.getMetaData().getColumnName(1);
        rs.close();
        if (!"v".equals(label)) {
            throw new RuntimeException(
                    "qualified select-list column label not stripped: got '" + label + "'");
        }

        // OFF + refresh lifecycle: a session begun after the runtime flip must
        // see the qualified name; flipping back restores stripping
        stmt.executeUpdate("SET SYSTEM PARAMETERS 'cas_stripped_column_name=no'");
        try {
            Connection off = connect();
            rs = off.createStatement().executeQuery("SELECT y.v FROM b1_smoke y ORDER BY y.id");
            label = rs.getMetaData().getColumnName(1);
            rs.close();
            off.close();
            if (!"y.v".equals(label)) {
                throw new RuntimeException(
                        "cas_stripped_column_name=no not honored by new session: got '"
                                + label
                                + "'");
            }
        } finally {
            stmt.executeUpdate("SET SYSTEM PARAMETERS 'cas_stripped_column_name=yes'");
        }
        Connection on = connect();
        rs = on.createStatement().executeQuery("SELECT y.v FROM b1_smoke y ORDER BY y.id");
        label = rs.getMetaData().getColumnName(1);
        rs.close();
        on.close();
        if (!"v".equals(label)) {
            throw new RuntimeException(
                    "cas_stripped_column_name=yes restore not honored: got '" + label + "'");
        }

        // 9. type battery: bind -> store -> fetch round-trip per major type
        step("types");
        stmt.executeUpdate(
                "CREATE TABLE b2_types (c_int INT, c_big BIGINT, c_num NUMERIC(15,4),"
                        + " c_flt FLOAT, c_dbl DOUBLE, c_chr CHAR(8), c_vch VARCHAR(64),"
                        + " c_dt DATE, c_tm TIME, c_ts TIMESTAMP, c_dtt DATETIME, c_bit BIT VARYING(64))");
        PreparedStatement tins =
                con.prepareStatement("INSERT INTO b2_types VALUES (?,?,?,?,?,?,?,?,?,?,?,?)");
        BigDecimal num = new BigDecimal("12345.6789");
        Date dt = Date.valueOf("2026-08-29");
        Time tm = Time.valueOf("13:14:15");
        Timestamp ts = Timestamp.valueOf("2026-08-29 13:14:15");
        Timestamp dtt = Timestamp.valueOf("2026-08-29 13:14:15.123");
        byte[] bits = new byte[] {(byte) 0xDE, (byte) 0xAD, (byte) 0xBE, (byte) 0xEF};
        tins.setInt(1, -42);
        tins.setLong(2, 9007199254740993L);
        tins.setBigDecimal(3, num);
        tins.setFloat(4, 1.5f);
        tins.setDouble(5, Math.PI);
        tins.setString(6, "fixed");
        tins.setString(7, "variable text");
        tins.setDate(8, dt);
        tins.setTime(9, tm);
        tins.setTimestamp(10, ts);
        tins.setTimestamp(11, dtt);
        tins.setBytes(12, bits);
        if (tins.executeUpdate() != 1) {
            throw new RuntimeException("type battery insert failed");
        }
        tins.close();
        rs = stmt.executeQuery("SELECT * FROM b2_types");
        if (!rs.next()) {
            throw new RuntimeException("type battery row not found");
        }
        if (rs.getInt(1) != -42) {
            throw new RuntimeException("INT mismatch: " + rs.getInt(1));
        }
        if (rs.getLong(2) != 9007199254740993L) {
            throw new RuntimeException("BIGINT mismatch: " + rs.getLong(2));
        }
        if (rs.getBigDecimal(3).compareTo(num) != 0) {
            throw new RuntimeException("NUMERIC mismatch: " + rs.getBigDecimal(3));
        }
        if (rs.getFloat(4) != 1.5f) {
            throw new RuntimeException("FLOAT mismatch: " + rs.getFloat(4));
        }
        if (rs.getDouble(5) != Math.PI) {
            throw new RuntimeException("DOUBLE mismatch: " + rs.getDouble(5));
        }
        if (!"fixed".equals(rs.getString(6).trim())) {
            throw new RuntimeException("CHAR mismatch: '" + rs.getString(6) + "'");
        }
        if (!"variable text".equals(rs.getString(7))) {
            throw new RuntimeException("VARCHAR mismatch: '" + rs.getString(7) + "'");
        }
        if (!dt.toString().equals(rs.getDate(8).toString())) {
            throw new RuntimeException("DATE mismatch: " + rs.getDate(8));
        }
        if (!tm.toString().equals(rs.getTime(9).toString())) {
            throw new RuntimeException("TIME mismatch: " + rs.getTime(9));
        }
        if (rs.getTimestamp(10).getTime() / 1000 != ts.getTime() / 1000) {
            throw new RuntimeException("TIMESTAMP mismatch: " + rs.getTimestamp(10));
        }
        if (!dtt.equals(rs.getTimestamp(11))) {
            throw new RuntimeException("DATETIME mismatch: " + rs.getTimestamp(11));
        }
        byte[] gotBits = rs.getBytes(12);
        if (!java.util.Arrays.equals(bits, gotBits)) {
            throw new RuntimeException("BIT VARYING mismatch");
        }
        rs.close();

        // 10. batches: prepared-array and statement-batch execution
        step("batch");
        PreparedStatement bins = con.prepareStatement("INSERT INTO b1_smoke VALUES (?, ?)");
        for (int i = 20; i <= 22; i++) {
            bins.setInt(1, i);
            bins.setString(2, "batch" + i);
            bins.addBatch();
        }
        int[] counts = bins.executeBatch();
        bins.close();
        if (counts.length != 3) {
            throw new RuntimeException("prepared batch expected 3 results, got " + counts.length);
        }
        for (int c : counts) {
            if (c != 1 && c != Statement.SUCCESS_NO_INFO) {
                throw new RuntimeException("prepared batch element count " + c);
            }
        }
        Statement bstmt = con.createStatement();
        bstmt.addBatch("UPDATE b1_smoke SET v = 'batched' WHERE id = 20");
        bstmt.addBatch("DELETE FROM b1_smoke WHERE id = 22");
        bstmt.executeBatch();
        bstmt.close();
        rs = stmt.executeQuery("SELECT COUNT(*) FROM b1_smoke WHERE id BETWEEN 20 AND 22");
        rs.next();
        if (rs.getInt(1) != 2) {
            throw new RuntimeException("batch net rows expected 2, got " + rs.getInt(1));
        }
        rs.close();
        stmt.executeUpdate("DELETE FROM b1_smoke WHERE id BETWEEN 20 AND 22");

        // 11. generated keys (fn_get_generated_keys)
        step("generated_keys");
        stmt.executeUpdate("CREATE TABLE b2_gk (id INT AUTO_INCREMENT PRIMARY KEY, v VARCHAR(10))");
        PreparedStatement gins =
                con.prepareStatement(
                        "INSERT INTO b2_gk (v) VALUES (?)", Statement.RETURN_GENERATED_KEYS);
        gins.setString(1, "gk");
        gins.executeUpdate();
        rs = gins.getGeneratedKeys();
        if (!rs.next() || rs.getInt(1) < 1) {
            throw new RuntimeException("generated key not returned");
        }
        rs.close();
        gins.close();

        // 12. LOB (fn_lob_new / fn_lob_write / fn_lob_read)
        step("lob");
        stmt.executeUpdate("CREATE TABLE b2_lob (id INT PRIMARY KEY, b BLOB)");
        byte[] payload = new byte[8192];
        for (int i = 0; i < payload.length; i++) {
            payload[i] = (byte) (i * 31);
        }
        con.setAutoCommit(false);
        java.sql.Blob blob = con.createBlob();
        blob.setBytes(1, payload);
        PreparedStatement lins = con.prepareStatement("INSERT INTO b2_lob VALUES (1, ?)");
        lins.setBlob(1, blob);
        if (lins.executeUpdate() != 1) {
            throw new RuntimeException("lob insert failed");
        }
        lins.close();
        con.commit();
        rs = stmt.executeQuery("SELECT b FROM b2_lob WHERE id = 1");
        rs.next();
        java.sql.Blob got = rs.getBlob(1);
        byte[] gotBytes = got.getBytes(1, (int) got.length());
        rs.close();
        con.commit();
        con.setAutoCommit(true);
        if (!java.util.Arrays.equals(payload, gotBytes)) {
            throw new RuntimeException("lob round-trip mismatch: " + gotBytes.length + " bytes");
        }

        // 13. query timeout: the driver delegates setQueryTimeout to the CAS,
        // which lands it as the server's passive tdes deadline. SLEEP()'s
        // condvar wait (CBRD-26904) only checks that deadline on its own
        // wakeup, so the exception fires at SLEEP's end, not at the 2s mark —
        // measured identical on legacy cub_cas (10012ms) and 1-hop (10009ms).
        // CAS parity is the bar: assert the timeout error fires and the
        // session survives; the wall-time bound only guards against a hang.
        step("query_timeout");
        Statement tstmt = con.createStatement();
        tstmt.setQueryTimeout(2);
        long tBegin = System.currentTimeMillis();
        boolean timedOut = false;
        try {
            tstmt.executeQuery("SELECT SLEEP(10)");
        } catch (SQLException e) {
            timedOut = true;
            System.out.println("B1_JDBC: query timeout raised: " + e.getMessage().trim());
        }
        long tElapsed = System.currentTimeMillis() - tBegin;
        if (!timedOut) {
            throw new RuntimeException("SLEEP(10) was not timed out");
        }
        if (tElapsed > 15000) {
            throw new RuntimeException(
                    "query timeout took " + tElapsed + "ms - hang, not the CS-parity 10s");
        }
        tstmt.close();
        // the session must still work
        rs = stmt.executeQuery("SELECT COUNT(*) FROM b1_smoke");
        rs.next();
        rs.close();

        // 14. XA 2PC round-trip (fn_xa_prepare / fn_xa_recover / fn_xa_end_tran).
        // Skipped over SSL: CUBRIDXADataSource has no useSSL property channel,
        // so its plaintext dial would be refused by an SSL=ON broker.
        if (sslMode) {
            System.out.println("B1_JDBC: step xa (skipped in ssl mode)");
        } else {
            step("xa");
            // getXAConnection reads serverName/portNumber/databaseName only —
            // setUrl is not parsed on the XA path (CUBRIDXADataSource.java:70-73)
            cubrid.jdbc.driver.CUBRIDXADataSource xds = new cubrid.jdbc.driver.CUBRIDXADataSource();
            xds.setServerName("127.0.0.1");
            xds.setPortNumber(Integer.parseInt(args[0]));
            xds.setDatabaseName(args[1]);
            XAConnection xacon = xds.getXAConnection(user, pass);
            XAResource xares = xacon.getXAResource();
            Connection xc = xacon.getConnection();
            Xid xid =
                    new Xid() {
                        public int getFormatId() {
                            return 0x42;
                        }

                        public byte[] getGlobalTransactionId() {
                            return "b2smoke-gtrid".getBytes();
                        }

                        public byte[] getBranchQualifier() {
                            return "b2smoke-bqual".getBytes();
                        }
                    };
            xares.start(xid, XAResource.TMNOFLAGS);
            Statement xstmt = xc.createStatement();
            xstmt.executeUpdate("INSERT INTO b1_smoke VALUES (100, 'xa_row')");
            xstmt.close();
            xares.end(xid, XAResource.TMSUCCESS);
            int vote = xares.prepare(xid);
            if (vote != XAResource.XA_OK && vote != XAResource.XA_RDONLY) {
                throw new RuntimeException("xa prepare voted " + vote);
            }
            if (vote == XAResource.XA_OK) {
                xares.commit(xid, false);
            }
            Xid[] recovered = xares.recover(XAResource.TMSTARTRSCAN | XAResource.TMENDRSCAN);
            if (recovered == null) {
                throw new RuntimeException("xa recover returned null");
            }
            xc.close();
            xacon.close();
            rs = stmt.executeQuery("SELECT v FROM b1_smoke WHERE id = 100");
            if (!rs.next() || !"xa_row".equals(rs.getString(1))) {
                throw new RuntimeException("xa-committed row not visible");
            }
            rs.close();
            stmt.executeUpdate("DELETE FROM b1_smoke WHERE id = 100");
        }

        // 15. prepared-handle cap: cas_max_prepared_stmt_count=64 (set by
        // smoke_jdbc.sh) must reject the 65th open handle on one session —
        // proves the cas_* conf actually reaches the folded speaker (B2-D7)
        step("stmt_cap");
        Connection capCon = connect();
        PreparedStatement[] held = new PreparedStatement[65];
        int failedAt = -1;
        try {
            for (int i = 0; i < 65; i++) {
                try {
                    held[i] = capCon.prepareStatement("SELECT 1 + " + i + " FROM db_root");
                } catch (SQLException e) {
                    failedAt = i + 1;
                    System.out.println(
                            "B1_JDBC: stmt cap raised at handle "
                                    + failedAt
                                    + ": "
                                    + e.getMessage().trim());
                    break;
                }
            }
        } finally {
            for (PreparedStatement p : held) {
                if (p != null) {
                    p.close();
                }
            }
            capCon.close();
        }
        if (failedAt != 65) {
            throw new RuntimeException(
                    "stmt cap expected to fail at handle 65, failed at " + failedAt);
        }

        // 16. SHOW SESSION STATUS: the server view replacing the broker's
        // per-CAS slot statistics (B2-D10) — this session must be listed
        step("session_status");
        rs = stmt.executeQuery("SHOW SESSION STATUS");
        boolean foundSelf = false;
        int sessionRows = 0;
        while (rs.next()) {
            sessionRows++;
            if ("dba".equalsIgnoreCase(rs.getString("Db_user")) && rs.getLong("Num_queries") > 0) {
                foundSelf = true;
            }
        }
        rs.close();
        if (sessionRows < 1 || !foundSelf) {
            throw new RuntimeException(
                    "SHOW SESSION STATUS: rows=" + sessionRows + " foundSelf=" + foundSelf);
        }

        // 17. cancel: long-running SLEEP interrupted out-of-band ("QC" via the
        // broker -> control channel -> tran interrupt, #117 D4); the
        // connection must survive the cancelled statement
        step("cancel");
        final Statement slow = con.createStatement();
        Thread canceller =
                new Thread(
                        () -> {
                            try {
                                Thread.sleep(1500);
                                slow.cancel();
                            } catch (Exception e) {
                                e.printStackTrace();
                            }
                        });
        canceller.start();
        long begin = System.currentTimeMillis();
        boolean cancelled = false;
        try {
            slow.executeQuery("SELECT SLEEP(30)");
        } catch (SQLException e) {
            cancelled = true;
            System.out.println("B1_JDBC: cancel raised: " + e.getMessage().trim());
        }
        canceller.join();
        long elapsed = System.currentTimeMillis() - begin;
        if (!cancelled) {
            throw new RuntimeException("SLEEP(30) was not cancelled");
        }
        if (elapsed > 15000) {
            throw new RuntimeException("cancel took " + elapsed + "ms - interrupt did not land");
        }
        // the session must still work
        rs = stmt.executeQuery("SELECT COUNT(*) FROM b1_smoke");
        rs.next();
        if (rs.getInt(1) != 2) {
            throw new RuntimeException("post-cancel count expected 2, got " + rs.getInt(1));
        }
        rs.close();

        // 18. reconnect: close, open a fresh connection (a fresh handoff/token)
        step("reconnect");
        stmt.close();
        con.close();
        con = connect();
        stmt = con.createStatement();
        rs = stmt.executeQuery("SELECT v FROM b1_smoke WHERE id = 2");
        rs.next();
        if (!"row2".equals(rs.getString(1))) {
            throw new RuntimeException("reconnect read expected row2, got " + rs.getString(1));
        }
        rs.close();
        for (String t : new String[] {"b2_lob", "b2_gk", "b2_types", "b1_smoke"}) {
            stmt.executeUpdate("DROP TABLE " + t);
        }
        stmt.close();
        con.close();

        // 19. altHosts failover (stage B3, workspace#122 D5): the primary
        // host is a dead port, so the driver's whitelist/retry machinery
        // must move to the altHost — the only failover path left once the
        // CAS-side host loop is gone (#121 D3/D8)
        if (sslMode) {
            System.out.println("B1_JDBC: step alt_hosts (skipped in ssl mode)");
        } else {
            step("alt_hosts");
            int deadPort = Integer.parseInt(args[0]) + 17;
            String altUrl =
                    "jdbc:cubrid:127.0.0.1:"
                            + deadPort
                            + ":"
                            + args[1]
                            + ":::?altHosts=127.0.0.1:"
                            + args[0]
                            + "&rcTime=600";
            Connection alt = DriverManager.getConnection(altUrl, user, pass);
            Statement astmt = alt.createStatement();
            ResultSet ars = astmt.executeQuery("SELECT 1 FROM db_root");
            if (!ars.next() || ars.getInt(1) != 1) {
                throw new RuntimeException("altHosts failover connection cannot query");
            }
            ars.close();
            astmt.close();
            alt.close();
        }

        System.out.println("B1_JDBC: SUCCESS");
    }
}
