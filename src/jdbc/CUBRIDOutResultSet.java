package cubrid.jdbc.driver;

import java.sql.SQLException;

import cubrid.jdbc.jci.UConnection;
import cubrid.jdbc.jci.UStatement;

public class CUBRIDOutResultSet extends CUBRIDResultSet {
    private boolean created;

    private int srv_handle;

    private UConnection ucon;

    /*
     * ======================================================================= |
     * CONSTRUCTOR
     * =======================================================================
     */
    public CUBRIDOutResultSet(UConnection ucon, int srv_handle_id) {
        super(null);
        created = false;
        this.srv_handle = srv_handle_id;
        this.ucon = ucon;
        ucon.getCUBRIDConnection().addOutResultSet(this);
    }

    public void createInstance() throws Exception {
        if (created)
            return;
        if (srv_handle <= 0)
            throw new IllegalArgumentException();

        u_stmt = new UStatement(ucon, srv_handle);
        column_info = u_stmt.getColumnInfo();
        number_of_rows = u_stmt.getExecuteResult();

        created = true;
    }

    public void close() throws SQLException {
        if (is_closed) {
            return;
        }
        is_closed = true;

        clearCurrentRow();

        u_stmt.close();

        streams = null;
        u_stmt = null;
        column_info = null;
        error = null;
    }
}
