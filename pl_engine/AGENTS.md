# pl_engine/ — Java PL (Procedural Language) Engine

Gradle-based Java project. Runs as separate process, communicates with `cub_server` via Unix domain sockets.

## Structure

```
pl_engine/
├── CMakeLists.txt          # CMake integration (builds via Gradle)
├── settings.gradle.kts     # Gradle settings
├── pl_server/
│   ├── build.gradle.kts    # Dependencies, ANTLR, JUnit 5
│   └── src/
│       ├── main/
│       │   ├── antlr/      # ANTLR 4.9.3 grammar files (PL/CSQL parsing)
│       │   ├── java/com/cubrid/jsp/  # Main Java sources
│       │   └── resources/  # Configuration files
│       └── test/           # JUnit 5 tests
```

## Java Package Structure

```
com.cubrid.jsp/                   # SP runtime
├── Server                        # Main entry point
├── data/                         # Data handling
├── value/                        # Value conversion (SQL ↔ Java)
├── jdbc/                         # Internal JDBC for SP SQL execution
├── protocol/                     # Wire protocol with cub_server
├── code/                         # Code management
├── classloader/                  # SP class loading
├── impl/                         # SP implementation dispatch
├── context/                      # Execution context
├── compiler/                     # SP compilation
└── exception/                    # Error handling

com.cubrid.plcsql/                # PL/CSQL compiler
├── compiler/
│   ├── ast/                      # Abstract syntax tree nodes
│   │   └── loopOpt/              # Loop optimization
│   ├── visitor/                  # AST visitors
│   ├── type/                     # Type system
│   ├── serverapi/                # Server API interface
│   ├── error/                    # Compiler errors
│   └── annotation/               # Compiler annotations
├── builtin/                      # Built-in PL/CSQL functions
└── predefined/sp/                # Predefined stored procedures
```

## Build

```bash
# Via CMake (integrated)
ninja pl_server          # Build PL engine
ninja pl_unittest        # Run PL engine tests

# Via Gradle directly
cd pl_engine && ./gradlew build
cd pl_engine && ./gradlew test
```

## Dependencies

- **JDK 1.8+** (toolchain set to Java 8)
- **ANTLR 4.9.3** — PL/CSQL grammar parsing
- **junixsocket 2.8.3** — Unix domain socket communication
- **CUBRID JDBC** — for internal SQL execution from stored procedures
- **Apache Commons** — text, collections, lang3, io, compress
- **Netty 4.1.115** — buffer management
- **JUnit Jupiter 5.9.1** — testing

## Where to Look

| Task | File/Dir |
|------|----------|
| Fix PL engine startup | `com.cubrid.jsp.Server` |
| Fix PL/CSQL parsing | `src/main/antlr/` grammars |
| Fix SP execution | `com.cubrid.jsp/` handler classes |
| Fix type conversion | Type mapping classes in `com.cubrid.jsp/` |
| Fix communication | Socket handling classes |
| Add tests | `src/test/` — JUnit 5 |

## Conventions

- Java code formatted with `google-java-format` (CI-enforced)
- Gradle Kotlin DSL (`build.gradle.kts`)
- Fat JAR packaging (all dependencies bundled via `tasks.jar`)
- ANTLR generates visitor + listener patterns

## Gotchas

- PL engine must be running before stored procedures can execute
- JDBC dependency resolved from local submodule path or CUBRID's Maven repo
- ANTLR grammar changes require `./gradlew generateGrammarSource`
- Java 8 target — do not use Java 9+ APIs

