# Marble Roller: a rolling-ball obstacle course built on the engine's physics +
# PBR renderer. No assets -- geometry is engine primitives, the sky/renderer/font
# are engine primitives with embedded shaders.
add_executable(marble
    src/main.cpp
    src/MarbleApp.cpp
)
target_include_directories(marble PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(marble PRIVATE engine)
