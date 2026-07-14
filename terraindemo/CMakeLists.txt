# Terrain demo: a procedural fBm heightmap generated into a lit, shadowed mesh with
# height/slope texturing; a ball snaps to the surface via HeightAt.
add_executable(terraindemo
    src/main.cpp
    src/TerrainDemoApp.cpp
)
target_include_directories(terraindemo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(terraindemo PRIVATE engine)
