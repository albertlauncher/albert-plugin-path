cmake_minimum_required(VERSION 3.16)
project(path VERSION 2.5)

find_package(Albert REQUIRED)

albert_plugin(
    INCLUDE PRIVATE $<TARGET_PROPERTY:albert::applications,INTERFACE_INCLUDE_DIRECTORIES>
    QT Concurrent Widgets
)
