cmake_minimum_required(VERSION 3.24)

project(SilentProtagonistVoice VERSION 1.0.0 LANGUAGES CXX)

set(AUTHOR_NAME "Cizlen")
set(BEAUTIFUL_NAME "Silent Protagonist Voice")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
	message(FATAL_ERROR "In-source builds are not allowed; use the presets (cmake --preset release)")
endif()

# ---------------------------------------------------------------- CommonLibSSE-NG
# SE + AE in one DLL. VR is left off: its dialogue menu layout differs and it
# needs the openvr submodule.
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

if(DEFINED ENV{COMMONLIB_SSE_FOLDER} AND EXISTS "$ENV{COMMONLIB_SSE_FOLDER}/CMakeLists.txt")
	message(STATUS "CommonLibSSE-NG from COMMONLIB_SSE_FOLDER: $ENV{COMMONLIB_SSE_FOLDER}")
	add_subdirectory("$ENV{COMMONLIB_SSE_FOLDER}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/clib-build")
else()
	message(STATUS "COMMONLIB_SSE_FOLDER not set: downloading CommonLibSSE-NG (alandtse, ng branch)")
	include(FetchContent)
	FetchContent_Declare(
		CommonLibSSE
		GIT_REPOSITORY https://github.com/alandtse/CommonLibVR.git
		GIT_TAG ng
		GIT_SHALLOW TRUE
		GIT_SUBMODULES ""  # only the VR build needs extern/openvr
	)
	FetchContent_MakeAvailable(CommonLibSSE)
endif()

get_target_property(COMMONLIB_SRC_DIR CommonLibSSE SOURCE_DIR)
if(COMMONLIB_SRC_DIR)
	include("${COMMONLIB_SRC_DIR}/cmake/CommonLibSSE.cmake")
endif()

find_path(SIMPLEINI_INCLUDE_DIRS "SimpleIni.h" REQUIRED)

# ---------------------------------------------------------------- plugin
file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")

add_commonlibsse_plugin(
	${PROJECT_NAME}
	AUTHOR "${AUTHOR_NAME}"
	SOURCES ${headers} ${sources}
)

target_include_directories(${PROJECT_NAME} PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
	"${SIMPLEINI_INCLUDE_DIRS}"
)
target_precompile_headers(${PROJECT_NAME} PRIVATE include/PCH.h)
target_compile_definitions(${PROJECT_NAME} PRIVATE MOD_NAME="${BEAUTIFUL_NAME}")

if(MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE /utf-8 /W4 /wd4100 /wd4324 /Zi)
	# a .pdb in every config, so crash loggers can name the function that failed
	target_link_options(${PROJECT_NAME} PRIVATE /DEBUG /OPT:REF /OPT:ICF)
endif()

# ---------------------------------------------------------------- output
# Set SKYRIM_MODS_FOLDER (your MO2 "mods" folder) to have every build copied
# into "<mods>/Silent Protagonist Voice" ready to enable, or SKYRIM_FOLDER to
# copy straight into the game's Data folder.
function(copy_mod_to TARGET_FOLDER)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E make_directory "${TARGET_FOLDER}/SKSE/Plugins"
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${TARGET_FOLDER}/SKSE/Plugins/"
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${TARGET_FOLDER}/SKSE/Plugins/"
		COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/dist" "${TARGET_FOLDER}"
		VERBATIM)
	message(STATUS "Build output will be copied to ${TARGET_FOLDER}")
endfunction()

if(DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
	copy_mod_to("$ENV{SKYRIM_MODS_FOLDER}/${BEAUTIFUL_NAME}")
elseif(DEFINED ENV{SKYRIM_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_FOLDER}/Data")
	copy_mod_to("$ENV{SKYRIM_FOLDER}/Data")
endif()

# a ready-to-install folder inside the build directory, whatever else happens
copy_mod_to("${CMAKE_CURRENT_BINARY_DIR}/mod")
