# pl_engine/ — PL/CSQL Engine (Java)

## OVERVIEW

Java-based stored procedure compiler and runtime (~46k lines). Gradle build. Communicates with C engine via `src/sp/` bridge.

## STRUCTURE

```
pl_engine/
├── pl_server/src/main/java/com/cubrid/
│   ├── plcsql/compiler/     # PL/CSQL → Java bytecode compiler
│   │   ├── ast/             # AST node classes (96 files)
│   │   ├── visitor/         # AST visitors
│   │   ├── type/            # Type system
│   │   ├── serverapi/       # Server API bridge
│   │   └── error/           # Compiler errors
│   ├── jsp/                 # Java Stored Procedure runtime
│   │   ├── data/            # Data type marshalling (23 files)
│   │   ├── value/           # Value representation (20 files)
│   │   ├── jdbc/            # Internal JDBC driver (13 files)
│   │   ├── protocol/        # C↔Java protocol (10 files)
│   │   ├── code/            # Bytecode management
│   │   ├── classloader/     # Custom class loading
│   │   ├── impl/            # Implementation details
│   │   ├── context/         # Execution context
│   │   └── compiler/        # Java SP compiler
│   └── plcsql/builtin/      # Built-in PL/CSQL functions
├── gradle/                   # Gradle wrapper
├── CMakeLists.txt            # CMake integration for main build
└── settings.gradle.kts       # Gradle config
```

## COMMANDS

```bash
./gradlew build      # Build + test
./gradlew test       # Tests only
```

## CODEOWNER

All files → @beyondykk9

## NOTES

- CMake integration: `pl_engine/CMakeLists.txt` calls Gradle during main build
- Protocol between C and Java uses custom binary format in `jsp/protocol/`
- AST classes are boilerplate-heavy (96 files) — generated or semi-generated
