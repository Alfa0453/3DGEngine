# Core application and architecture

## Targets and dependency direction

The root CMake project builds these primary targets:

| Target | Role |
|---|---|
| `engine` | Reusable static runtime library |
| `game` | Shared project gameplay module and script registration |
| `3DGEditor` | Dear ImGui authoring application |
| `3DGScriptCompiler` | Helper used by editor-managed script compilation |
| `player` | Standalone packaged-game runtime |
| `tests/*` | Regression executables grouped by subsystem |

Runtime code must not include editor headers. Editor documents are converted
into engine runtime descriptions or exported scene records before the player
loads them.

Third-party foundations are GLFW, GLAD, GLM, OpenGL, Assimp, Dear ImGui, and
miniaudio. PNG, JPEG, and TrueType handling also have engine-side
implementations.

## `Application`

`engine::Application` owns a `Window` and defines the main-loop contract:

- `OnInit()` runs after the OpenGL context exists.
- `OnUpdate(dt)` runs once per rendered frame.
- `OnFixedUpdate(fixedDt)` runs zero or more times from an accumulator.
- `OnRender()` draws one frame.
- `OnShutdown()` releases application-owned resources.

The default fixed step is `1/120` second. `InterpolationAlpha()` exposes the
remaining accumulator fraction for presentation interpolation. Change the
simulation rate with `SetFixedTimeStep()` before calling `Run()`.

Do not perform OpenGL resource creation in constructors that run before
`OnInit()`. Editor panels follow the same rule by creating preview resources
lazily during `Draw()`.

## `Window`

`engine::Window` is the RAII wrapper for GLFW and the OpenGL context. It
provides:

- framebuffer size and aspect ratio;
- key and mouse-button state;
- absolute mouse position, per-frame mouse delta, and wheel delta;
- cursor capture for gameplay mouse look;
- fullscreen and VSync control;
- minimized-state detection;
- access to the native `GLFWwindow`.

`Window::Update()` swaps buffers and pumps OS events. Mouse deltas are reset
per frame, so consumers should read them during the application update.

## Configuration and paths

`engine::Config` reads and writes simple `key = value` files. Typed getters
always accept a fallback, so an absent setting is not fatal. The standalone
player uses this for window and startup configuration.

`engine::ExecutableDir()` returns the executable directory and is the preferred
base for packaged content. It prevents runtime behavior from depending on the
process working directory.

`HighPerformanceGPU.h` exports the NVIDIA Optimus and AMD PowerXpress hints on
Windows. Include it exactly once in an executable entry point.

## Timing ownership

Use the two clocks consistently:

| Work | Recommended clock |
|---|---|
| Keyboard/mouse sampling | Variable update |
| Camera input and camera effects | Variable update |
| Animation visual blending | Variable update |
| Audio source/listener updates | Variable update |
| Physics and character collision | Fixed update |
| AI steering and path following | Fixed update |
| Projectile sweeps and damage | Fixed update |
| Script `OnUpdate` | Variable update |
| Script `OnFixedUpdate` | Fixed update |

Avoid applying the same movement from both clocks. Doing so makes speed
frame-rate dependent and can cause collision tunnelling or jitter.

## Ownership rules

GPU wrappers such as `Mesh`, `Texture`, `Shader`, `Framebuffer`, and `Cubemap`
own OpenGL objects and are move-only. Runtime asset caches own shared models
and textures; ECS components store non-owning pointers to those cached
resources. Clear or replace a registry before destroying the cache that its
components reference.

The hot-reloadable `ScriptModule` has a stricter lifetime rule: destroy all
live script instances and clear the script factory registry before unloading
the DLL, because their code and virtual tables live inside that module.

