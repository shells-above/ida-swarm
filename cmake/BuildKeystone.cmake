# Custom build for Keystone assembler library
# This avoids CMake version compatibility issues with the upstream keystone

# Use ExternalProject to fetch and build Keystone with our custom flags
include(ExternalProject)

set(KEYSTONE_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/keystone-build)
set(KEYSTONE_INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/keystone-install)

# Configure ExternalProject to build Keystone
ExternalProject_Add(keystone_external
    GIT_REPOSITORY https://github.com/keystone-engine/keystone.git
    GIT_TAG 0.9.2
    PREFIX ${KEYSTONE_PREFIX}
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${KEYSTONE_INSTALL_DIR}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DBUILD_SHARED_LIBS=OFF
        -DLLVM_TARGETS_TO_BUILD=X86$<SEMICOLON>ARM$<SEMICOLON>AArch64$<SEMICOLON>Mips$<SEMICOLON>PowerPC$<SEMICOLON>Sparc$<SEMICOLON>SystemZ
        -DBUILD_LIBS_ONLY=ON
        -DCMAKE_CXX_STANDARD=11
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}
    BUILD_BYPRODUCTS ${KEYSTONE_INSTALL_DIR}/lib/libkeystone.a
)

# Create imported library target
add_library(keystone STATIC IMPORTED)
set_target_properties(keystone PROPERTIES
    IMPORTED_LOCATION ${KEYSTONE_INSTALL_DIR}/lib/libkeystone.a
    INTERFACE_INCLUDE_DIRECTORIES ${KEYSTONE_INSTALL_DIR}/include
)

# Add dependency to ensure keystone is built before use
add_dependencies(keystone keystone_external)

# Disable Qt processing for external project
set_property(TARGET keystone_external PROPERTY AUTOMOC OFF)
set_property(TARGET keystone_external PROPERTY AUTOUIC OFF)
set_property(TARGET keystone_external PROPERTY AUTORCC OFF)

message(STATUS "Configured Keystone assembler library")