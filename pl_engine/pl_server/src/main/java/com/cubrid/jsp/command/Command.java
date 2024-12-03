package com.cubrid.jsp.command;

import com.cubrid.jsp.ConnectionHandler;
import com.cubrid.jsp.context.Context;
import com.cubrid.jsp.protocol.Header;

public interface Command {
       void execute(Header header, Context ctx, ConnectionHandler connectionManager) throws Exception; 
}
