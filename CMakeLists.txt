cmake_minimum_required(VERSION 3.5)
project(MDMidi)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
include(GenerateExportHeader)

set(MDMidiDrv_src
		MDMidiDrv/src/MDMidiDrv.cpp
		MDMidiDrv/src/dsound.cpp
		MDMidiDrv/src/sound_out_dsound.cpp
		MDMidiDrv/src/sound_out_winmm.cpp
		MDMidiDrv/src/sound_out_xaudio2.cpp

		MDMidiDrv/lib/SoundEngine/chips/2612intf.c
		MDMidiDrv/lib/SoundEngine/chips/fm2612.c
		MDMidiDrv/lib/SoundEngine/chips/sn764intf.c
		MDMidiDrv/lib/SoundEngine/chips/sn76496.c

		MDMidiDrv/src/2612_reader.c
		MDMidiDrv/src/MainEngine.c
		MDMidiDrv/src/Loader.c
		MDMidiDrv/src/Sound.c

		MDMidiDrv/include/MDMidDrv.h
)
set(MDMidiPlay_src
		MDMidiPlay/src/Player.c
		MDMidiPlay/src/Stream.c
		MDMidiPlay/src/Midi1to0.c
)

#set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS 1)

add_library(MDMidiDrv SHARED ${MDMidiDrv_src})
target_link_libraries(MDMidiDrv winmm XAUDIO2_8 dsound)
target_include_directories(MDMidiDrv PRIVATE MDMidiDrv/lib MDMidiDrv/src)
target_include_directories(MDMidiDrv PUBLIC MDMidiDrv/include)
GENERATE_EXPORT_HEADER(
		MDMidiDrv
		BASE_NAME MDMidiDrv
		EXPORT_MACRO_NAME MDMidiDrv_EXPORT
		EXPORT_FILE_NAME MDMidiDrv_Export.h
		STATIC_DEFINE MDMidiDrv_BUILT_AS_STATIC
)
target_include_directories(MDMidiDrv PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(MDMidiPlay ${MDMidiPlay_src})
target_include_directories(MDMidiPlay PRIVATE MDMidiDrv/lib)
target_include_directories(MDMidiPlay PUBLIC MDMidiDrv/include)
target_link_libraries(MDMidiPlay MDMidiDrv)