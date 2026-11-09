cmake_minimum_required(VERSION 3.21)

project(CK3DnaManagerQt VERSION 1.0.1 LANGUAGES CXX)

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTORCC ON)

option(BUILD_SCREENSHOTS "Build the offscreen screenshot helper" OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Sql Svg)

qt_standard_project_setup()

add_library(ck3core STATIC
    src/core/DnaData.h
    src/core/GeneCatalog.h
    src/core/GeneCatalog.cpp
    src/core/DnaCodec.h
    src/core/DnaCodec.cpp
    src/core/I18n.h
    src/core/I18n.cpp
    src/core/Database.h
    src/core/Database.cpp
    src/core/ImageUtils.h
    src/core/ImageUtils.cpp
    src/core/LegacyImporter.h
    src/core/LegacyImporter.cpp
)

target_include_directories(ck3core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ck3core PUBLIC Qt6::Core Qt6::Gui Qt6::Sql)
target_compile_definitions(ck3core PUBLIC QT_NO_CAST_FROM_ASCII)
if(WIN32)
    # Windows Imaging Component provides a WebP fallback when Qt's image plugin
    # for WebP is not installed.
    target_link_libraries(ck3core PRIVATE windowscodecs ole32 uuid)
endif()

add_library(ck3ui STATIC
    src/ui/AppStyle.h
    src/ui/AppStyle.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/EntryListDelegate.h
    src/ui/EntryListDelegate.cpp
    src/ui/FlowLayout.h
    src/ui/FlowLayout.cpp
    src/ui/PortraitWidget.h
    src/ui/PortraitWidget.cpp
    src/ui/GeneEditorWidget.h
    src/ui/GeneEditorWidget.cpp
    src/ui/AddEntryDialog.h
    src/ui/AddEntryDialog.cpp
    src/ui/QuickConvertDialog.h
    src/ui/QuickConvertDialog.cpp
    src/ui/AboutDialog.h
    src/ui/AboutDialog.cpp
)

target_link_libraries(ck3ui PUBLIC ck3core Qt6::Widgets Qt6::Svg)
target_compile_definitions(ck3ui PUBLIC QT_NO_CAST_FROM_ASCII)

set(APP_SOURCES
    src/main.cpp
    resources/resources.qrc
)
if(WIN32)
    list(APPEND APP_SOURCES resources/app.rc)
endif()

qt_add_executable(CK3DnaManager ${APP_SOURCES})

target_link_libraries(CK3DnaManager PRIVATE ck3ui)
target_compile_definitions(CK3DnaManager PRIVATE CK3DNA_VERSION="${PROJECT_VERSION}")

if(WIN32)
    set_target_properties(CK3DnaManager PROPERTIES
        WIN32_EXECUTABLE TRUE
        OUTPUT_NAME "CK3 DNA Manager Qt"
    )
else()
    # A space-free binary name follows the conventions on Linux and macOS and
    # keeps the desktop entry and packaging scripts simple.
    set_target_properties(CK3DnaManager PROPERTIES
        OUTPUT_NAME "ck3-dna-manager-qt"
    )
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(ck3_tests
        tests/test_core.cpp
        resources/resources.qrc
    )
    target_link_libraries(ck3_tests PRIVATE ck3core Qt6::Test Qt6::Widgets)
    add_test(NAME ck3_tests COMMAND ck3_tests)
endif()

if(BUILD_SCREENSHOTS)
    qt_add_executable(ck3_screenshots
        tests/screenshot.cpp
        resources/resources.qrc
    )
    target_link_libraries(ck3_screenshots PRIVATE ck3ui Qt6::Widgets)
endif()

if(WIN32)
    install(TARGETS CK3DnaManager
        BUNDLE DESTINATION .
        RUNTIME DESTINATION .
    )
elseif(APPLE)
    install(TARGETS CK3DnaManager
        BUNDLE DESTINATION .
        RUNTIME DESTINATION .
    )
else()
    include(GNUInstallDirs)
    install(TARGETS CK3DnaManager
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(FILES resources/app.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
        RENAME ck3-dna-manager-qt.desktop
    )
    install(FILES resources/icons/app.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/256x256/apps
        RENAME ck3-dna-manager-qt.png
    )
endif()
