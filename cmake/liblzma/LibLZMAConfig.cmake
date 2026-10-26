# The RWE build uses the pinned XZ subproject, never a host liblzma.
set(LibLZMA_FOUND TRUE)
set(LIBLZMA_FOUND TRUE)
set(LIBLZMA_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/libs/xz/src/liblzma/api")
set(LIBLZMA_INCLUDE_DIRS "${LIBLZMA_INCLUDE_DIR}")
set(LIBLZMA_LIBRARY liblzma)
set(LIBLZMA_LIBRARIES liblzma)
