cmake_minimum_required(VERSION 3.24)

project(CodecDeck VERSION 0.1.1 LANGUAGES CXX)

include(GNUInstallDirs)
set(CODECDECK_HELPER_PATH "${CMAKE_INSTALL_FULL_LIBEXECDIR}/codecdeck-helper")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core DBus Gui Qml Quick QuickControls2 Test)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(codecdeck
    src/main.cpp
    src/bluezdevicemanager.cpp
    src/bluezdevicemanager.h
    src/appcontroller.cpp
    src/appcontroller.h
    src/configstore.cpp
    src/configstore.h
    src/pipewiremonitor.cpp
    src/pipewiremonitor.h
)

add_executable(codecdeck-helper src/helper_main.cpp)
target_compile_definitions(codecdeck-helper PRIVATE
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
)
target_link_libraries(codecdeck-helper PRIVATE Qt6::Core Qt6::DBus)

qt_add_qml_module(codecdeck
    URI io.github.mizuikki.CodecDeck
    VERSION 1.0
    NO_IMPORT_SCAN
    QML_FILES
        qml/Main.qml
)

set(CODECDECK_APP_ICON_PNG
    data/icons/hicolor/512x512/apps/io.github.mizuikki.CodecDeck.png)
set_source_files_properties(
    ${CODECDECK_APP_ICON_PNG}
    PROPERTIES QT_RESOURCE_ALIAS "io.github.mizuikki.CodecDeck.png")
qt_add_resources(codecdeck codecdeck_icons
    PREFIX "/icons"
    FILES ${CODECDECK_APP_ICON_PNG}
)

target_include_directories(codecdeck PRIVATE src)

target_compile_definitions(codecdeck PRIVATE
    CODECDECK_VERSION="${PROJECT_VERSION}"
    CODECDECK_HELPER_PATH="${CODECDECK_HELPER_PATH}"
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
)

target_link_libraries(codecdeck PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    PkgConfig::PIPEWIRE
)

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(codecdeck-tests
        tests/configstore_test.cpp
        src/configstore.cpp
        src/configstore.h
    )
    target_include_directories(codecdeck-tests PRIVATE src)
    target_compile_definitions(codecdeck-tests PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
    )
    target_link_libraries(codecdeck-tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME codecdeck-tests COMMAND codecdeck-tests)

    qt_add_executable(codecdeck-pipewire-tests
        tests/pipewiremonitor_test.cpp
        src/pipewiremonitor.cpp
        src/pipewiremonitor.h
    )
    target_include_directories(codecdeck-pipewire-tests PRIVATE src)
    target_compile_definitions(codecdeck-pipewire-tests PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
    )
    target_link_libraries(codecdeck-pipewire-tests PRIVATE
        Qt6::Core
        Qt6::Test
        PkgConfig::PIPEWIRE
    )
    add_test(NAME codecdeck-pipewire-tests COMMAND codecdeck-pipewire-tests)
    set_tests_properties(codecdeck-pipewire-tests PROPERTIES LABELS integration)

    qt_add_executable(codecdeck-bluez-tests
        tests/bluezdevicemanager_test.cpp
        src/bluezdevicemanager.cpp
        src/bluezdevicemanager.h
    )
    target_include_directories(codecdeck-bluez-tests PRIVATE src)
    target_compile_definitions(codecdeck-bluez-tests PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
    )
    target_link_libraries(codecdeck-bluez-tests PRIVATE Qt6::Core Qt6::DBus Qt6::Test)
    add_test(NAME codecdeck-bluez-tests COMMAND codecdeck-bluez-tests)
endif()

install(TARGETS codecdeck RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT Application)
install(TARGETS codecdeck-helper RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}
        COMPONENT PrivilegedHelper)
install(FILES data/io.github.mizuikki.CodecDeck.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
        COMPONENT Application)
install(FILES data/io.github.mizuikki.CodecDeck.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
        COMPONENT Application)
install(DIRECTORY data/icons/hicolor/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor
        COMPONENT Application)

configure_file(data/io.github.mizuikki.codecdeck.policy.in
               ${CMAKE_CURRENT_BINARY_DIR}/io.github.mizuikki.codecdeck.policy @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/io.github.mizuikki.codecdeck.policy
        DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions
        COMPONENT PrivilegedHelper)

set(CPACK_PACKAGE_NAME "codecdeck")
set(CPACK_PACKAGE_VENDOR "CodecDeck contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Control Bluetooth audio codecs on PipeWire")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "CodecDeck contributors")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_NAME "codecdeck")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "CodecDeck contributors")
set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/mizuikki/codecdeck")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "bluez, wireplumber, polkitd, qt6-qpa-plugins, qml6-module-org-kde-kirigami, qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEB_COMPONENT_INSTALL OFF)

include(CPack)
