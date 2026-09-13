cmake_minimum_required(VERSION 3.16)
project(Mesh VERSION 0.1 LANGUAGES CXX)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#屏蔽不必要的报错
if(MINGW)
    add_compile_options(-Wno-error=format -Wno-format)
endif()

#注册Qt原生类
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Multimedia WebSockets)

#配置OpenSSL临时环境变量
set(OPENSSL_ROOT_DIR "E:/Code/QtCode/Dependency/openssl")
set(OPENSSL_USE_STATIC_LIBS FALSE)
#find_package(OpenSSL REQUIRED)

#注册libdatachannel外置模块(带cmake的源码)和预先禁用测试代码
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
add_subdirectory("E:/Code/QtCode/Dependency/libdatachannel" "${CMAKE_BINARY_DIR}/libdatachannel")

#注册OpenCV外置模块(带cmake的package)
set(OpenCV_DIR "E:/Code/QtCode/Dependency/opencv/install/x64/mingw/lib")
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

#注册Opus外置模块(不带cmake的原生cpp静态库)
set(OPUS_ROOT_DIR "E:/Code/QtCode/Dependency/opus")
set(OPUS_INCLUDE_DIR "${OPUS_ROOT_DIR}/include")
set(OPUS_LIB_DIR "${OPUS_ROOT_DIR}/lib")
include_directories(${OPUS_INCLUDE_DIR})
link_directories(${OPUS_LIB_DIR})
add_library(opus STATIC IMPORTED)
set_target_properties(opus PROPERTIES IMPORTED_LOCATION "${OPUS_LIB_DIR}/libopus.a")

#将cpp文件组统一定义在一个列表
set(PROJECT_SOURCES
    main.cpp
    mainwindow_worker.cpp
    mainwindow_peer.cpp
    mainwindow_video.cpp
    mainwindow_audio.cpp
    mainwindow_request.cpp
    mainwindow_uifunc.cpp
    mainwindow.h
    mainwindow.ui
    videochatwindow.h
    videochatwindow.cpp
    audiochatwindow.h
    audiochatwindow.cpp
    topbasedialog.h
    sessionbasedialog.h
    passivesessionrequestdialog.h
    initiativesessionrequestdialog.h
    filerequestdialog.h
)

#添加所有需要参与编译的本地代码文件
qt_add_executable(Mesh 
    WIN32    
    MANUAL_FINALIZATION    
    ${PROJECT_SOURCES}
    dcworker.h
    wintun.h
    tunloader.h
    tunmanager.h
    dcmanager.h
    wssignalingworker.h
    tuninworker.h
    tunoutworker.h
    filesender.h
    filedownloader.h
    videoencoder.h
    videodecoder.h
    audiocapture.h
    audioencoder.h
    audiodecoder.h
    audioplayer.h
    peerjsonworker.h
    basejsonworker.h
    jsonloader.h
    netconfig.h
    settingsdialog.h
    startupdialog.h
    request.h
    logger.h
)

#控制需要参与链接的连接目标/变量(需明确类到库的映射关系)
target_link_libraries(Mesh
    PRIVATE
        Qt6::Widgets
        Qt6::Network
        Qt6::Multimedia
        Qt6::WebSockets
        ${OpenCV_LIBS}
        datachannel
        opus
        ws2_32
        iphlpapi
        netapi32
        crypt32
        mswsock
        strmiids
        uuid
)

#Windows系统层面的一些行为
qt_finalize_executable(Mesh)
