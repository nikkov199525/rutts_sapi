cmake_minimum_required(VERSION 3.24)

# Full release: both architectures are built and tested in build/, the result
# is laid out in out/ru_tts and packed into out/Output. Script mode keeps the
# two architectures in separate caches. Both this script and build_release.cmd
# can be invoked from any working directory.
get_filename_component(SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(PAYLOAD_DIR "${SOURCE_DIR}/out/ru_tts")
set(SETUP_DIR "${SOURCE_DIR}/out/Output")
if(DEFINED ENV{LLVM_MINGW} AND NOT "$ENV{LLVM_MINGW}" STREQUAL "")
  set(ENV{PATH} "$ENV{LLVM_MINGW}/bin;$ENV{PATH}")
endif()
find_program(LLVM_CXX NAMES x86_64-w64-mingw32-clang++)
find_program(LLVM_X86_CXX NAMES i686-w64-mingw32-clang++)
if(NOT LLVM_CXX OR NOT LLVM_X86_CXX)
  message(FATAL_ERROR "LLVM-MinGW not found. Add its bin to PATH or set LLVM_MINGW.")
endif()
find_program(ISCC_EXECUTABLE NAMES ISCC.exe iscc.exe HINTS
  "C:/Program Files (x86)/Inno Setup 6" "C:/Program Files/Inno Setup 6" "$ENV{LOCALAPPDATA}/Programs/Inno Setup 6")
if(NOT ISCC_EXECUTABLE)
  message(FATAL_ERROR "Inno Setup 6 not found. Install it or add ISCC.exe to PATH.")
endif()
get_filename_component(CMAKE_BIN "${CMAKE_COMMAND}" DIRECTORY)
find_program(CTEST_EXECUTABLE NAMES ctest HINTS "${CMAKE_BIN}" REQUIRED)

function(run_step)
  execute_process(COMMAND ${ARGV} WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE result)
  if(NOT result STREQUAL "0")
    list(JOIN ARGV " " command)
    message(FATAL_ERROR "Release step failed (${result}): ${command}")
  endif()
endfunction()

foreach(arch x86 x64)
  set(build_dir "${SOURCE_DIR}/build/${arch}")
  set(first_configure)
  if(NOT EXISTS "${build_dir}/CMakeCache.txt")
    list(APPEND first_configure -G "MinGW Makefiles"
      "-DCMAKE_TOOLCHAIN_FILE=${SOURCE_DIR}/cmake/mingw-${arch}.cmake")
  endif()
  # Reassert Release and tests even after a developer changed the cache.
  run_step("${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${build_dir}" ${first_configure}
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON)
  run_step("${CMAKE_COMMAND}" --build "${build_dir}" --parallel)
  # Both engines are checked before anything reaches out/.
  run_step("${CTEST_EXECUTABLE}" --test-dir "${build_dir}" --output-on-failure -R "^integration$")
endforeach()

# A fresh folder each time: nothing from an earlier build may slip into the setup.
file(REMOVE_RECURSE "${PAYLOAD_DIR}")
foreach(arch x86 x64)
  run_step("${CMAKE_COMMAND}" --install "${SOURCE_DIR}/build/${arch}" --prefix "${PAYLOAD_DIR}")
endforeach()

# The installer is versioned by the day it is built.
string(TIMESTAMP SETUP_VERSION "%Y.%m.%d")
file(TO_NATIVE_PATH "${PAYLOAD_DIR}" NATIVE_PAYLOAD)
file(TO_NATIVE_PATH "${SETUP_DIR}" NATIVE_SETUP)
# No quotes (ISCC would keep the escaped ones) and no underscore in the /D
# names, which ISCC mangles into something else.
run_step("${ISCC_EXECUTABLE}" /Qp
  "/DSetupVersion=${SETUP_VERSION}"
  "/DPayload=${NATIVE_PAYLOAD}"
  "/DOutputDir=${NATIVE_SETUP}"
  "${SOURCE_DIR}/installer/rutts_sapi.iss")
message(STATUS "Files: ${PAYLOAD_DIR}")
message(STATUS "Installer: ${SETUP_DIR}/rutts_sapi_${SETUP_VERSION}_setup.exe")
