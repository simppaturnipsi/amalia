find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Widgets)
find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)

qt_add_executable(amalia-launcher
    src/main.cpp
    src/UpdateEngine.cpp src/UpdateEngine.h
)
target_link_libraries(amalia-launcher PRIVATE Qt6::Core Qt6::Network Qt6::Widgets OpenSSL::Crypto)
target_compile_definitions(amalia-launcher PRIVATE AMALIA_LAUNCHER_VERSION="${PROJECT_VERSION}")
install(TARGETS amalia-launcher RUNTIME DESTINATION bin)
install(FILES launcher.json DESTINATION share/amalia/config)

set(AMALIA_UPDATE_PUBLIC_KEY "" CACHE FILEPATH "Organization Ed25519 public key in PEM format")
if(AMALIA_UPDATE_PUBLIC_KEY AND EXISTS "${AMALIA_UPDATE_PUBLIC_KEY}")
    install(FILES "${AMALIA_UPDATE_PUBLIC_KEY}" DESTINATION share/amalia/config RENAME update-public-key.pem)
else()
    message(WARNING "No AMALIA_UPDATE_PUBLIC_KEY supplied: build can launch installed versions but will reject all updates.")
endif()
