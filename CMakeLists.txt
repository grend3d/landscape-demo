cmake_minimum_required(VERSION 3.10)
project(map-viewer-thing VERSION 0.1)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if (EXISTS ./grend)
	message(STATUS "Found grend subdirectory, using that as library")
	set(GREND_PATH ./grend)
endif()

if (GREND_PATH)
	message(STATUS "GREND_PATH: ${GREND_PATH}")
	set(BULLET_PHYSICS_SOURCE_DIR ./bullet)
	add_subdirectory(bullet)
	add_subdirectory(${GREND_PATH})
endif()

set (DEMO_LINK_LIBS)
if (ANDROID OR GREND_PATH)
	# everything needed should be pulled in here
	find_library(Grend Grend)
	list(APPEND DEMO_LINK_LIBS Grend)

# TODO: assume unixen, presumably there'll be different library lookup
#       stuff for windows/etc
else()
	add_compile_options(-DDEMO_PREFIX="${CMAKE_INSTALL_PREFIX}/share/landscape-demo/")
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(Grend REQUIRED IMPORTED_TARGET grend)
	list(APPEND DEMO_LINK_LIBS PkgConfig::Grend)
endif()

set(LANDSCAPE_DEMO_SRC
	src/boxSpawner.cpp
	src/boxSpawner.hpp
	src/enemy.cpp
	src/healthbar.cpp
	src/inputHandler.cpp
	src/landscapeGenerator.cpp
	src/main.cpp
	src/player.cpp
	src/projectile.cpp
)

if (ANDROID)
	message(STATUS "Android!")
endif()
if (MINGW)
	message(STATUS "MinGW!")
endif()
if (CYGWIN)
	message(STATUS "Cygwin!")
endif()
if (UNIX)
	message(STATUS "Unix!")
endif()
if (WIN32)
	message(STATUS "Win32!")
endif()

if (ANDROID)
	message(STATUS "Setting library for android")
	set(TARGET_NAME main)
	add_library(main SHARED ${LANDSCAPE_DEMO_SRC})
	install(TARGETS main DESTINATION lib)
	list(APPEND DEMO_LINK_LIBS SDL2 SDL2main)

else()
	if(MINGW OR WIN32 OR CYGWIN)
		message(STATUS "Got here")
		#add_compile_options(-lopengl32)
		find_library(opengl32 opengl32)
		list(APPEND DEMO_LINK_LIBS opengl32)
	endif()

	message(STATUS "Setting standalone executable")
	set(TARGET_NAME landscape-demo)
	add_executable(landscape-demo ${LANDSCAPE_DEMO_SRC})
	install(TARGETS landscape-demo DESTINATION bin)
endif()

target_include_directories(${TARGET_NAME} PUBLIC "${PROJECT_BINARY_DIR}")
target_include_directories(${TARGET_NAME} PUBLIC Grend)
target_link_libraries(${TARGET_NAME} ${DEMO_LINK_LIBS})