cmake_minimum_required(VERSION 3.24)

project(PurrFind VERSION 0.5.0 LANGUAGES CXX)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PURRFIND_RELEASE_VERSION LIMIT_COUNT 1)
string(STRIP "${PURRFIND_RELEASE_VERSION}" PURRFIND_RELEASE_VERSION)
if(PURRFIND_RELEASE_VERSION STREQUAL "")
    message(FATAL_ERROR "VERSION must contain the PurrFind release version")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)
include(CTest)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Concurrent DBus Gui Multimedia Qml Quick QuickControls2 Widgets)
find_package(X11 QUIET)
find_package(SQLite3 3.34 REQUIRED)
find_package(PkgConfig REQUIRED)

option(PURRFIND_WITH_PDF "Enable PDF content extraction with Poppler-Qt6" ON)
option(PURRFIND_WITH_OFFICE "Enable OOXML/OpenDocument extraction with libzip/libxml2" ON)
option(PURRFIND_WITH_EXIV2 "Enable advanced image metadata with Exiv2" ON)
option(PURRFIND_WITH_OCR "Enable local OCR with Tesseract" ON)

if(PURRFIND_WITH_PDF)
    pkg_check_modules(POPPLER_QT6 REQUIRED IMPORTED_TARGET poppler-qt6)
endif()
if(PURRFIND_WITH_EXIV2)
    pkg_check_modules(EXIV2 REQUIRED IMPORTED_TARGET exiv2)
endif()
if(PURRFIND_WITH_OFFICE)
    pkg_check_modules(LIBZIP REQUIRED IMPORTED_TARGET libzip)
    pkg_check_modules(LIBXML2 REQUIRED IMPORTED_TARGET libxml-2.0)
endif()
if(PURRFIND_WITH_OCR)
    pkg_check_modules(TESSERACT REQUIRED IMPORTED_TARGET tesseract)
endif()

add_library(purrfind-core STATIC
    src/core/Config.cpp
    src/core/Database.cpp
    src/core/FileSystem.cpp
    src/core/Logging.cpp
    src/core/QueryParser.cpp
    src/core/SearchEngine.cpp
)
target_include_directories(purrfind-core PUBLIC src)
target_link_libraries(purrfind-core PUBLIC Qt6::Core SQLite3::SQLite3)
target_compile_definitions(purrfind-core PUBLIC
    PURRFIND_VERSION="${PURRFIND_RELEASE_VERSION}"
)
target_compile_options(purrfind-core PRIVATE -Wall -Wextra -Wpedantic)

add_library(purrfind-content STATIC
    src/content/ArchiveReader.cpp
    src/content/ExtractorRegistry.cpp
    src/content/OfficeExtractor.cpp
    src/content/OpenDocumentExtractor.cpp
    src/content/PdfExtractor.cpp
    src/content/PlainTextExtractor.cpp
    src/content/TextNormalizer.cpp
)
target_include_directories(purrfind-content PUBLIC src)
target_link_libraries(purrfind-content PUBLIC purrfind-core)
target_compile_options(purrfind-content PRIVATE -Wall -Wextra -Wpedantic)
if(PURRFIND_WITH_PDF)
    target_compile_definitions(purrfind-content PUBLIC PURRFIND_WITH_PDF=1)
    target_link_libraries(purrfind-content PRIVATE PkgConfig::POPPLER_QT6)
endif()
if(PURRFIND_WITH_OFFICE)
    target_compile_definitions(purrfind-content PUBLIC PURRFIND_WITH_OFFICE=1)
    target_link_libraries(purrfind-content PRIVATE PkgConfig::LIBZIP PkgConfig::LIBXML2)
endif()

add_library(purrfind-metadata STATIC
    src/metadata/ImageMetadataProvider.cpp
    src/metadata/MetadataRegistry.cpp
)
target_include_directories(purrfind-metadata PUBLIC src)
target_link_libraries(purrfind-metadata PUBLIC purrfind-core Qt6::Gui)
target_compile_options(purrfind-metadata PRIVATE -Wall -Wextra -Wpedantic)
if(PURRFIND_WITH_EXIV2)
    target_compile_definitions(purrfind-metadata PUBLIC PURRFIND_WITH_EXIV2=1)
    target_link_libraries(purrfind-metadata PRIVATE PkgConfig::EXIV2)
endif()

add_library(purrfind-preview STATIC
    src/preview/FolderPreviewProvider.cpp
    src/preview/GenericPreviewProvider.cpp
    src/preview/ImagePreviewProvider.cpp
    src/preview/OfficePreviewProvider.cpp
    src/preview/PdfPreviewProvider.cpp
    src/preview/PreviewCache.cpp
    src/preview/PreviewImageProvider.cpp
    src/preview/PreviewRegistry.cpp
    src/preview/TextPreviewProvider.cpp
)
target_include_directories(purrfind-preview PUBLIC src)
target_link_libraries(purrfind-preview PUBLIC purrfind-core purrfind-content purrfind-metadata
    Qt6::Concurrent Qt6::Gui Qt6::Quick)
target_compile_options(purrfind-preview PRIVATE -Wall -Wextra -Wpedantic)
if(PURRFIND_WITH_PDF)
    target_link_libraries(purrfind-preview PRIVATE PkgConfig::POPPLER_QT6)
endif()

add_library(purrfind-ocr STATIC
    src/ocr/OcrLanguageManager.cpp
    src/ocr/OcrQueue.cpp
    src/ocr/OcrScheduler.cpp
    src/ocr/TesseractOcrEngine.cpp
)
target_include_directories(purrfind-ocr PUBLIC src)
target_link_libraries(purrfind-ocr PUBLIC purrfind-core Qt6::Core Qt6::Gui)
target_compile_options(purrfind-ocr PRIVATE -Wall -Wextra -Wpedantic)
if(PURRFIND_WITH_OCR)
    target_compile_definitions(purrfind-ocr PUBLIC PURRFIND_WITH_OCR=1)
    target_link_libraries(purrfind-ocr PRIVATE PkgConfig::TESSERACT)
endif()

if(PURRFIND_WITH_OCR)
    add_executable(purrfind-ocr-worker src/ocr/worker_main.cpp)
    target_link_libraries(purrfind-ocr-worker PRIVATE purrfind-ocr)
    target_compile_options(purrfind-ocr-worker PRIVATE -Wall -Wextra -Wpedantic)
    if(PURRFIND_WITH_PDF)
        target_compile_definitions(purrfind-ocr-worker PRIVATE PURRFIND_WITH_PDF=1)
        target_link_libraries(purrfind-ocr-worker PRIVATE PkgConfig::POPPLER_QT6)
    endif()
    add_executable(purrfind-ocr-benchmark benchmarks/ocr_benchmark.cpp)
    target_link_libraries(purrfind-ocr-benchmark PRIVATE purrfind-ocr purrfind-core Qt6::Gui)
endif()

add_executable(purrfind-indexer
    src/indexer/main.cpp
    src/indexer/Crawler.cpp
    src/indexer/ContentIndexQueue.cpp
    src/indexer/EventQueue.cpp
    src/indexer/IndexerService.cpp
    src/indexer/InotifyWatcher.cpp
    src/indexer/MetadataQueue.cpp
)
target_link_libraries(purrfind-indexer PRIVATE purrfind-core purrfind-content purrfind-metadata purrfind-ocr Qt6::DBus)
target_compile_options(purrfind-indexer PRIVATE -Wall -Wextra -Wpedantic)

qt_add_executable(purrfind
    src/app/main.cpp
    src/app/AppController.cpp
    src/app/GlobalShortcut.cpp
    src/app/SearchModel.cpp
)
qt_add_resources(purrfind "qml_resources"
    PREFIX "/"
    FILES qml/Main.qml qml/SettingsDialog.qml resources/icons/io.github.guedessoftware.PurrFind.svg
          resources/icons/purrfind-brand.svg resources/icons/logo_fundo_claro.png
          resources/icons/logo_fundo_escuro.png resources/icons/categories/all.svg
          resources/icons/categories/file.svg resources/icons/categories/folder.svg
          resources/icons/categories/image.svg resources/icons/categories/document.svg
          resources/icons/categories/video.svg resources/icons/categories/other.svg
)
target_link_libraries(purrfind PRIVATE
    purrfind-core purrfind-preview Qt6::Core Qt6::Concurrent Qt6::DBus Qt6::Gui Qt6::Qml
    Qt6::Multimedia Qt6::Quick Qt6::QuickControls2 Qt6::Widgets
)
if(X11_FOUND)
    target_compile_definitions(purrfind PRIVATE PURRFIND_WITH_X11_SHORTCUT=1)
    target_link_libraries(purrfind PRIVATE X11::X11)
endif()
target_compile_options(purrfind PRIVATE -Wall -Wextra -Wpedantic)

add_executable(purrfind-benchmark benchmarks/benchmark.cpp)
target_link_libraries(purrfind-benchmark PRIVATE purrfind-core)

add_executable(purrfind-content-benchmark benchmarks/content_benchmark.cpp)
target_link_libraries(purrfind-content-benchmark PRIVATE purrfind-core)

add_executable(purrfind-preview-benchmark benchmarks/preview_benchmark.cpp)
target_link_libraries(purrfind-preview-benchmark PRIVATE purrfind-preview Qt6::Gui)

add_executable(purrfind-metadata-benchmark benchmarks/metadata_benchmark.cpp)
target_link_libraries(purrfind-metadata-benchmark PRIVATE purrfind-core)

if(BUILD_TESTING)
    add_executable(purrfind-core-tests tests/core_tests.cpp)
    target_link_libraries(purrfind-core-tests PRIVATE purrfind-core)
    add_test(NAME core COMMAND purrfind-core-tests)

    add_executable(purrfind-integration-tests
        tests/integration_tests.cpp
        src/indexer/Crawler.cpp
        src/indexer/ContentIndexQueue.cpp
        src/indexer/EventQueue.cpp
        src/indexer/InotifyWatcher.cpp
        src/indexer/MetadataQueue.cpp
    )
    target_link_libraries(purrfind-integration-tests PRIVATE purrfind-core purrfind-content purrfind-metadata Qt6::Core)
    add_test(NAME integration COMMAND purrfind-integration-tests)

    add_executable(purrfind-inotify-stress-tests
        tests/inotify_stress_tests.cpp
        src/indexer/Crawler.cpp
        src/indexer/InotifyWatcher.cpp
    )
    target_link_libraries(purrfind-inotify-stress-tests PRIVATE purrfind-core Qt6::Core)
    add_test(NAME inotify-stress COMMAND purrfind-inotify-stress-tests)
    set_tests_properties(inotify-stress PROPERTIES TIMEOUT 180 LABELS "stress")

    add_executable(purrfind-recovery-tests
        tests/recovery_tests.cpp
        src/indexer/Crawler.cpp
        src/indexer/ContentIndexQueue.cpp
        src/indexer/EventQueue.cpp
        src/indexer/IndexerService.cpp
        src/indexer/InotifyWatcher.cpp
        src/indexer/MetadataQueue.cpp
    )
    target_link_libraries(purrfind-recovery-tests PRIVATE
        purrfind-core purrfind-content purrfind-metadata purrfind-ocr Qt6::DBus)
    add_test(NAME recovery COMMAND purrfind-recovery-tests)

    add_executable(purrfind-content-tests tests/content_tests.cpp)
    target_link_libraries(purrfind-content-tests PRIVATE purrfind-content Qt6::Gui)
    if(PURRFIND_WITH_OFFICE)
        target_link_libraries(purrfind-content-tests PRIVATE PkgConfig::LIBZIP)
    endif()
    add_test(NAME content COMMAND purrfind-content-tests)

    add_executable(purrfind-preview-tests tests/preview_tests.cpp)
    target_link_libraries(purrfind-preview-tests PRIVATE purrfind-preview Qt6::Gui)
    if(PURRFIND_WITH_OFFICE)
        target_link_libraries(purrfind-preview-tests PRIVATE PkgConfig::LIBZIP)
    endif()
    if(PURRFIND_WITH_EXIV2)
        target_link_libraries(purrfind-preview-tests PRIVATE PkgConfig::EXIV2)
    endif()
    add_test(NAME preview COMMAND purrfind-preview-tests)
    set_tests_properties(preview PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

    if(PURRFIND_WITH_OCR)
        add_executable(purrfind-ocr-tests tests/ocr_tests.cpp)
        target_link_libraries(purrfind-ocr-tests PRIVATE purrfind-ocr purrfind-preview Qt6::Gui)
        add_test(NAME ocr COMMAND purrfind-ocr-tests $<TARGET_FILE:purrfind-ocr-worker>)
        set_tests_properties(ocr PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()

configure_file(packaging/purrfind-indexer.service.in purrfind-indexer.service @ONLY)
configure_file(packaging/org.purrfind.Indexer.service.in org.purrfind.Indexer.service @ONLY)

install(TARGETS purrfind purrfind-indexer
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(PURRFIND_WITH_OCR)
    install(TARGETS purrfind-ocr-worker RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/purrfind-indexer.service
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/user
)
install(FILES packaging/io.github.guedessoftware.PurrFind.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
install(FILES packaging/io.github.guedessoftware.PurrFind-autostart.desktop
    DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/xdg/autostart
)
install(FILES packaging/io.github.guedessoftware.PurrFind.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
)
install(FILES resources/icons/io.github.guedessoftware.PurrFind.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.purrfind.Indexer.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services
)

set(CPACK_PACKAGE_NAME "purrfind")
set(CPACK_PACKAGE_VENDOR "Guedes Software")
set(CPACK_PACKAGE_CONTACT "PurrFind contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Fast, native file and content search for Linux")
set(CPACK_PACKAGE_VERSION "0.5.0")
set(CPACK_PACKAGE_FILE_NAME "purrfind-${PURRFIND_RELEASE_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_VERSION "0.5.0~rc2")
# dpkg-shlibdeps is not available on every build host; make the architecture
# deterministic instead of allowing CPack to fall back to i386.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
endif()
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_VERSION "0.5.0")
set(CPACK_RPM_PACKAGE_RELEASE "0.rc2")
set(CPACK_RPM_PACKAGE_AUTOREQ ON)
set(CPACK_SOURCE_GENERATOR "TXZ")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "PurrFind-${PURRFIND_RELEASE_VERSION}-source")
set(CPACK_SOURCE_IGNORE_FILES "/build[^/]*/;/dist/;/.git/;/.cache/;\\.sqlite3(-wal|-shm)?$;~$")
include(CPack)
