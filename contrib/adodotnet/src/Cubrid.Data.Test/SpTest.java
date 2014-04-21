import java.sql.*;
import cubrid.jdbc.driver.*;

public class SpTest
{
    public static String test1(int i)
    {
        return "cubrid" + i;
    }

    public static ResultSet test2()
    {
        try {
            Connection conn = null;
            Statement stmt = null;
            
            conn = DriverManager.getConnection("jdbc:default:connection:","","");
            stmt = conn.createStatement();
            ResultSet rs= stmt.executeQuery("SELECT * from t");
            ((CUBRIDResultSet) rs).setReturnable();

            return rs;
        } catch ( Exception e ) {
            e.printStackTrace();
        }
        
        return null;
     }
}