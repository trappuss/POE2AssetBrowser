cmake_minimum_required(VERSION 3.21)

project(POE2AssetBrowser
    VERSION 0.9.0
    DESCRIPTION "Native C++/Qt6 Path of Exile 2 asset browser (reads the installed game's Bundles2 directly)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Dependencies (resolved by the vcpkg toolchain — see CMakePresets.json) ──
# UI / rendering: Qt 6 Widgets + OpenGL. No glTF library — the .glb writer is self-contained.
# The Oodle decompressor (third_party/ooz) and the BC decoder (third_party/bcdec) are vendored,
# so there is nothing proprietary in the repo and no runtime download (template §1).
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)

qt_standard_project_setup()

# ── Vendored ooz (Oodle Kraken/Mermaid/Leviathan). Its kraken.cpp carries a CLI main at its tail;
# rename it so it doesn't clash with the app's main. The three .cpp files are the unmodified
# upstream release; only third_party/ooz/stdafx.h is ours (cross-platform shim). ──
add_library(ooz STATIC
    third_party/ooz/kraken.cpp
    third_party/ooz/bitknit.cpp
    third_party/ooz/lzna.cpp)
target_include_directories(ooz PRIVATE third_party/ooz)
target_compile_definitions(ooz PRIVATE main=ooz_cli_main)
if(MSVC)
    target_compile_options(ooz PRIVATE /w)       # upstream code; silence its warnings
else()
    target_compile_options(ooz PRIVATE -w -msse4.1)
endif()

qt_add_executable(POE2AssetBrowser WIN32
    src/main.cpp

    src/app/Config.h                src/app/Config.cpp
    src/app/ExportConfig.h
    src/app/SettingsDialog.h        src/app/SettingsDialog.cpp
    src/app/ExportOptionsDialog.h   src/app/ExportOptionsDialog.cpp
    src/app/ImageExportDialog.h     src/app/ImageExportDialog.cpp
    src/app/GifExportDialog.h       src/app/GifExportDialog.cpp
    src/app/ExportCapture.h         src/app/ExportCapture.cpp
    src/app/ExportNotifier.h
    src/app/MainWindow.h            src/app/MainWindow.cpp
    src/app/AppPaths.h
    src/app/AppLog.h
    src/app/LogConsole.h            src/app/LogConsole.cpp
    src/app/SehGuard.h              src/app/SehGuard.cpp
    src/app/Hotkeys.h

    src/bundle/Oodle.h              src/bundle/Oodle.cpp
    src/bundle/Bundle.h             src/bundle/Bundle.cpp
    src/bundle/BundleIndex.h        src/bundle/BundleIndex.cpp

    src/tex/DdsImage.h              src/tex/DdsImage.cpp
    src/tex/BcdecImpl.cpp

    src/model/ModelGeometry.h
    src/model/RigMath.h
    src/model/MeshParser.h          src/model/MeshParser.cpp
    src/model/AssetText.h           src/model/AssetText.cpp
    src/model/AstSkeleton.h         src/model/AstSkeleton.cpp
    src/model/GlbExporter.h         src/model/GlbExporter.cpp

    src/store/AssetStore.h          src/store/AssetStore.cpp
    src/store/IndexLoader.h         src/store/IndexLoader.cpp
    src/store/MaterialFamilyIndex.h src/store/MaterialFamilyIndex.cpp
    src/store/DatFile.h             src/store/DatFile.cpp
    src/store/NameIndex.h           src/store/NameIndex.cpp

    src/index/AssetListModel.h      src/index/AssetListModel.cpp
    src/index/Facets.h
    src/index/FunnelFilter.h        src/index/FunnelFilter.cpp

    src/gl/GLModelWidget.h          src/gl/GLModelWidget.cpp
    src/gl/GLTextureWidget.h        src/gl/GLTextureWidget.cpp
    src/gl/GifEncoder.h             src/gl/GifEncoder.cpp
    src/gl/ModelThumbnailRenderer.h src/gl/ModelThumbnailRenderer.cpp

    src/tabs/ModelsTab.h            src/tabs/ModelsTab.cpp
    src/tabs/TexturesTab.h          src/tabs/TexturesTab.cpp
    src/tabs/CustomizeTab.h         src/tabs/CustomizeTab.cpp
    src/tabs/BulkTab.h              src/tabs/BulkTab.cpp

    src/bulk/BulkExtractor.h        src/bulk/BulkExtractor.cpp

    src/report/MaterialReport.h     src/report/MaterialReport.cpp

    src/util/QueryTerm.h
    src/util/NameTemplate.h
    src/util/ExportLayout.h
    src/util/PanelPersist.h
    src/util/TextReportDialog.h
    src/util/HintBar.h
    src/util/CsvCopy.h              src/util/CsvCopy.cpp
    src/util/ThumbnailCache.h       src/util/ThumbnailCache.cpp
    src/util/HoverPreview.h         src/util/HoverPreview.cpp
    src/app/SearchableCombo.h
)

target_include_directories(POE2AssetBrowser PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ooz
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/bcdec)

# Crash resilience: /EHa lets C++ try/catch intercept Windows structured exceptions (a bad model
# or a GPU-driver fault) so one asset reports instead of taking the app down (template §2).
if(MSVC)
    target_compile_options(POE2AssetBrowser PRIVATE /EHa /MP /bigobj)
    target_compile_definitions(POE2AssetBrowser PRIVATE NOMINMAX)
endif()

target_link_libraries(POE2AssetBrowser PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    ooz)

# ── Packaging: copy the Qt runtime DLLs + plugins next to the exe so it launches stand-alone
# (run.bat does the same by hand as a fallback). ──
if(WIN32)
    set(_qt_bin "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin")
    set(_qt_plugins "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/Qt6/plugins")
    if(EXISTS "${_qt_bin}")
        install(DIRECTORY "${_qt_bin}/" DESTINATION . FILES_MATCHING PATTERN "*.dll")
    endif()
    foreach(_p platforms imageformats styles iconengines)
        if(EXISTS "${_qt_plugins}/${_p}")
            install(DIRECTORY "${_qt_plugins}/${_p}" DESTINATION . FILES_MATCHING PATTERN "*.dll")
        endif()
    endforeach()
endif()

install(TARGETS POE2AssetBrowser RUNTIME DESTINATION . BUNDLE DESTINATION .)
