add_library(MaterialMaker STATIC
    src/MaterialDocument.cpp
    src/MaterialMakerPanel.cpp
)

target_include_directories(MaterialMaker PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(MaterialMaker PUBLIC cxx_std_17)

if(TARGET imgui)
    target_link_libraries(MaterialMaker PUBLIC imgui)
endif()
