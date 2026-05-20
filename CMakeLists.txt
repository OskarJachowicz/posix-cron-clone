cmake_minimum_required(VERSION 3.20)
project(SCR_projekt_2 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_executable(SCR_projekt_2 main.c logger.c logger.h defs.h server.c client.c cron.h)

target_link_libraries(SCR_projekt_2 PRIVATE Threads::Threads rt)