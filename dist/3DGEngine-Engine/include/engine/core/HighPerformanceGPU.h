#pragma once
// Ask laptops with switchable graphics to run on the DEDICATED GPU.
//
// On hybrid systems (Intel iGPU + NVIDIA/AMD dGPU), the driver runs OpenGL apps
// on the power-saving integrated GPU unless the executable exports these two
// symbols. The NVIDIA and AMD drivers check for them at process start.
//
// Usage: include this header EXACTLY ONCE, in your executable's main .cpp (it
// DEFINES global variables, so including it in more than one translation unit
// per executable would be a multiple-definition error). No-op off Windows.
#if defined(_WIN32)
extern "C" {
    // 0x00000001 enables NVIDIA Optimus high-performance mode.
    __declspec(dllexport) unsigned long NvOptimusEnablement = 1;
    // Non-zero requests the AMD PowerXpress high-performance GPU.
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif
