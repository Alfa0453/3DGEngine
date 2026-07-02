# The physics playground demo: drop/stack/shoot bodies, a swinging joint chain,
# all drawn with the PBR pipeline. Like the scene demo it ships NO assets -- the
# renderer, sky and font are all engine primitives with embedded shaders.
add_executable(physics
    src/main.cpp
    src/PhysicsDemoApp.cpp
)
target_include_directories(physics PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(physics PRIVATE engine)
