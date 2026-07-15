include(FetchContent)

find_package(MdfLib CONFIG QUIET)
if(TARGET Upstream::mdf)
    set(MIATA_MDF_TARGET Upstream::mdf)
    return()
endif()

option(MIATA_FETCH_MDF_DEPS "Fetch pinned MDF4 writer dependencies" ON)
if(NOT MIATA_FETCH_MDF_DEPS)
    message(FATAL_ERROR
        "MdfLib was not found. Install it or configure with MIATA_FETCH_MDF_DEPS=ON.")
endif()

# Keep these revisions fixed. Updating any of them is an explicit dependency
# change that must be followed by MDF readback and interoperability testing.
set(MIATA_MDFLIB_COMMIT 2eabc2b3ac89b4bf41d65dc0416ae1d34f1c1f16)
set(MIATA_ZLIB_COMMIT 51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf)
set(MIATA_EXPAT_COMMIT f9a3eeb3e09fbea04b1c451ffc422ab2f1e45744)

find_package(ZLIB QUIET)
if(NOT TARGET ZLIB::ZLIB)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG ${MIATA_ZLIB_COMMIT}
        GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(zlib)
    set_target_properties(zlib PROPERTIES EXCLUDE_FROM_ALL TRUE)
    set_target_properties(zlib zlibstatic PROPERTIES AUTOMOC OFF)
    add_library(ZLIB::ZLIB INTERFACE IMPORTED GLOBAL)
    set_property(TARGET ZLIB::ZLIB PROPERTY INTERFACE_LINK_LIBRARIES zlibstatic)
    set(ZLIB_FOUND TRUE)
endif()
set(ZLIB_FOUND TRUE)
set(ZLIB_LIBRARIES ZLIB::ZLIB)
set(ZLIB_INCLUDE_DIRS "")

find_package(EXPAT QUIET)
if(NOT TARGET EXPAT::EXPAT)
    set(EXPAT_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(EXPAT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(EXPAT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(EXPAT_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(EXPAT_BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
    set(EXPAT_BUILD_PKGCONFIG OFF CACHE BOOL "" FORCE)
    set(EXPAT_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(EXPAT_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        expat
        GIT_REPOSITORY https://github.com/libexpat/libexpat.git
        GIT_TAG ${MIATA_EXPAT_COMMIT}
        GIT_SHALLOW FALSE
        SOURCE_SUBDIR expat
    )
    FetchContent_MakeAvailable(expat)
    set_target_properties(expat PROPERTIES AUTOMOC OFF)
    add_library(EXPAT::EXPAT INTERFACE IMPORTED GLOBAL)
    set_property(TARGET EXPAT::EXPAT PROPERTY INTERFACE_LINK_LIBRARIES expat)
    set(EXPAT_FOUND TRUE)
endif()
set(EXPAT_FOUND TRUE)
set(EXPAT_LIBRARIES EXPAT::EXPAT)
set(EXPAT_INCLUDE_DIRS "")

set(MDF_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(MDF_BUILD_SHARED_LIB_NET OFF CACHE BOOL "" FORCE)
set(MDF_BUILD_SHARED_LIB_EXAMPLE OFF CACHE BOOL "" FORCE)
set(MDF_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(MDF_BUILD_TOOL OFF CACHE BOOL "" FORCE)
set(MDF_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(MDF_BUILD_PYTHON OFF CACHE BOOL "" FORCE)
set(MDF_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    mdflib
    GIT_REPOSITORY https://github.com/ihedvall/mdflib.git
    GIT_TAG ${MIATA_MDFLIB_COMMIT}
    GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(mdflib)
set_target_properties(mdf PROPERTIES AUTOMOC OFF)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # mdflib v2.3.0 timestamp headers use fixed-width integers without directly
    # including <cstdint>; GCC 13 no longer receives it reliably transitively.
    target_compile_options(mdf PRIVATE -include cstdint)
endif()
set(MIATA_MDF_TARGET mdf)
