cmake_minimum_required(VERSION 3.24)
project(VynxArc VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.12 REQUIRED COMPONENTS Widgets Concurrent Test LinguistTools)
find_program(CARGO_EXECUTABLE cargo REQUIRED)
qt_standard_project_setup()
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS core/src/*.rs)
file(GLOB_RECURSE CLI_SOURCES CONFIGURE_DEPENDS apps/cli/src/*.rs)
file(GLOB_RECURSE RAR_SOURCES CONFIGURE_DEPENDS third_party/unrar-sys/src/*.rs third_party/unrar-sys/vendor/unrar/*.cpp third_party/unrar-sys/vendor/unrar/*.hpp)
set(RUST_TARGET_DIR "${CMAKE_SOURCE_DIR}/target")
if(MSVC)
    set(RUST_LIBRARY "${RUST_TARGET_DIR}/release/vynx_arc_core.lib")
else()
    set(RUST_LIBRARY "${RUST_TARGET_DIR}/release/libvynx_arc_core.a")
endif()
add_custom_command(OUTPUT "${RUST_LIBRARY}"
    COMMAND ${CARGO_EXECUTABLE} build --workspace --release --locked
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DEPENDS ${CORE_SOURCES} ${CLI_SOURCES} ${RAR_SOURCES} apps/cli/Cargo.toml third_party/unrar-sys/build.rs core/build.rs core/Cargo.toml Cargo.toml Cargo.lock
    VERBATIM)
add_custom_target(rust_core DEPENDS "${RUST_LIBRARY}")
add_library(vynx_core STATIC IMPORTED GLOBAL)
set_target_properties(vynx_core PROPERTIES IMPORTED_LOCATION "${RUST_LIBRARY}")
add_dependencies(vynx_core rust_core)
qt_add_executable(VynxArc WIN32 apps/gui/main.cpp apps/gui/ArchiveModel.cpp apps/gui/ArchiveModel.h apps/gui/MainWindow.cpp apps/gui/MainWindow.h apps/gui/ShellRequest.cpp apps/gui/ShellRequest.h)
target_sources(VynxArc PRIVATE
    apps/gui/UiCapture.cpp
    apps/gui/dialogs/CreateArchiveDialog.cpp apps/gui/dialogs/CreateArchiveDialog.h
    apps/gui/dialogs/ExtractDialog.cpp apps/gui/dialogs/ExtractDialog.h
    apps/gui/dialogs/ConflictDialog.cpp apps/gui/dialogs/ConflictDialog.h
    apps/gui/dialogs/SettingsDialog.cpp apps/gui/dialogs/SettingsDialog.h
    apps/gui/dialogs/AboutDialog.cpp apps/gui/dialogs/AboutDialog.h
    apps/gui/Theme.cpp apps/gui/Theme.h
    apps/gui/pages/HomePage.cpp apps/gui/pages/HomePage.h
    apps/gui/pages/ArchivePage.cpp apps/gui/pages/ArchivePage.h
    apps/gui/widgets/BreadcrumbBar.cpp apps/gui/widgets/BreadcrumbBar.h
    apps/gui/widgets/IconProvider.cpp apps/gui/widgets/IconProvider.h
    apps/gui/widgets/RecentDelegate.cpp apps/gui/widgets/RecentDelegate.h
    apps/gui/widgets/OperationPanel.cpp apps/gui/widgets/OperationPanel.h
    apps/gui/widgets/ToastOverlay.cpp apps/gui/widgets/ToastOverlay.h)
target_compile_definitions(VynxArc PRIVATE VYNX_VERSION="${PROJECT_VERSION}")
target_include_directories(VynxArc PRIVATE "${RUST_TARGET_DIR}/cxxbridge")
target_link_libraries(VynxArc PRIVATE Qt6::Widgets Qt6::Concurrent vynx_core)
if(WIN32)
    if(MSVC)
        add_library(VynxShell SHARED apps/shell/ShellCommand.cpp apps/shell/VynxShell.def)
        target_link_libraries(VynxShell PRIVATE shell32 shlwapi ole32 advapi32 uuid)
        target_compile_definitions(VynxShell PRIVATE UNICODE _UNICODE)
        target_compile_options(VynxShell PRIVATE /W4 /EHsc)
        install(TARGETS VynxShell RUNTIME DESTINATION .)
        add_executable(VynxShellSmoke apps/shell/ShellSmoke.cpp)
        target_link_libraries(VynxShellSmoke PRIVATE shell32 ole32 uuid)
        target_compile_definitions(VynxShellSmoke PRIVATE UNICODE _UNICODE)
    endif()
    target_sources(VynxArc PRIVATE apps/gui/app.rc)
    if(MSVC)
        target_compile_definitions(VynxArc PRIVATE VYNX_MSVC)
        target_sources(VynxArc PRIVATE apps/gui/app.manifest)
    endif()
    target_link_libraries(VynxArc PRIVATE ws2_32 userenv bcrypt ntdll advapi32 crypt32 powrprof shell32)
    if(NOT MSVC)
        target_link_libraries(VynxArc PRIVATE winpthread)
    endif()
endif()
qt_add_resources(VynxArc branding PREFIX "/" FILES assets/icons/vynx-arc.svg assets/icons/vynx-arc.png)
qt_add_translations(VynxArc TS_FILES assets/translations/vynx_uk.ts assets/translations/vynx_ru.ts RESOURCE_PREFIX "/i18n")
install(TARGETS VynxArc RUNTIME DESTINATION .)
install(FILES "${RUST_TARGET_DIR}/release/vynxarc-cli.exe" DESTINATION .)
install(FILES LICENSE docs/THIRD_PARTY_NOTICES.md DESTINATION .)
enable_testing()
add_executable(VynxPresentationTests
    apps/gui/tests/GuiPresentationTest.cpp apps/gui/ArchiveModel.cpp apps/gui/ArchiveModel.h
    apps/gui/Theme.cpp apps/gui/widgets/IconProvider.cpp
    apps/gui/widgets/BreadcrumbBar.cpp apps/gui/widgets/BreadcrumbBar.h
    apps/gui/widgets/OperationPanel.cpp apps/gui/widgets/OperationPanel.h
    apps/gui/pages/ArchivePage.cpp apps/gui/pages/ArchivePage.h
    apps/gui/dialogs/CreateArchiveDialog.cpp apps/gui/dialogs/CreateArchiveDialog.h)
target_include_directories(VynxPresentationTests PRIVATE "${RUST_TARGET_DIR}/cxxbridge")
target_link_libraries(VynxPresentationTests PRIVATE Qt6::Widgets Qt6::Test vynx_core)
if(WIN32)
    target_link_libraries(VynxPresentationTests PRIVATE bcrypt ntdll userenv ws2_32 advapi32 shell32 ole32 crypt32)
    target_compile_options(VynxPresentationTests PRIVATE /utf-8)
endif()
set_target_properties(VynxPresentationTests PROPERTIES WIN32_EXECUTABLE FALSE)
add_test(NAME presentation COMMAND VynxPresentationTests -o -,txt)
set_tests_properties(presentation PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
if(TARGET VynxShellSmoke)
    add_test(NAME shell_smoke COMMAND VynxShellSmoke $<TARGET_FILE:VynxShell>)
endif()
add_test(NAME gui_smoke COMMAND VynxArc --smoke-test)
set_tests_properties(gui_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
