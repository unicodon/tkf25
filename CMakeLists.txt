cmake_minimum_required(VERSION 3.15)
project(tkf25 CXX)

find_package(CURL CONFIG REQUIRED)
find_package(unifex CONFIG REQUIRED)

add_executable(app WIN32
   src/main_win.cpp
   src/workqueue.cpp
   src/curl_workqueue.cpp
   src/gif.cpp
   src/app.cpp)
set_property(TARGET app PROPERTY CXX_STANDARD 20)
target_link_libraries(app PRIVATE CURL::libcurl unifex::unifex)

