# Navmesh demo: an agent pathfinds across a walkable polygon mesh (with a hole)
# to a movable target, following the funnel (string-pulled) path.
add_executable(navdemo
    src/main.cpp
    src/NavDemoApp.cpp
)
target_include_directories(navdemo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(navdemo PRIVATE engine)
