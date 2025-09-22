# CMake script to patch Keystone files for CMake 4.0 compatibility and C++ issues

# Read the file
file(READ ${FILE_PATH} FILE_CONTENTS)

# Replace cmake_minimum_required version
string(REGEX REPLACE "cmake_minimum_required\\(VERSION [0-9\\.]+\\)" 
                     "cmake_minimum_required(VERSION 3.5)" 
                     FILE_CONTENTS "${FILE_CONTENTS}")

# Remove or comment out the CMP0051 policy setting
string(REGEX REPLACE "cmake_policy\\(SET CMP0051 OLD\\)" 
                     "# cmake_policy(SET CMP0051 OLD) - Removed for CMake 4.0 compatibility" 
                     FILE_CONTENTS "${FILE_CONTENTS}")

# Write the patched content back
file(WRITE ${FILE_PATH} "${FILE_CONTENTS}")

message(STATUS "Patched ${FILE_PATH}")

# Additional patch for STLExtras.h to fix intptr_t issue
if(${FILE_PATH} MATCHES "STLExtras.h$")
    file(READ ${FILE_PATH} FILE_CONTENTS)
    
    # Add cstdint include if not present
    if(NOT FILE_CONTENTS MATCHES "#include <cstdint>")
        string(REPLACE "#include <utility> // for std::pair"
                       "#include <utility> // for std::pair\n#include <cstdint> // for intptr_t"
                       FILE_CONTENTS "${FILE_CONTENTS}")
    endif()
    
    file(WRITE ${FILE_PATH} "${FILE_CONTENTS}")
    message(STATUS "Added cstdint include to ${FILE_PATH}")
endif()