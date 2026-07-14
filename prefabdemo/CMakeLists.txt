# Prefab demo: define entity templates once in a PrefabLibrary, spawn instances with
# one call, and Registry::Clone the last one.
add_executable(prefabdemo
    src/main.cpp
    src/PrefabDemoApp.cpp
)
target_include_directories(prefabdemo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(prefabdemo PRIVATE engine)
