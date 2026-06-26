# GLAD is distributed as generated C source. We compile it into a small static
# library and expose its headers so any target that links `glad` can call GL.
add_library(glad STATIC src/glad.c)

target_include_directories(glad
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# On Unix-like systems GLAD resolves symbols through the dynamic loader.
if(UNIX)
    target_link_libraries(glad PUBLIC ${CMAKE_DL_LIBS})
endif()
