# Player demo: a kinematic capsule character with first/third-person cameras,
# ramps + a step to climb, and dynamic capsule "pins" that topple under physics.
add_executable(playerdemo
    src/main.cpp
    src/PlayerDemoApp.cpp
)
target_include_directories(playerdemo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(playerdemo PRIVATE engine)
