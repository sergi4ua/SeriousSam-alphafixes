cmake_minimum_required(VERSION 3.20...4.0)
project(WldConvert LANGUAGES CXX)

# The converter loads the alpha's 32-bit Engine.dll and Entities.dll: configure with -A Win32.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "WldConvert must be built as 32-bit: cmake -A Win32 ...")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# static runtime: no Visual C++ redistributable needed
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

# Where "install" copies WldConvert.exe: the game's Bin folder next to this project.
set(WC_GAME_BIN "${CMAKE_CURRENT_SOURCE_DIR}/../Bin" CACHE PATH "Serious Sam alpha Bin folder")

add_executable(WldConvert
  src/main.cpp src/tfe_read.cpp src/convert.cpp src/alpha_write.cpp src/classes.cpp src/engine.cpp
  src/stream.h src/world.h src/classes.h src/convert.h src/report.h src/tfe_classes.inc)
target_compile_options(WldConvert PRIVATE /W4 /EHsc)
# the engine's exception handling (VC 6) walks SEH frames: no SafeSEH tables needed
target_link_options(WldConvert PRIVATE /SAFESEH:NO)

install(TARGETS WldConvert RUNTIME DESTINATION "${WC_GAME_BIN}")
