# Auto-navmesh demo: build a navmesh from box obstacles at runtime (voxelize +
# region-merge) and pathfind across the generated (colour-coded) polygons.
add_executable(gennavdemo
    src/main.cpp
    src/GenNavDemoApp.cpp
)
target_include_directories(gennavdemo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gennavdemo PRIVATE engine)
