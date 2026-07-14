add_library(MaterialMaker STATIC
    src/MaterialDocument.cpp
    src/MaterialMakerPanel.cpp
    src/MaterialPreview.cpp
    src/TexturePacker.cpp
    src/ModelMaterialImport.cpp
)

target_include_directories(MaterialMaker PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(MaterialMaker PUBLIC cxx_std_17)

if(TARGET imgui)
    target_link_libraries(MaterialMaker PUBLIC imgui)
endif()

# The live PBR preview renders through the engine (PbrRenderer, IBL, Framebuffer,
# primitives) and calls GL directly, so it needs the engine target (which brings
# glad, glm and imgui transitively).
if(TARGET engine)
    target_link_libraries(MaterialMaker PUBLIC engine)
endif()
