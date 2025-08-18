# Patch for Keystone STLExtras.h to fix C++ compatibility issues

if(EXISTS "${PATCH_DIR}/llvm/include/llvm/ADT/STLExtras.h")
    file(READ "${PATCH_DIR}/llvm/include/llvm/ADT/STLExtras.h" FILE_CONTENTS)
    
    # Check if already patched
    if(NOT FILE_CONTENTS MATCHES "#include <cstdint>")
        # Add cstdint include after cstddef
        string(REPLACE "#include <cstddef>"
                       "#include <cstddef>\n#include <cstdint> // Added for intptr_t"
                       FILE_CONTENTS "${FILE_CONTENTS}")
        
        file(WRITE "${PATCH_DIR}/llvm/include/llvm/ADT/STLExtras.h" "${FILE_CONTENTS}")
        message(STATUS "Patched STLExtras.h to include cstdint")
    else()
        message(STATUS "STLExtras.h already patched")
    endif()
endif()