# ECS game: a tiny shooter built as components + systems (player/enemy/projectile/
# attachment/health) to demonstrate separating gameplay code.
add_executable(ecsgame
    src/main.cpp
    src/EcsGameApp.cpp
)
target_include_directories(ecsgame PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ecsgame PRIVATE engine)
