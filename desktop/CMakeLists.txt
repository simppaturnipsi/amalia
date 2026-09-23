find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network Sql WebEngineWidgets WebSockets)
find_package(Qt6Keychain REQUIRED)

qt_add_executable(amalia-desktop
    src/main.cpp
    src/AppCatalog.cpp src/AppCatalog.h
    src/MainWindow.cpp src/MainWindow.h
    src/WebAppWindow.cpp src/WebAppWindow.h
    src/OfflineQueue.cpp src/OfflineQueue.h
    src/ApiClient.cpp src/ApiClient.h
    src/JournalSocket.cpp src/JournalSocket.h
    src/JournalWidget.cpp src/JournalWidget.h
    src/CredentialStore.cpp src/CredentialStore.h
    src/AuthManager.cpp src/AuthManager.h
    resources/amalia.qrc
)

target_link_libraries(amalia-desktop PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Sql
    Qt6::WebEngineWidgets Qt6::WebSockets
)

if(TARGET Qt6Keychain::Qt6Keychain)
    target_link_libraries(amalia-desktop PRIVATE Qt6Keychain::Qt6Keychain)
    target_compile_definitions(amalia-desktop PRIVATE AMALIA_HAVE_QTKEYCHAIN=1)
elseif(TARGET qt6keychain)
    target_link_libraries(amalia-desktop PRIVATE qt6keychain)
    target_compile_definitions(amalia-desktop PRIVATE AMALIA_HAVE_QTKEYCHAIN=1)
else()
    message(FATAL_ERROR "QtKeychain was found without a supported Qt 6 target.")
endif()

target_compile_definitions(amalia-desktop PRIVATE AMALIA_VERSION="${PROJECT_VERSION}")
install(TARGETS amalia-desktop RUNTIME DESTINATION bin)
install(FILES resources/apps.json resources/amalia.json DESTINATION share/amalia/config)
