# AI demo: a patrolling enemy that spots the player by line of sight, then
# pathfinds (A*) and pursues (steering) -- state chosen by an FSM. No assets.
add_executable(aidemo
    src/main.cpp
    src/AiDemoApp.cpp
)
target_include_directories(aidemo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(aidemo PRIVATE engine)
