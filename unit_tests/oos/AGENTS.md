# OOS Tests

This subtree is an explicit exception to the Catch2 default in the parent `unit_tests/AGENTS.md`. Preserve GoogleTest
for the established OOS test targets and use its existing fixtures and assertions for new OOS coverage. Catch2 remains
the default outside this subtree; do not introduce a third test framework.
