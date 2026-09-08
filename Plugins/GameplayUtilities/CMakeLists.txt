add_library(three_dg_gameplay_utilities STATIC
    src/GameplayUtilities.cpp
)

target_include_directories(three_dg_gameplay_utilities
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(three_dg_gameplay_utilities PUBLIC engine)
target_compile_features(three_dg_gameplay_utilities PUBLIC cxx_std_17)
set_target_properties(three_dg_gameplay_utilities PROPERTIES
    FOLDER "Plugins/Gameplay Utilities"
    POSITION_INDEPENDENT_CODE ON)

# The bootstrap is compiled into each host executable so its startup callback
# cannot be discarded by the static-library linker. RegisterGameModule invokes
# that callback again whenever script registries are rebuilt.
foreach(_3dg_host IN ITEMS 3DGEditor player)
    if(TARGET ${_3dg_host})
        target_sources(${_3dg_host} PRIVATE src/GameplayUtilitiesBootstrap.cpp)
        target_link_libraries(${_3dg_host} PRIVATE three_dg_gameplay_utilities)
    endif()
endforeach()

# Make the public helpers available to manual game code and project native
# scripts as well as to the example scripts registered by the bootstrap.
if(TARGET game)
    target_link_libraries(game PUBLIC three_dg_gameplay_utilities)
endif()
if(TARGET game_scripts)
    target_link_libraries(game_scripts PRIVATE three_dg_gameplay_utilities)
endif()
