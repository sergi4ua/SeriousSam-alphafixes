cmake_minimum_required(VERSION 3.20...4.0)
project(AlphaFixesEntities LANGUAGES CXX)

# The alpha's Engine.dll and Entities.dll are 32-bit: configure with -A Win32.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "AlphaFixesEntities must be built as 32-bit: cmake -A Win32 ...")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# static runtime: no Visual C++ redistributable needed
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

# The game folder: Bin\Ecc.exe compiles the classes, model headers are included
# from Models\, and "install" copies the DLL and its data there.
set(AFE_GAME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "Serious Sam alpha folder")
get_filename_component(AFE_GAME_DIR "${AFE_GAME_DIR}" ABSOLUTE)
set(AFE_ECC "${AFE_GAME_DIR}/Bin/Ecc.exe")
if(NOT EXISTS "${AFE_ECC}")
  message(FATAL_ERROR "The alpha's Entity Class Compiler wasn't found: ${AFE_ECC}")
endif()

# Import libraries for the alpha's DLLs, made from their export lists.
set(AFE_LIBDIR "${CMAKE_CURRENT_BINARY_DIR}/lib")
file(MAKE_DIRECTORY "${AFE_LIBDIR}")
foreach(dll Engine Entities)
  add_custom_command(
    OUTPUT "${AFE_LIBDIR}/${dll}.lib"
    COMMAND "${CMAKE_AR}" /nologo /machine:x86 "/def:${CMAKE_CURRENT_SOURCE_DIR}/SDK/lib/${dll}.def" "/out:${AFE_LIBDIR}/${dll}.lib"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/SDK/lib/${dll}.def"
    COMMENT "Import library for the alpha's ${dll}.dll"
    VERBATIM)
  list(APPEND AFE_IMPORT_LIBS "${AFE_LIBDIR}/${dll}.lib")
endforeach()
add_custom_target(AlphaImportLibs DEPENDS ${AFE_IMPORT_LIBS})

# Entity classes: one .es each in Classes\, compiled by the alpha's Ecc.exe.
set(AFE_CLASSES RobotDriving Copier Damager Trigger2)
set(AFE_GENDIR "${CMAKE_CURRENT_BINARY_DIR}/gen")
foreach(cls ${AFE_CLASSES})
  set(es "${CMAKE_CURRENT_SOURCE_DIR}/Classes/${cls}.es")
  add_custom_command(
    OUTPUT "${AFE_GENDIR}/${cls}.cpp" "${AFE_GENDIR}/${cls}.h" "${AFE_GENDIR}/${cls}_tables.h"
    COMMAND "${CMAKE_COMMAND}" "-DECC=${AFE_ECC}" "-DES=${es}" "-DOUT=${AFE_GENDIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunEcc.cmake"
    DEPENDS "${es}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunEcc.cmake"
    COMMENT "Ecc ${cls}.es"
    VERBATIM)
  list(APPEND AFE_GENERATED "${AFE_GENDIR}/${cls}.cpp")
  list(APPEND AFE_SOURCES_ES "${es}")
endforeach()

add_library(AlphaFixesEntities SHARED
  src/DllMain.cpp src/EnemyBase.cpp src/Common.cpp src/AlphaFixesEntities.h
  SDK/StdH.h SDK/Engine/Engine.h SDK/Entities/Common.h SDK/Entities/EnemyBase.h SDK/Entities/Projectile.h
  ${AFE_SOURCES_ES} ${AFE_GENERATED})
add_dependencies(AlphaFixesEntities AlphaImportLibs)
target_include_directories(AlphaFixesEntities PRIVATE SDK "${AFE_GENDIR}" "${AFE_GAME_DIR}")
target_link_libraries(AlphaFixesEntities PRIVATE ${AFE_IMPORT_LIBS})
target_compile_options(AlphaFixesEntities PRIVATE /W3 /EHsc)
# test builds log the events of this DLL's enemies to Bin\AlphaFixesEntities_trace.log
option(AFE_TRACE "Log enemy events (testing only)" OFF)
if(AFE_TRACE)
  target_compile_definitions(AlphaFixesEntities PRIVATE AFE_TRACE)
endif()
# The alpha's ECC writes "STATE_x, FALSE;" for every autocall: harmless.
set_source_files_properties(${AFE_GENERATED} PROPERTIES COMPILE_OPTIONS "/wd4548;/wd4552")
# the engine's exception handling (VC 6) walks SEH frames: no SafeSEH tables needed
target_link_options(AlphaFixesEntities PRIVATE /SAFESEH:NO)

install(TARGETS AlphaFixesEntities RUNTIME DESTINATION "${AFE_GAME_DIR}/Bin")
install(DIRECTORY Data/ DESTINATION "${AFE_GAME_DIR}")
