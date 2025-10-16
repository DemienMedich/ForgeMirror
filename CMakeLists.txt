cmake_minimum_required(VERSION 3.15)
project(JobSkill LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(JobSkill
    main.cpp
)

# Для Windows корректно установить кодировку консоли при необходимости вручную в приложении

