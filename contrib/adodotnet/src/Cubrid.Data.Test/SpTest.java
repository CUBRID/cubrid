/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */
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