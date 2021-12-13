import java.sql.CallableStatement;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.ResultSet;
import java.sql.Types;

public class TestResultSet {
    public static void main(String[] args) {
        Connection conn = null;

        try {
            Class.forName("cubrid.jdbc.driver.CUBRIDDriver");
            conn = DriverManager.getConnection("jdbc:CUBRID:localhost:33500:demodb:::", "dba", "");

            CallableStatement cstmt = conn.prepareCall("?=CALL rset()");
            cstmt.registerOutParameter(1, Types.JAVA_OBJECT);
            cstmt.execute();
            ResultSet rs = (ResultSet) cstmt.getObject(1);

            System.out.println(rs);

            while (rs.next()) {
                System.out.println(rs.getString(1));
            }

            rs.close();
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
}
