# pl_engine/ — Java PL (Procedural Language) Server

Gradle (Kotlin DSL) project that builds `pl_server.jar`: the Java SP runtime and the PL/CSQL compiler. The `cub_pl` process loads it (the engine starts and monitors `cub_pl`). It talks to `cub_server` over a Unix domain socket on Linux by default, or TCP to `127.0.0.1` on Windows or when `stored_procedure_uds` is off. Engine side: [src/sp/AGENTS.md](../src/sp/AGENTS.md).

## Layout

| Path | Content |
|------|---------|
| `CMakeLists.txt` | CMake targets that drive Gradle |
| `pl_server/build.gradle.kts` | Dependencies, ANTLR, Java toolchain, fat jar |
| `pl_server/src/main/antlr/` | PL/CSQL grammars (`PlcLexer.g4`, `PlcParser.g4`, `StaticSqlWithRecords.g4`) |
| `pl_server/src/main/java/com/cubrid/jsp/` | SP runtime: `Server` (main class), `protocol/`, `data/` (`CUBRIDPacker`, `CUBRIDUnpacker`), `value/`, `jdbc/` (server-side JDBC), `classloader/`, `compiler/`, ... |
| `pl_server/src/main/java/com/cubrid/plcsql/` | PL/CSQL compiler (`compiler/`, `builtin/`, `predefined/sp/`) |
| `pl_server/src/test/java/` | JUnit 5 tests |

## Build and Test

Use the CMake targets in a configured build directory (see [docs/build.md](../docs/build.md) for configuring):

```bash
cmake --build <build-dir> --target pl_build      # part of ALL; runs gradlew build -x test
cmake --build <build-dir> --target pl_unittest   # runs gradlew cleanTest test; depends on pl_build
```

- `pl_build` writes to `<build-dir>/pl_engine` (`-PbuildDir=`). `pl_unittest` passes `-DbuildDir=` instead, which Gradle does not read as the build directory, so its test build lands in the source tree (`pl_engine/pl_server/build/`, gitignored). When the `cubrid-jdbc` submodule is present, `pl_build` copies its jar and passes `-PcubridJdbcPath=<build-dir>/pl_engine/lib`.
- The Gradle toolchain is pinned to Java 8, so Gradle must find a JDK 8: an installed one, or a download through the foojay resolver (`settings.gradle.kts`). `pl_install_jdk` downloads a JDK 8 into `<build-dir>/vm/jdk8` for packaging.
- `install` copies `pl_server.jar`, `logging.properties`, and the bundled `jdk8` into the `vm` directory of the install prefix. Install into a scratch prefix for a runtime check ([docs/build.md](../docs/build.md#scratch-install)).
- A direct `./gradlew build` run differs from the CMake build:
  - It runs the tests.
  - It writes into the source tree (`build/`, gitignored).
  - Without `-PcubridJdbcPath`, it resolves `cubrid-jdbc:latest.integration` from `http://maven.cubrid.org/` over plain HTTP. The result is not the pinned submodule jar, and the network fetch is insecure.
  - Prefer the CMake targets. If you must run Gradle directly, pass `-PcubridJdbcPath=<build-dir>/pl_engine/lib` after a CMake `pl_build`.
- CI does not run `pl_unittest` or Gradle tests. The test tree is small, and the PL/CSQL library tests are disabled (`*.java.disable`). To verify a behavior change, add a JUnit test and also exercise the procedure through SQL (a SQL regression test or a scratch server).

## Formatting (CI `code-style` check)

CI formats each changed `.java` file with google-java-format **1.7** in AOSP style (`-a`, 4-space indent) on Java 8 (`.github/workflows/codestyle.sh`, `.github/workflows/check.yml`). Another version, or the default Google style, reformats whole files and fails the check. To match it locally, download `google-java-format-1.7-all-deps.jar` from the URL in `check.yml` into `.github/workflows/`, then run `.github/workflows/codestyle.sh <file>` from the repository root.

## Rules

- Use only Java 8 APIs: the toolchain is pinned to 8 (`build.gradle.kts`).
- Wire format: `CUBRIDPacker` / `CUBRIDUnpacker` must stay in sync with `dbvalue_java` (`src/method/method_struct_value.cpp`). Change both sides in the same commit. Protocol messages in `com.cubrid.jsp.protocol` pair with the C side in `src/sp/`.
- Grammar edits need no manual step. The Gradle `antlr` plugin regenerates sources (`generateGrammarSource`, with `-listener -visitor`) before `compileJava`.
- The jar bundles all runtime dependencies (`tasks.jar`). A new dependency ships inside `pl_server.jar`.
