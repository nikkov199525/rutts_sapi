# LMDB on Windows always opens a second, writable handle to the data file
# (me_ovfd), even for MDB_RDONLY. rulex.db lives in Program Files, where users
# have no write access, so the read-only open fails with ACCESS_DENIED.
# Compile a copy of mdb.c that skips that handle for read-only environments.
set(LMDB_PATCHED "${CMAKE_BINARY_DIR}/generated/mdb.c")
file(READ "${LMDB}/mdb.c" MDB_SOURCE)
string(REPLACE "\r\n" "\n" MDB_SOURCE "${MDB_SOURCE}")
set(OLD_OPEN "	rc = mdb_fopen(env, &fname, MDB_O_OVERLAPPED, mode, &env->me_ovfd);\n	if (rc)\n		goto leave;")
set(NEW_OPEN "	if (!(flags & MDB_RDONLY)) {\n	rc = mdb_fopen(env, &fname, MDB_O_OVERLAPPED, mode, &env->me_ovfd);\n	if (rc)\n		goto leave;\n	}")
set(OLD_INIT "	e->me_mfd = INVALID_HANDLE_VALUE;")
set(NEW_INIT "	e->me_mfd = INVALID_HANDLE_VALUE;\n#ifdef _WIN32\n	e->me_ovfd = INVALID_HANDLE_VALUE;\n#endif")
foreach(PART OPEN INIT)
 string(FIND "${MDB_SOURCE}" "${OLD_${PART}}" FOUND)
 if(FOUND EQUAL -1)
  message(FATAL_ERROR "LMDB changed: read-only patch (${PART}) no longer applies to ${LMDB}/mdb.c")
 endif()
 string(REPLACE "${OLD_${PART}}" "${NEW_${PART}}" MDB_SOURCE "${MDB_SOURCE}")
endforeach()
file(WRITE "${LMDB_PATCHED}.tmp" "${MDB_SOURCE}")
file(COPY_FILE "${LMDB_PATCHED}.tmp" "${LMDB_PATCHED}" ONLY_IF_DIFFERENT)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${LMDB}/mdb.c")
