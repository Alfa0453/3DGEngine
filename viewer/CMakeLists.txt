# The model viewer: a third executable on the engine, used to inspect any model
# the OBJ loader can read.
add_executable(viewer
    src/main.cpp
    src/ViewerApp.cpp
)

target_include_directories(viewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(viewer PRIVATE engine)

target_compile_definitions(viewer
    PRIVATE ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

add_custom_command(TARGET viewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:viewer>/assets"
    COMMENT "Copying viewer assets next to the executable")
