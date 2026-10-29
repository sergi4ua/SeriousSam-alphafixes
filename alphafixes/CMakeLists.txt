cmake_minimum_required(VERSION 3.20...4.0)
project(AlphaFixes LANGUAGES C CXX)

# The game is a 32-bit program: configure with -A Win32.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "AlphaFixes must be built as 32-bit: cmake -A Win32 ...")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# static runtime: no Visual C++ redistributable needed on the player's PC
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

# Where "install" copies the DLLs: the game's Bin folder next to this project.
set(AF_GAME_BIN "${CMAKE_CURRENT_SOURCE_DIR}/../Bin" CACHE PATH "Serious Sam alpha Bin folder")

# --- winmm.dll: proxy that loads AlphaFixes.dll -----------------------------
add_library(winmm_proxy SHARED proxy/winmm_proxy.c proxy/winmm.def)
set_target_properties(winmm_proxy PROPERTIES OUTPUT_NAME winmm)
target_compile_options(winmm_proxy PRIVATE /W4 /GS-)
# no incremental-link thunks: the stubs must be the exports themselves
target_link_options(winmm_proxy PRIVATE /INCREMENTAL:NO /SAFESEH:NO)

# --- third party ------------------------------------------------------------------
set(IMGUI_DIR third_party/imgui)
add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_win32.cpp ${IMGUI_DIR}/backends/imgui_impl_opengl2.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR})
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

set(MINHOOK_DIR third_party/minhook)
add_library(minhook STATIC
  ${MINHOOK_DIR}/src/buffer.c ${MINHOOK_DIR}/src/hook.c ${MINHOOK_DIR}/src/trampoline.c ${MINHOOK_DIR}/src/hde/hde32.c)
target_include_directories(minhook PUBLIC ${MINHOOK_DIR}/include)

# --- AlphaFixes.dll ------------------------------------------------------------
add_library(AlphaFixes SHARED
  src/main.cpp src/config.cpp src/patch.cpp src/display.cpp src/widescreen.cpp
  src/mouse.cpp src/overlay.cpp src/streamfix.cpp src/lightfix.cpp src/cannon.cpp src/weapons.cpp src/texts.cpp src/settingsfile.cpp src/graphics.cpp src/ingamemenu.cpp src/testaids.cpp src/alphafixes.h)
target_link_libraries(AlphaFixes PRIVATE imgui minhook opengl32 user32 gdi32)
target_compile_options(AlphaFixes PRIVATE /W4 /wd4100)
target_link_options(AlphaFixes PRIVATE /SAFESEH:NO)

# Testing aids (trigger files in Bin, AF_LOG_* environment variables): for test builds only.
option(AF_TEST_AIDS "Build the testing aids into AlphaFixes.dll" OFF)
target_compile_definitions(AlphaFixes PRIVATE AF_TEST_AIDS=$<BOOL:${AF_TEST_AIDS}>)

install(TARGETS winmm_proxy AlphaFixes RUNTIME DESTINATION "${AF_GAME_BIN}" LIBRARY DESTINATION "${AF_GAME_BIN}")
