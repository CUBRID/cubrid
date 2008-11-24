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

import java.sql.SQLException;

import cubrid.jdbc.jci.UConnection;
import cubrid.jdbc.jci.UStatement;

public class CUBRIDOutResultSet extends CUBRIDResultSet
{
  private boolean created;

  private int srv_handle;

  private UConnection ucon;

  public CUBRIDOutResultSet(UConnection ucon, int srv_handle_id)
  {
    super(null);
    created = false;
    this.srv_handle = srv_handle_id;
    this.ucon = ucon;
    ucon.getCUBRIDConnection().addOutResultSet(this);
  }

  public void createInstance() throws Exception
  {
    if (created)
      return;
    if (srv_handle <= 0)
      throw new IllegalArgumentException();

    u_stmt = new UStatement(ucon, srv_handle);
    column_info = u_stmt.getColumnInfo();
    number_of_rows = u_stmt.getExecuteResult();

    created = true;
  }

  public void close() throws SQLException
  {
    if (is_closed)
    {
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
