set(CPACK_PACKAGE_VENDOR "CuraEngine")
set(CPACK_PACKAGE_CONTACT "edwardFang <edwardfang2018@outlook.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "CuraEngine")
set(CPACK_PACKAGE_VERSION "1.0")
set(CPACK_GENERATOR "DEB")
if(NOT DEFINED CPACK_DEBIAN_PACKAGE_ARCHITECTURE)
  execute_process(COMMAND dpkg --print-architecture OUTPUT_VARIABLE CPACK_DEBIAN_PACKAGE_ARCHITECTURE OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${CPACK_PACKAGE_VERSION}_${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}")

set(DEB_DEPENDS
    "libstdc++6 (>= 4.9.0)"
    "libgcc1 (>= 4.9.0)"
)
string(REPLACE ";" ", " DEB_DEPENDS "${DEB_DEPENDS}")
set(CPACK_DEBIAN_PACKAGE_DEPENDS ${DEB_DEPENDS})

include(CPack)
