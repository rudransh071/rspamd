SET(ZSTDSRC	divsufsort.c
			entropy_common.c
			fse_compress.c
			fse_decompress.c
			huf_compress.c
			huf_decompress.c
			zbuff_compress.c
			zbuff_decompress.c
			zdict.c
			zstd_common.c
			zstd_compress.c
			zstd_decompress.c)

ADD_LIBRARY(rspamd-zstd STATIC ${ZSTDSRC})

IF(ENABLE_FULL_DEBUG MATCHES "OFF")
if ("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
	SET_TARGET_PROPERTIES(rspamd-zstd PROPERTIES COMPILE_FLAGS "-O3")
endif ()
ENDIF()
