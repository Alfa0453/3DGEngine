# UI demo: immediate-mode panels / buttons / sliders / health bar over a 3D scene.
add_executable(uidemo
    src/main.cpp
    src/UiDemoApp.cpp
)
target_include_directories(uidemo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(uidemo PRIVATE engine)
