cmake_minimum_required(VERSION 3.16)
project(Cave68kSDK VERSION 0.2.0 LANGUAGES C)

# The board runtime for recompiled Cave 68000 games: a static library. A game links it with its recompiled
# program, which supplies the code and tables src/game_program.c calls.
add_library(cave68k STATIC
	src/machine.c
	src/video.c
	src/sound.c
	src/rom_set.c
	src/inflate.c
	src/game_program.c
	src/tasks.c
	src/cheats.c
	src/free_play.c
	src/boards/donpachi.c
	src/boards/ddonpach.c
)
target_include_directories(cave68k PUBLIC include PRIVATE src)
set_target_properties(cave68k PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
if(NOT MSVC)
	target_link_libraries(cave68k PUBLIC m)
endif()
