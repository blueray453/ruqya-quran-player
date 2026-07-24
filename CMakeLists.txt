cmake_minimum_required(VERSION 3.16)
project(RuqyaQuranPlayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Multimedia)

add_executable(ruqya_quran_player
    main.cpp
)

target_link_libraries(ruqya_quran_player PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
)
