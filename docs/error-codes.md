# Adding a New Error Code

The procedure for adding an engine error code and its message.
Read this before you add an `ER_*` code.

`ER_LAST_ERROR` is a sentinel one below the newest code, and `er_set ()` rejects any code at or below it (`src/base/error_manager.c`).

1. In `src/base/error_code.h`, define the new code with the current `ER_LAST_ERROR` value, then set `ER_LAST_ERROR` one lower. Add codes only there: `src/compat/dbi_compat.h` includes `error_code.h` and defines no `ER_*` macros.
2. In both `msg/en_US.utf8/cubrid.msg` and `msg/ko_KR.utf8/cubrid.msg`, under `$set 5 MSGCAT_SET_ERROR`, the new message takes positive ID `-ER_CODE`, which is the ID of the current `Last Error` line (ko_KR: `마지막 에러`); move that line to ID `-ER_LAST_ERROR`.
3. Check the text at runtime: the build compiles the catalogs with `gencat`, and the engine reads them from `$CUBRID/msg`, so install an assert-enabled build into a scratch `$CUBRID` (see [build.md](build.md#scratch-install)), raise the error once, and confirm that the message is yours, not `No error message available.`.
4. If a client-facing error must also be exposed in CCI, open a separate PR in `CUBRID/cubrid-cci` for `src/cci/base_error_code.h`; do not modify files inside `cubrid-cci/` in an engine PR. The submodule bump PR normally comes from `.github/workflows/submodule-bump-receiver.yml`; check for an open bump PR before you bump the submodule by hand.

How engine interfaces report errors (`er_set`, negative codes, no escaping C++ exceptions) is in [src/AGENTS.md](../src/AGENTS.md#resource-memory-and-error-handling).
