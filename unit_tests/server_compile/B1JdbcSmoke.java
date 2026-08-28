import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;

/**
 * B1JdbcSmoke - the track-B standard smoke over the real JDBC driver
 * (workspace#122 D5): connect -> DDL -> DML -> cancel -> reconnect, against a
 * DIRECT_HANDOFF broker, i.e. the driver's wire lands 1-hop on cub_server's
 * folded CAS speaker (stage B1).
 *
 * usage: java B1JdbcSmoke <broker_port> <dbname> <dbuser> <dbpasswd>
 * prints "B1_JDBC: SUCCESS" and exits 0 only if every step behaved.
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

    public static void main(String[] args) throws Exception {
        if (args.length != 4) {
            System.err.println("usage: B1JdbcSmoke <broker_port> <dbname> <dbuser> <dbpasswd>");
            System.exit(2);
        }
        Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
        url = "jdbc:cubrid:127.0.0.1:" + args[0] + ":" + args[1] + ":::";
        user = args[2];
        pass = args[3];

        // 1. connect
        step("connect");
        Connection con = connect();

        // 2. DDL (autocommit on by default)
        step("ddl");
        Statement stmt = con.createStatement();
        try {
            stmt.executeUpdate("DROP TABLE IF EXISTS b1_smoke");
        } catch (SQLException ignored) {
        }
        stmt.executeUpdate("CREATE TABLE b1_smoke (id INT PRIMARY KEY, v VARCHAR(32))");

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

        // 4. cancel: long-running SLEEP interrupted out-of-band ("QC" via the
        // broker -> control channel -> tran interrupt, #117 D4); the
        // connection must survive the cancelled statement
        step("cancel");
        final Statement slow = con.createStatement();
        Thread canceller = new Thread(() -> {
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

        // 5. reconnect: close, open a fresh connection (a fresh handoff/token)
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
        stmt.executeUpdate("DROP TABLE b1_smoke");
        stmt.close();
        con.close();

        System.out.println("B1_JDBC: SUCCESS");
    }
}
