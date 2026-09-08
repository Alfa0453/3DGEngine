# Automated Test Panel

Open **Panels > Debug & Diagnostics > Automated Test Panel** to discover, build, and
run the engine test suite without leaving the editor.

## Test build isolation

The panel uses `build-tests` beside the engine source tree. This is deliberately
separate from the active editor build: selecting **Configure/build before run** enables
the test targets in that isolated directory, builds them, and then runs CTest. Debug
and Release configurations can therefore be tested without reconfiguring the editor
that is currently running.

If `tests/CMakeLists.txt` is missing, the panel reports it immediately. Restore or
create the engine test suite before requesting configuration. Turning off automatic
configuration still permits discovery and execution from an existing test build.

## Running tests

- **Discover** lists tests without executing them.
- **Run all** builds when requested and executes the complete suite.
- Select a result and use **Run selected** to reproduce one failure.
- Choose a category or enter a name filter, then use **Run filtered** to execute only
  the matching tests.
- **Per-test timeout** prevents a hung test from blocking a run indefinitely.
- **Stop** terminates the complete configure, build, or test process tree safely.

The panel categorizes tests by their registered CTest names into engine core, assets,
rendering, physics and AI, animation, gameplay, editor and world, scripting, and
performance groups. Categories affect presentation and filtered runs; they do not
change the test executables themselves.

## Results and reports

The results table reports each test's status and duration. Failed-test diagnostics and
all configure/build output remain visible in the scrolling output view. The live log is
stored at `build-tests/automated_test_panel.log`.

Use **Save report** to write the latest status, exit code, total duration, and full log
to `TestReports/latest_test_report.txt`. This stable location is suitable for attaching
to bug reports or collecting in a continuous-integration artifact step.
