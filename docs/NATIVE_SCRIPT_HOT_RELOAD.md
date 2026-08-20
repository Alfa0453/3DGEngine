# Native C++ Script Build, Hot Reload, and Recovery

## Runtime flow

Native project sources live under `Content/Scripts`. The editor snapshots native source
timestamps and sizes every 0.25 seconds and debounces changes for 0.65 seconds. A build is
performed asynchronously; CMake still produces `Binaries/game_scripts.dll`, after which the
worker copies it to a unique
`Intermediate/Scripts/game_scripts_candidate_<generation>.dll`. The worker never loads a DLL
or mutates the scene, registries, or live instances.

The editor thread rejects completed work if its project or generation is stale. It writes
`Intermediate/Scripts/module_load.pending` before loading a candidate. The candidate registers
into temporary `ScriptRegistry` and `BtScriptRegistry` objects and must pass the scripting API
version handshake, duplicate-registration checks, and factory-construction validation.

On validation failure, the active module and live registries remain untouched. On success, the
editor pauses script execution, captures existing hot-reload and AI blackboard state, destroys
DLL-backed instances, extracts old project factories, installs candidate factories, rebuilds AI
and native script instances, swaps modules, destroys old factories, and unloads the old DLL.
Only then is the marker removed. Serialized script fields remain on their ECS components and the
existing `OnBeforeHotReload` / `OnAfterHotReload` state hooks remain authoritative.

Only one build runs at a time. Further saves queue one follow-up build. Creating a native source
through Create + Attach resets the watcher baseline, so initial file creation cannot race an
implicit build. The first actual edit/save follows the normal automatic pipeline.

## Crash recovery and safe mode

If the editor terminates during candidate loading, registration, installation, or instance
recreation, `module_load.pending` remains. On the next project open, the editor does not load the
suspect DLL. It moves the candidate and its build product to
`Binaries/Quarantine/game_scripts_failed_<generation>_<timestamp>.dll`, clears the handled
marker, opens the project with native project scripts disabled, and reports Script Safe Mode in
the build status and editor log.

Source editing, compiler diagnostics, Save Source, manual Hot Reload, and automatic compilation
remain available in Safe Mode. A successful candidate install returns the state to Ready. No
source file is ever quarantined or deleted.

## Module ABI

Every generated project module exports `Get3DGScriptApiVersion` in addition to
`RegisterScriptModule`. `ScriptModule::Load` rejects a missing or incompatible version before
registration. Factory registration occurs only in temporary registries.

## Project boundaries and shutdown

Project switching and editor shutdown stop scheduling work and drain the active build future.
Any uninstalled candidate is removed. Play-mode native and behavior instances are destroyed
before registries are cleared and before the project module is unloaded. A normal clean shutdown
removes a stale marker only when no module installation is in progress.

## Failure behavior

- Compile failure: build log and diagnostics remain available; active module is unchanged.
- Candidate load/validation failure: candidate is quarantined; active module is unchanged.
- Install/recreation failure: new instances are destroyed, old registries are restored, and the
  candidate is unloaded. If native code terminates the process, the marker provides recovery.
- Interrupted startup load: suspect binaries are quarantined and the project opens in Safe Mode.
- Successful repair: the repaired candidate validates and installs, then Safe Mode clears.
