# Engine Plugins

Place engine-wide plugins in this folder. Each plugin lives in its own directory and
contains one `.3dgplugin` descriptor. Project-only plugins belong in
`<Project>/Plugins` instead.

See `docs/PLUGIN_MANAGER_GUIDE.md` for the descriptor contract, dependency rules, and
CMake integration example. `GameplayUtilities` is a disabled-by-default example that
demonstrates a public native library and scripts registered in the editor and player.
`PhotoMode` is a second example that demonstrates runtime host services: a free camera,
lens and color controls, depth of field, gameplay freeze, and screenshot capture.
