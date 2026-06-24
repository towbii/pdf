cmake_minimum_required(VERSION 3.21)
project(PDFEditor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui)

# ---------- MuPDF ----------
# Try vcpkg-style find first (package name: unofficial-libmupdf)
find_package(unofficial-libmupdf CONFIG QUIET)

if(unofficial-libmupdf_FOUND)
    message(STATUS "MuPDF: found via vcpkg (unofficial-libmupdf)")
    set(MUPDF_TARGET unofficial::libmupdf::libmupdf)
else()
    # Fallback: manual MUPDF_DIR
    if(NOT DEFINED MUPDF_DIR)
        set(MUPDF_DIR "$ENV{MUPDF_DIR}" CACHE PATH "MuPDF installation root")
    endif()
    find_path(MUPDF_INCLUDE_DIR mupdf/fitz.h
        PATHS "${MUPDF_DIR}/include" "C:/mupdf/include"
    )
    find_library(MUPDF_LIB NAMES mupdf libmupdf
        PATHS "${MUPDF_DIR}/lib" "${MUPDF_DIR}/Release" "${MUPDF_DIR}"
              "C:/mupdf/lib"
    )
    if(NOT MUPDF_INCLUDE_DIR OR NOT MUPDF_LIB)
        message(FATAL_ERROR
            "MuPDF not found!\n"
            "  vcpkg:  vcpkg install libmupdf:x64-windows\n"
            "  manual: set -DMUPDF_DIR=C:/path/to/mupdf")
    endif()
    message(STATUS "MuPDF: ${MUPDF_LIB}")
    set(MUPDF_TARGET ${MUPDF_LIB})
endif()

# ---------- Sources ----------
set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/PdfDocument.cpp
    src/PdfView.cpp
    src/ThumbnailPanel.cpp
    src/SignatureDialog.cpp
    src/SignaturePickerDialog.cpp
    src/KeybindDialog.cpp
)
if(WIN32)
    list(APPEND SOURCES src/PDFEditor.rc)
endif()
set(HEADERS
    src/MainWindow.h
    src/PdfDocument.h
    src/PdfView.h
    src/ThumbnailPanel.h
    src/SignatureDialog.h
    src/SignaturePickerDialog.h
    src/KeybindDialog.h
    src/Theme.h
)

qt_add_executable(PDFEditor WIN32 ${SOURCES} ${HEADERS})

# Copy translation files next to the executable after each build
add_custom_command(TARGET PDFEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_SOURCE_DIR}/translations/en.qm"
        "$<TARGET_FILE_DIR:PDFEditor>/en.qm"
    COMMENT "Copying translation en.qm"
)

target_include_directories(PDFEditor PRIVATE src)
if(DEFINED MUPDF_INCLUDE_DIR)
    target_include_directories(PDFEditor PRIVATE ${MUPDF_INCLUDE_DIR})
endif()

target_link_libraries(PDFEditor PRIVATE
    Qt6::Core Qt6::Widgets Qt6::Gui
    ${MUPDF_TARGET}
)
if(WIN32)
    target_link_libraries(PDFEditor PRIVATE
        User32 Gdi32 Shell32 Advapi32 Comdlg32 Ole32 OleAut32 Winspool)
    qt_generate_deploy_app_script(TARGET PDFEditor
        OUTPUT_SCRIPT deploy_script NO_TRANSLATIONS NO_COMPILER_RUNTIME)
    install(SCRIPT ${deploy_script})
endif()
