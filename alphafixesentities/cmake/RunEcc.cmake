# Runs the alpha's Entity Class Compiler on one .es file.
#   cmake -DECC=<Ecc.exe> -DES=<source .es> -DOUT=<output folder> -P RunEcc.cmake
# Ecc writes <name>.cpp, <name>.h and <name>_tables.h next to its input, so the
# .es is copied into OUT first.  The #line directives are then pointed back at
# the real source, so that compiler errors show the .es line.
get_filename_component(name "${ES}" NAME_WE)
file(MAKE_DIRECTORY "${OUT}")
configure_file("${ES}" "${OUT}/${name}.es" COPYONLY)
file(REMOVE "${OUT}/${name}.cpp" "${OUT}/${name}.h" "${OUT}/${name}_tables.h")

execute_process(COMMAND "${ECC}" "${name}.es"
  WORKING_DIRECTORY "${OUT}"
  RESULT_VARIABLE iResult
  OUTPUT_VARIABLE strOutput
  ERROR_VARIABLE strOutput)
if(NOT iResult EQUAL 0 OR NOT EXISTS "${OUT}/${name}.cpp")
  message(FATAL_ERROR "Ecc failed on ${ES}:\n${strOutput}")
endif()

file(TO_CMAKE_PATH "${ES}" strSource)
foreach(strFile "${name}.cpp" "${name}.h" "${name}_tables.h")
  file(READ "${OUT}/${strFile}" strText)
  string(REGEX REPLACE "#line ([0-9]+) \"[^\"]*/${name}\\.es\"" "#line \\1 \"${strSource}\"" strText "${strText}")
  file(WRITE "${OUT}/${strFile}" "${strText}")
endforeach()
