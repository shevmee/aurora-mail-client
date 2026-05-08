set(UI_SOURCES
    # Main window
    MainWindow.hpp
    MainWindow.cpp
    MainWindow.ui
    EmailListManager.hpp
    EmailListManager.cpp

    # Scenario-façade coordinators (extracted from MainWindow)
    Coordinators/SessionCoordinator.hpp
    Coordinators/SessionCoordinator.cpp

    # Custom widgets
    Widgets/EmailItemWidget.hpp
    Widgets/EmailItemWidget.cpp
    Widgets/NetworkTextBrowser.hpp
    Widgets/NetworkTextBrowser.cpp
    Widgets/AIAssistantDialog.hpp
    Widgets/AIAssistantDialog.cpp
    Widgets/AiSettingsDialog.hpp
    Widgets/AiSettingsDialog.cpp
)

# Create static library for UI module
add_library(AuroraMailUi STATIC)

target_sources(AuroraMailUi PRIVATE ${UI_SOURCES})

target_include_directories(AuroraMailUi PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Widgets
)

target_link_libraries(AuroraMailUi PUBLIC
    AuroraMailCore
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Network
)
