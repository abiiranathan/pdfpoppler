
cmake_minimum_required(VERSION 3.5.0)
project(pdfpoppler VERSION 0.1.0 LANGUAGES C DESCRIPTION "A simple wrapper around poppler glib")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_TYPE Release)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Find pkg-config
find_package(PkgConfig REQUIRED)

# Use pkg-config to get the necessary flags
pkg_check_modules(POPPLER REQUIRED poppler-glib)
pkg_check_modules(CAIRO REQUIRED cairo)

include(GNUInstallDirs)
include(CTest)
enable_testing()

file(GLOB_RECURSE SOURCES "src/*.c")
file(GLOB_RECURSE HEADERS "include/*.h")

add_library(${PROJECT_NAME} ${SOURCES})

# Include directories for Poppler and Cairo
target_include_directories(${PROJECT_NAME} PUBLIC ${POPPLER_INCLUDE_DIRS} ${CAIRO_INCLUDE_DIRS})

# Link directories
target_link_directories(${PROJECT_NAME} PUBLIC ${POPPLER_LIBRARY_DIRS} ${CAIRO_LIBRARY_DIRS})

# Add the compile options(CFLAGS)
target_compile_options(${PROJECT_NAME} PUBLIC -Wall -Wextra -Werror )
target_link_libraries(${PROJECT_NAME} PUBLIC glib-2.0 gobject-2.0 gio-2.0 cairo poppler-glib)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    # If in release mode, add some security flags
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        message(STATUS "Using GCC, adding security flags")
        target_compile_options(${PROJECT_NAME} PUBLIC 
            -fstack-protector-strong 
            -D_FORTIFY_SOURCE=2 
            -fPIE 
            -fPIC 
            -O2)
    endif()
endif()

# Include the include directory
target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/${PROJECT_NAME}>
)

# Set the public macros for the library, especially for the implementation
# of header-only libraries and overriding the default macros
# target_compile_definitions(${PROJECT_NAME} PUBLIC MY_MACRO=1 STB_IMAGE_IMPLEMENTATION)

# Install targets
install(FILES ${HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})

install(TARGETS ${PROJECT_NAME} 
    EXPORT ${PROJECT_NAME}_export
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} 
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install export targets
install(EXPORT ${PROJECT_NAME}_export
    FILE ${PROJECT_NAME}Targets.cmake
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

# Generate the configuration file for the library
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}Config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

# Generate the .pc file for pkg-config
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc
    @ONLY)

# Install the .pc file
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

