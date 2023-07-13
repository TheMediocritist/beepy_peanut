cmake_minimum_required(VERSION 3.14)
project(peanutGB_Linux C)

set(CMAKE_C_STANDARD 11)

set(SOURCE_FILES
    src/main.c
    src/rom_list.c
    src/peanut_gb.h
    src/minigb_apu/minigb_apu.c
    src/minigb_apu/minigb_apu.h
)

add_executable(peanutGB_Linux ${SOURCE_FILES})
