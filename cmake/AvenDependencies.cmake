# Third-party dependencies, pinned to exact versions.
# Aven owns the engine architecture, renderer, scripting language and editor;
# these small permissively-licensed libraries handle solved low-level problems.

include(FetchContent)
set(FETCHCONTENT_QUIET ON)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# --- GLFW: windowing and input (zlib) ---
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
if(UNIX AND NOT APPLE)
    option(AVEN_GLFW_WAYLAND "Build GLFW with native Wayland support" OFF)
    set(GLFW_BUILD_WAYLAND ${AVEN_GLFW_WAYLAND} CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE)

# --- stb: image loading/writing, font rasterization (MIT / public domain) ---
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20)

# --- cgltf: glTF 2.0 model loading (MIT) ---
FetchContent_Declare(cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG v1.15
    GIT_SHALLOW TRUE)

# --- miniaudio: audio playback (MIT-0 / public domain) ---
FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.25
    GIT_SHALLOW TRUE)

# --- Box2D: 2D physics (MIT) ---
set(BOX2D_SAMPLES OFF CACHE BOOL "" FORCE)
set(BOX2D_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BOX2D_DOCS OFF CACHE BOOL "" FORCE)
set(BOX2D_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_VALIDATE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(box2d
    GIT_REPOSITORY https://github.com/erincatto/box2d.git
    GIT_TAG v3.1.1
    GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(glfw stb cgltf miniaudio box2d)

# glad: generated OpenGL 3.3 core loader, vendored in third_party/glad.
add_library(aven_glad STATIC ${PROJECT_SOURCE_DIR}/third_party/glad/src/gl.c)
target_include_directories(aven_glad PUBLIC ${PROJECT_SOURCE_DIR}/third_party/glad/include)

add_library(aven_stb INTERFACE)
target_include_directories(aven_stb SYSTEM INTERFACE ${stb_SOURCE_DIR})

add_library(aven_cgltf INTERFACE)
target_include_directories(aven_cgltf SYSTEM INTERFACE ${cgltf_SOURCE_DIR})

add_library(aven_miniaudio INTERFACE)
target_include_directories(aven_miniaudio SYSTEM INTERFACE ${miniaudio_SOURCE_DIR})

# --- Dear ImGui (docking branch) + ImGuizmo: editor UI only ---
if(AVEN_BUILD_EDITOR)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.91.9b-docking
        GIT_SHALLOW TRUE)
    FetchContent_Declare(imguizmo
        GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
        GIT_TAG 8ddc3516e3b90a0d1ae94370e597acca5a4e9f64)
    FetchContent_MakeAvailable(imgui imguizmo)

    add_library(aven_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        ${imguizmo_SOURCE_DIR}/ImGuizmo.cpp)
    target_include_directories(aven_imgui SYSTEM PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${imgui_SOURCE_DIR}/misc/cpp
        ${imguizmo_SOURCE_DIR})
    target_link_libraries(aven_imgui PUBLIC glfw)
    set(AVEN_IMGUI_FONTS_DIR ${imgui_SOURCE_DIR}/misc/fonts CACHE INTERNAL "")
endif()
