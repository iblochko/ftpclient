cmake_minimum_required(VERSION 3.30)
project(ftpclient C)

set(CMAKE_C_STANDARD 11)

add_executable(ftpclient ftp_client.c)
