# Particle demo: magic / fire / smoke / spark emitters rendered as instanced HDR
# billboards with bloom, over a small PBR scene.
add_executable(particledemo
    src/main.cpp
    src/ParticleDemoApp.cpp
)
target_include_directories(particledemo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(particledemo PRIVATE engine)
