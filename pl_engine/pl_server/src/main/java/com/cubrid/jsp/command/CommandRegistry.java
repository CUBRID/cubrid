package com.cubrid.jsp.command;

import java.util.HashMap;
import java.util.Map;

public class CommandRegistry {
    private final Map<Integer, Command> commandMap = new HashMap<>();

    public CommandRegistry() {
        // Add more commands as needed
    }

    public Command getCommand(int requestCode) {
        // return commandMap.getOrDefault(requestCode, new DefaultCommand());
        return null;
    }
}
