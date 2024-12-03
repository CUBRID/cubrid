package com.cubrid.jsp.command;

import com.cubrid.jsp.ConnectionHandler;
import com.cubrid.jsp.context.Context;
import com.cubrid.jsp.protocol.Header;

public class CommandHandler {
        private static final CommandRegistry commandRegistry = new CommandRegistry();
    
        public CommandHandler() {

        }
    
        public void handleRequest(Header header, Context ctx, ConnectionHandler connectionManager) throws Exception {
            Command command = commandRegistry.getCommand(header.code);
            command.execute(header, ctx, connectionManager);
        }
    }