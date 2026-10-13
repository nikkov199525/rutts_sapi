cmake_minimum_required(VERSION 3.24)
project(RuTTSSAPI VERSION 1.0.0 LANGUAGES C CXX)
if(NOT WIN32 OR NOT MINGW)
  message(FATAL_ERROR "Use LLVM-MinGW on Windows; see README.md")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_link_options(-static)

# Layout: external/ holds the submodules (the ru_tts core, rulex and its
# storage, the resampler), src/ is this bridge and its configurator.
set(EXT "${CMAKE_SOURCE_DIR}/external")
set(CORE "${EXT}/ru_tts")
set(LMDB "${EXT}/lmdb/libraries/liblmdb")
set(RULEX "${EXT}/rulex")
foreach(SUBMODULE "${CORE}/src" "${LMDB}" "${RULEX}/src" "${EXT}/pcre2/src" "${EXT}/libsamplerate")
 if(NOT EXISTS "${SUBMODULE}")
  message(FATAL_ERROR "${SUBMODULE} is missing. Run: git submodule update --init --recursive")
 endif()
endforeach()

# --- ru_tts core: the synthesizer itself ------------------------------------
add_library(ru_tts SHARED
 ${CORE}/src/synth.c ${CORE}/src/sink.c ${CORE}/src/transcription.c ${CORE}/src/text2speech.c
 ${CORE}/src/utterance.c ${CORE}/src/time_planner.c ${CORE}/src/speechrate_control.c
 ${CORE}/src/intonator.c ${CORE}/src/soundproducer.c ${CORE}/src/numerics.c
 ${CORE}/src/male.c ${CORE}/src/female.c)
target_include_directories(ru_tts PUBLIC "${CORE}/src")
target_compile_options(ru_tts PRIVATE -w)
set_target_properties(ru_tts PROPERTIES PREFIX "")

# --- Resampling: core rate -> the rate handed to SAPI -----------------------
add_library(resampler STATIC ${EXT}/libsamplerate/samplerate.c ${EXT}/libsamplerate/src_linear.c ${EXT}/libsamplerate/src_zoh.c)
target_include_directories(resampler PUBLIC "${EXT}/libsamplerate")
target_compile_definitions(resampler PRIVATE HAVE_LRINT=1 HAVE_LRINTF=1)

# --- RuLex: the stress dictionary and its LMDB storage ----------------------
set(PCRE2_BUILD_PCRE2_8 ON CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_16 OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_32 OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2GREP OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory("${EXT}/pcre2" EXCLUDE_FROM_ALL)
include(cmake/LmdbPatch.cmake)
# access_utf8.c makes rulex open a database along a non-ASCII path.
add_library(rulex_objects STATIC
 ${RULEX}/src/lexdb.c ${RULEX}/src/coder.c
 ${LMDB_PATCHED} ${LMDB}/midl.c ${LMDB}/module.c
 src/rulex/access_utf8.c)
target_include_directories(rulex_objects PUBLIC "${LMDB}" "${RULEX}/src")
target_compile_definitions(rulex_objects PUBLIC PCRE2_STATIC)
target_compile_options(rulex_objects PRIVATE -w)
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
 # 32-bit rulex reads the same database as the 64-bit one through VL32.
 target_compile_definitions(rulex_objects PUBLIC MDB_VL32)
endif()
target_link_libraries(rulex_objects PUBLIC pcre2-posix pcre2-8 pthread)
add_library(rulex SHARED src/rulex/rulex.def)
set_target_properties(rulex PROPERTIES LINKER_LANGUAGE C PREFIX "")
target_link_libraries(rulex PRIVATE -Wl,--whole-archive rulex_objects -Wl,--no-whole-archive)

# rulex.db is built from the dictionary sources of the rulex submodule.
add_executable(lexholder ${RULEX}/src/lexholder.c)
target_link_libraries(lexholder PRIVATE rulex_objects)
target_compile_options(lexholder PRIVATE -w)
set(DATA "${RULEX}/data")
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/bin/rulex.db"
 COMMAND ${CMAKE_COMMAND} -E rm -f "${CMAKE_BINARY_DIR}/bin/rulex.db"
 COMMAND $<TARGET_FILE:lexholder> -f "${DATA}/explicit.dict" -X "${CMAKE_BINARY_DIR}/bin/rulex.db"
 COMMAND $<TARGET_FILE:lexholder> -f "${DATA}/implicit.dict" -M "${CMAKE_BINARY_DIR}/bin/rulex.db"
 COMMAND $<TARGET_FILE:lexholder> -f "${DATA}/lexicon.rules" -L "${CMAKE_BINARY_DIR}/bin/rulex.db"
 COMMAND $<TARGET_FILE:lexholder> -f "${DATA}/general.rules" -G "${CMAKE_BINARY_DIR}/bin/rulex.db"
 COMMAND $<TARGET_FILE:lexholder> -f "${DATA}/prefix.rules" -P "${CMAKE_BINARY_DIR}/bin/rulex.db"
 COMMAND $<TARGET_FILE:lexholder> -f "${DATA}/correction.rules" -C "${CMAKE_BINARY_DIR}/bin/rulex.db"
 DEPENDS lexholder ${DATA}/explicit.dict ${DATA}/implicit.dict ${DATA}/lexicon.rules ${DATA}/general.rules ${DATA}/prefix.rules ${DATA}/correction.rules VERBATIM)
add_custom_target(rulex_database ALL DEPENDS "${CMAKE_BINARY_DIR}/bin/rulex.db")

# The engine talks to rulex through this wrapper, never to rulex.dll directly.
add_library(rutts_rulex SHARED src/rulex/RulexApi.cpp)
target_include_directories(rutts_rulex PUBLIC src/rulex)
target_compile_definitions(rutts_rulex PRIVATE RUTTS_RULEX_EXPORTS UNICODE _UNICODE)
set_target_properties(rutts_rulex PROPERTIES PREFIX "")

# --- Settings, shared by the engine, the configurator and the tests ---------
add_library(rutts_config STATIC src/config/ParamReader.cpp)
target_include_directories(rutts_config PUBLIC src/config "${CORE}/src")
target_compile_definitions(rutts_config PUBLIC UNICODE _UNICODE NOMINMAX)
target_link_libraries(rutts_config PUBLIC shell32)

# --- The SAPI 5 engine ------------------------------------------------------
file(GLOB SAPI_SOURCES CONFIGURE_DEPENDS src/sapi/*.cpp)
add_library(rutts_sapi SHARED ${SAPI_SOURCES} src/sapi/rutts_sapi.def)
target_include_directories(rutts_sapi PRIVATE src/sapi src/rulex)
target_compile_definitions(rutts_sapi PRIVATE _WIN32_WINNT=0x0A00)
target_link_libraries(rutts_sapi PRIVATE rutts_config resampler ole32 oleaut32 shlwapi advapi32 uuid)
# --enable-stdcall-fixup maps the .def's undecorated names onto the x86
# _Name@N stdcall symbols, so COM finds DllGetClassObject on 32-bit too.
target_link_options(rutts_sapi PRIVATE -Wl,--enable-stdcall-fixup)
set_target_properties(rutts_sapi PROPERTIES PREFIX "")

# --- Configurator -----------------------------------------------------------
add_executable(rutts_configurator WIN32 src/configurator/IniConfigApp.cpp)
target_compile_definitions(rutts_configurator PRIVATE _WIN32_WINNT=0x0A00)
target_compile_options(rutts_configurator PRIVATE -municode)
target_link_options(rutts_configurator PRIVATE -municode)
target_link_libraries(rutts_configurator PRIVATE rutts_config comctl32 ole32 advapi32 uuid)

# --- Result -----------------------------------------------------------------
# "cmake --install" lays out out/ru_tts exactly as the installer puts it into
# Program Files: the engines in x86/ and x64/, one shared rulex.db, the
# configurator and the licenses next to them. build_release.cmd installs both
# trees there and packs the installer from that folder.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
 set(RUTTS_ARCH x64)
else()
 set(RUTTS_ARCH x86)
endif()
install(FILES $<TARGET_FILE:ru_tts> $<TARGET_FILE:rulex> $<TARGET_FILE:rutts_rulex> $<TARGET_FILE:rutts_sapi>
 DESTINATION ${RUTTS_ARCH})
if(RUTTS_ARCH STREQUAL "x64")
 # The x86 tree builds the same database; the configurator is 64-bit only.
 install(FILES "${CMAKE_BINARY_DIR}/bin/rulex.db" DESTINATION .)
 install(PROGRAMS $<TARGET_FILE:rutts_configurator> DESTINATION .)
 install(DIRECTORY LICENSES DESTINATION .)
endif()

# --- Tests ------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
 # RuTtsSapiGuids.cpp: the test asks the engine for a format of its own.
 add_executable(rutts_integration tests/integration.cpp src/sapi/RulexClient.cpp src/sapi/WinUtil.cpp src/sapi/RuTtsSapiGuids.cpp)
 target_include_directories(rutts_integration PRIVATE src/sapi src/rulex)
 target_link_libraries(rutts_integration PRIVATE rutts_config ole32 uuid advapi32)
 add_dependencies(rutts_integration rutts_sapi ru_tts rutts_rulex rulex rulex_database)
 add_test(NAME integration COMMAND rutts_integration "${CMAKE_BINARY_DIR}/bin")
endif()
