# Executed verification entry points

Run configurations are the adjacent `*-run.json` files. Every actual invocation
used a new output directory. Paths identify retained local inputs, not portable
installation promises.

- Native plus HTTP: `TMPDIR=/home/vimkim/temp/p06 python3 unit_tests/pgbuf_inspector/controlled_observation.py --fixture BUILD/bin/pgbuf_inspector_fixture --volmap-run RUN.json`, once per final debug/release input.
- Browser: `VOLMAP_PRODUCER_RUN=RUN.json mise x node@24.19.0 -- corepack pnpm --dir web exec playwright test --config playwright.producer.config.ts`, in the isolated pinned Volmap checkout, per mode.
- Consumer full gate: the pinned checkout's complete verification recipe; raw `consumer-verify.log.gz` records its Cargo, frontend, typecheck and browser commands.
- Producer suites: `ctest --test-dir BUILD --output-on-failure --verbose`, after matching build/install and with a short private `CUBRID_TMP`. Raw build/test logs include the local convenience wrappers used by this workstation.
- Companion CTP: `ctp.sh shell -c CONF`, with the selected case and updates disabled, inside the isolated namespace command and `inside.sh` recorded per mode in the manifest. Final child identity/controlled/attachment logs are adjacent.

Build/install commands, CMake build type/source directory, matching ELF build IDs,
actual loaded library paths, source/consumer commits and hashes are retained in
the raw logs and each native input manifest. The producer source base was f8c068f
plus the exact helper hashes in `manifest.json`; the commit containing this
ledger records that reviewed implementation. Test-only fixture changes do not
change the shipped server bytes.
