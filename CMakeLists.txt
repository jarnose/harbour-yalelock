cmake_minimum_required(VERSION 3.19)
project(harbour-yalelock CXX)

find_package(Qt5 COMPONENTS Core Qml Gui Quick Sql DBus LinguistTools REQUIRED)

include(FindPkgConfig)
pkg_search_module(SAILFISH sailfishapp REQUIRED)
pkg_search_module(SAILFISHSECRETS sailfishsecrets REQUIRED)
pkg_search_module(OPENSSL openssl REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# The lock is reached through BlueZ's D-Bus API (org.bluez.*) with Qt5::DBus,
# not Qt Bluetooth: Sailfish OS 5.1's SDK target has no pkgconfig(Qt5Bluetooth).
# Same approach as harbour-edgesync, harbour-ourasync and harbour-suuntosync.

add_executable(harbour-yalelock
    src/harbour-yalelock.cpp
    src/protocol/lockprotocol.cpp
    src/protocol/lockcipher.cpp
    src/ble/bluezmanagedobjects.cpp
    src/ble/yalescanner.cpp
    src/ble/yalelink.cpp
    src/secrets/secretvault.cpp
    src/model/lockstore.cpp
    src/model/locklistmodel.cpp
    src/controller/appcontroller.cpp
)

target_compile_definitions(harbour-yalelock PRIVATE
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:QT_QML_DEBUG>
)

target_include_directories(harbour-yalelock PRIVATE
    $<BUILD_INTERFACE:
    ${SAILFISH_INCLUDE_DIRS}
    ${SAILFISHSECRETS_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIRS}
>)

target_link_libraries(harbour-yalelock
    Qt5::Quick
    Qt5::Sql
    Qt5::DBus
    ${SAILFISH_LDFLAGS}
    ${SAILFISHSECRETS_LDFLAGS}
    ${OPENSSL_LDFLAGS}
)

install(TARGETS harbour-yalelock
    RUNTIME DESTINATION bin
)
install(DIRECTORY qml
    DESTINATION share/harbour-yalelock
)
install(FILES harbour-yalelock.desktop
    DESTINATION share/applications
)
install(FILES icons/86x86/harbour-yalelock.png
    DESTINATION share/icons/hicolor/86x86/apps
)
install(FILES icons/108x108/harbour-yalelock.png
    DESTINATION share/icons/hicolor/108x108/apps
)
install(FILES icons/128x128/harbour-yalelock.png
    DESTINATION share/icons/hicolor/128x128/apps
)
install(FILES icons/172x172/harbour-yalelock.png
    DESTINATION share/icons/hicolor/172x172/apps
)

# Source strings (qsTr()/tr()/QCoreApplication::translate()) are English;
# translations/harbour-yalelock.ts tracks them as the extraction base (kept
# up to date by manually running lupdate, not as part of this build) and each
# translations/harbour-yalelock-<lang>.ts is a translated copy of it.
# qt5_add_translation only compiles .ts -> .qm (lrelease); it never runs
# lupdate, so a normal build never touches the committed .ts files.
file(GLOB TS_FILES "${CMAKE_SOURCE_DIR}/translations/*.ts")
file(GLOB LANG_TS_FILES "${CMAKE_SOURCE_DIR}/translations/harbour-yalelock-*.ts")
qt5_add_translation(QM_FILES ${LANG_TS_FILES})
add_custom_target(translations ALL DEPENDS ${QM_FILES})
add_dependencies(harbour-yalelock translations)

install(FILES ${QM_FILES}
    DESTINATION share/harbour-yalelock/translations
)

# Get the other files reachable from the project tree in Qt Creator
add_custom_target(distfiles
    SOURCES
        harbour-yalelock.desktop
        qml/harbour-yalelock.qml
        qml/cover/CoverPage.qml
        qml/pages/MainPage.qml
        qml/pages/AddLockPage.qml
        qml/pages/ScanPage.qml
        qml/pages/RenameLockPage.qml
        rpm/harbour-yalelock.spec
        README.md
        THIRD-PARTY-NOTICES.md
        ${TS_FILES})

# Tell Qt Creator where the application executable(s) would be located on the device.
file(WRITE "${CMAKE_BINARY_DIR}/QtCreatorDeployment.txt"
    "${CMAKE_INSTALL_PREFIX}
${CMAKE_BINARY_DIR}/harbour-yalelock:bin
")
