cmake_minimum_required(VERSION 3.14)

project(Remote_Jetson_UI VERSION 0.1 LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 COMPONENTS Core Quick LinguistTools Network Multimedia REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Quick LinguistTools Network Multimedia REQUIRED)
# Tìm Gstreamer bằng PkgConfig
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-video-1.0 gstreamer-app-1.0)

set(TS_FILES Remote_Jetson_UI_vi_VN.ts)

set(PROJECT_SOURCES
        main.cpp
        qml.qrc
        udpsender.h
        udpsender.cpp
        videoreceiver.h
        videoreceiver.cpp
        ${TS_FILES}
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(Remote_Jetson_UI
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET Remote_Jetson_UI APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation

    qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
else()
    if(ANDROID)
        add_library(Remote_Jetson_UI SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(Remote_Jetson_UI
          ${PROJECT_SOURCES}
        )
    endif()

    qt5_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
endif()

target_compile_definitions(Remote_Jetson_UI
  PRIVATE $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:QT_QML_DEBUG>)
target_link_libraries(Remote_Jetson_UI
  PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Quick Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Multimedia ${GST_LIBRARIES})

target_include_directories(Remote_Jetson_UI PRIVATE ${GST_INCLUDE_DIRS})

set_target_properties(Remote_Jetson_UI PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER my.example.com
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_import_qml_plugins(Remote_Jetson_UI)
    qt_finalize_executable(Remote_Jetson_UI)
endif()
