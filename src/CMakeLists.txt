# Core library: everything that must work without a GUI (OCPP, database, REST, logging).
add_library(sleekocpp_core STATIC
    core/AppConfig.h
    core/AppConfig.cpp
    core/Logger.h
    core/Logger.cpp
    core/CentralSystem.h
    core/CentralSystem.cpp
    core/ChargePointCommands.h
    core/ChargePointCommands.cpp
    core/ChargePointModel.h
    core/ChargePointModel.cpp
    core/RestApi.h
    core/RestApi.cpp
    core/db/Database.h
    core/db/Database.cpp
    core/ocpp/OcppMessage.h
    core/ocpp/OcppMessage.cpp
    core/ocpp/ChargePointSession.h
    core/ocpp/ChargePointSession.cpp
    core/ocpp/OcppServer.h
    core/ocpp/OcppServer.cpp
)
target_include_directories(sleekocpp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sleekocpp_core PUBLIC Qt6::Core Qt6::Network Qt6::WebSockets Qt6::Sql Qt6::HttpServer)

# Application executable: picks GUI or headless mode at startup.
add_executable(sleekocpp main.cpp)
target_link_libraries(sleekocpp PRIVATE sleekocpp_core)

# Build SleekOCPP.app instead of a plain binary (used by packaging/macos.sh, then macdeployqt).
option(SLEEKOCPP_MACOS_BUNDLE "Build a macOS .app bundle" OFF)
if(APPLE AND SLEEKOCPP_MACOS_BUNDLE)
    set_target_properties(sleekocpp PROPERTIES
        OUTPUT_NAME SleekOCPP
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_BUNDLE_NAME SleekOCPP
        MACOSX_BUNDLE_GUI_IDENTIFIER com.zaptec.sleekocpp
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
endif()

if(SLEEKOCPP_BUILD_GUI)
    add_library(sleekocpp_gui STATIC
        gui/MainWindow.h
        gui/MainWindow.cpp
        gui/ChargePointListWidget.h
        gui/ChargePointListWidget.cpp
        gui/ChargePointDetailWidget.h
        gui/ChargePointDetailWidget.cpp
        gui/ChargePointConfigurationWidget.h
        gui/ChargePointConfigurationWidget.cpp
        gui/ChargePointControlWidget.h
        gui/ChargePointControlWidget.cpp
        gui/TransactionsWidget.h
        gui/TransactionsWidget.cpp
    )
    target_include_directories(sleekocpp_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(sleekocpp_gui PUBLIC sleekocpp_core Qt6::Widgets)

    target_link_libraries(sleekocpp PRIVATE sleekocpp_gui)
    target_compile_definitions(sleekocpp PRIVATE SLEEKOCPP_HAS_GUI)
endif()
