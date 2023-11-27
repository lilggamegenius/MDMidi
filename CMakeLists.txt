cmake_minimum_required(VERSION 3.27)
project(MDMidi)

set(CMAKE_CXX_STANDARD 23)

set(MDMidiDrv_src
		MDMidiDrv/src/MDMidiDrv.cpp
		MDMidiDrv/src/dsound.cpp
		MDMidiDrv/src/sound_out_dsound.cpp
		MDMidiDrv/src/sound_out_winmm.cpp
		MDMidiDrv/src/sound_out_xaudio2.cpp

		MDMidiPlay/src/Loader.c
		MDMidiPlay/src/Sound.c
		MDMidiPlay/src/2612_reader.c
		MDMidiPlay/src/Stream.c
		MDMidiPlay/src/MainEngine.c
		MDMidiPlay/src/chips/2612intf.c
		MDMidiPlay/src/chips/sn764intf.c
		MDMidiPlay/src/chips/fm2612.c
		MDMidiPlay/src/chips/sn76496.c
		MDMidiPlay/src/Player.c
		MDMidiPlay/src/2612_checksum.c
		MDMidiPlay/src/Midi1to0.c
)
#set(MDMidiPlay_src)

add_library(MDMidiDrv SHARED ${MDMidiDrv_src})
target_include_directories(MDMidiDrv PRIVATE MDMidiDrv/lib)

add_executable(MDMidiPlay ${MDMidiDrv_src})
#add_executable(MDMidiPlay ${MDMidiPlay_src})
#target_link_libraries(MDMidiPlay MDMidiDrv)