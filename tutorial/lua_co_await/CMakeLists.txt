CMAKE_MINIMUM_REQUIRED (VERSION 3.12)
PROJECT (lua_co_await)
SET (IRIS_CORE_SRC_DIR "${PROJECT_SOURCE_DIR}/../../src")

SET (STDLIB stdc++)
STRING (REPLACE "/" "\\" LOCAL_SOURCE_DIR "${PROJECT_SOURCE_DIR}")
SET_PROPERTY (GLOBAL PROPERTY USE_FOLDERS ON)

# https://stackoverflow.com/questions/45092198/cmake-how-do-i-change-properties-on-subdirectory-project-targets
DEFINE_PROPERTY (
    TARGET
    PROPERTY FOLDER
    INHERITED
    BRIEF_DOCS "Set the folder name."
    FULL_DOCS  "Use to organize targets in an IDE."
)

FUNCTION (ADD_SUBDIRECTORY_WITH_FOLDER _folder_name _folder)
    ADD_SUBDIRECTORY (${_folder} ${ARGN})
    SET_PROPERTY (DIRECTORY "${_folder}" PROPERTY FOLDER "${_folder_name}")
ENDFUNCTION ()

SET (CMAKE_CXX_STANDARD 20)

OPTION (BUILD_MONOLITHIC "Build Monolithic" OFF)
OPTION (BUILD_STANDALONE "Build Standalone (must enable Monolithic/Builtin Lua first)" OFF)

MESSAGE (STATUS "Current platform: ${CMAKE_GENERATOR_PLATFORM}")
SET (COND_VLD WIN32 AND (${CMAKE_GENERATOR_PLATFORM} EQUAL "x64" OR ${CMAKE_GENERATOR_PLATFORM} EQUAL "Win32"))

IF (COND_VLD)
	OPTION (ENABLE_VLD "Enable Visual Leak Detector" OFF)
	IF (ENABLE_VLD)
		ADD_COMPILE_DEFINITIONS (USE_VLD)
		# cmake does not allow parentheses in environment variable, so $ENV{ProgramFiles(x86)} is prohibited
		FILE (TO_CMAKE_PATH "$ENV{ProgramFiles} (x86)" PROGRAM_FILES)
		INCLUDE_DIRECTORIES ("${PROGRAM_FILES}/Visual Leak Detector/include")

		IF (CMAKE_SIZEOF_VOID_P EQUAL 8)
			LINK_DIRECTORIES ("${PROGRAM_FILES}/Visual Leak Detector/lib/Win64")
		ELSE (CMAKE_SIZEOF_VOID_P EQUAL 8)
			LINK_DIRECTORIES ("${PROGRAM_FILES}/Visual Leak Detector/lib/Win32")
		ENDIF (CMAKE_SIZEOF_VOID_P EQUAL 8)
	ENDIF (ENABLE_VLD)
ENDIF (COND_VLD)

IF (NOT MSVC)
	ADD_DEFINITIONS(-fPIC)
ENDIF (NOT MSVC)

SET (USE_LUA_VERSION "Builtin" CACHE STRING "Lua version")
SET_PROPERTY (CACHE USE_LUA_VERSION PROPERTY STRINGS "Builtin" "LuaJIT" "5.1" "5.2" "5.3" "5.4")
SET (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")
INCLUDE_DIRECTORIES ("${PROJECT_BINARY_DIR}")
INCLUDE_DIRECTORIES ("${PROJECT_SOURCE_DIR}/ref")
INCLUDE_DIRECTORIES ("${IRIS_CORE_SRC_DIR}")

FILE (GLOB_RECURSE IRIS_SRC
	"${IRIS_CORE_SRC_DIR}/*.h"
	"${PROJECT_SOURCE_DIR}/src/*.h"
	"${PROJECT_SOURCE_DIR}/src/*.cpp"
	"${PROJECT_SOURCE_DIR}/src/*.inl"
)

FOREACH (FILE ${IRIS_SRC}) 
	GET_FILENAME_COMPONENT (PARENT_DIR "${FILE}" PATH)

	# skip src or include and changes /'s to \\'s
	STRING (REGEX REPLACE "(\\./)?(src|include)/?" "" GROUP "${PARENT_DIR}")
	STRING (REPLACE "/" "\\" GROUP "${GROUP}")
	STRING (REPLACE "${LOCAL_SOURCE_DIR}" "" GROUP "${GROUP}")
	SOURCE_GROUP ("${GROUP}" FILES "${FILE}")
ENDFOREACH (FILE ${IRIS_SRC})

IF (${USE_LUA_VERSION} STREQUAL "Builtin")
	SET (ENABLE_LUA_BUILTIN ON)
ELSE (${USE_LUA_VERSION} STREQUAL "Builtin")
	SET (ENABLE_LUA_BUILTIN OFF)
ENDIF (${USE_LUA_VERSION} STREQUAL "Builtin")

IF (ENABLE_LUA_BUILTIN)
	MESSAGE (STATUS "Use built-in lua version.")
	# BUILD LUA
	FILE (GLOB_RECURSE LUA_EXE_SRC
		"${IRIS_CORE_SRC_DIR}/lua/src/lauxlib.h"
		"${IRIS_CORE_SRC_DIR}/lua/src/lua.h"
		"${IRIS_CORE_SRC_DIR}/lua/src/luaconf.h"
		"${IRIS_CORE_SRC_DIR}/lua/src/lualib.h"
		"${IRIS_CORE_SRC_DIR}/lua/src/lua.c"
	)
	
	FILE (GLOB_RECURSE LUA_LIB_SRC
		"${IRIS_CORE_SRC_DIR}/lua/src/*.h"
		"${IRIS_CORE_SRC_DIR}/lua/src/*.hpp"
		"${IRIS_CORE_SRC_DIR}/lua/src/*.c"
	)
	
	LIST (REMOVE_ITEM LUA_LIB_SRC "${IRIS_CORE_SRC_DIR}/lua/src/luac.c")
	LIST (REMOVE_ITEM LUA_LIB_SRC "${IRIS_CORE_SRC_DIR}/lua/src/lua.c")

	IF (BUILD_STANDALONE)
		IF (NOT BUILD_MONOLITHIC)
			MESSAGE (FATAL_ERROR "Standalone build requries BUILD_MONOLITHIC=ON")
		ENDIF (NOT BUILD_MONOLITHIC)
		ADD_LIBRARY (lua54 STATIC ${LUA_LIB_SRC})
	ELSE (BUILD_STANDALONE)
		ADD_LIBRARY (lua54 SHARED ${LUA_LIB_SRC})
		IF (MSVC)
			TARGET_COMPILE_DEFINITIONS (lua54 PRIVATE LUA_BUILD_AS_DLL)
		ELSE (MSVC)
			TARGET_COMPILE_DEFINITIONS (lua54 PRIVATE LUA_USE_LINUX)
			TARGET_LINK_LIBRARIES (lua54 m dl)
		ENDIF (MSVC)
		
		ADD_EXECUTABLE (lua ${LUA_EXE_SRC})
		TARGET_LINK_LIBRARIES (lua lua54)
	ENDIF (BUILD_STANDALONE)

	SET (LUA_CORE_LIB lua54)
	FILE (GLOB_RECURSE LUA_CORE_SRC
		"${IRIS_CORE_SRC_DIR}/lua/src/lauxlib.h"
		"${IRIS_CORE_SRC_DIR}/lua/src/lua.h"
		"${IRIS_CORE_SRC_DIR}/lua/src/lua.hpp"
		"${IRIS_CORE_SRC_DIR}/lua/src/luaconf.h"
		"${IRIS_CORE_SRC_DIR}/lua/src/lualib.h"
	)

	FOREACH (FILE ${LUA_CORE_SRC}) 
		GET_FILENAME_COMPONENT(PARENT_DIR "${FILE}" PATH)
	
		# skip src or include and changes /'s to \\'s
		STRING (REGEX REPLACE "(\\./)?(src|include)/?" "" GROUP "${PARENT_DIR}")
		STRING (REPLACE "/" "\\" GROUP "${GROUP}")
		STRING (REPLACE "${LOCAL_SOURCE_DIR}" "" GROUP "${GROUP}")
		SOURCE_GROUP ("${GROUP}" FILES "${FILE}")
	ENDFOREACH (FILE ${LUA_CORE_SRC})
ELSE (ENABLE_LUA_BUILTIN)
	IF (${USE_LUA_VERSION} STREQUAL "LuaJIT")
		FIND_PACKAGE (LuaJIT)
	ELSE (${USE_LUA_VERSION} STREQUAL "LuaJIT")
		FIND_PACKAGE (Lua ${USE_LUA_VERSION} REQUIRED)
	ENDIF (${USE_LUA_VERSION} STREQUAL "LuaJIT")

	IF (LUA_FOUND OR LUAJIT_FOUND)
		MESSAGE (STATUS "Find lua version " ${USE_LUA_VERSION})
		FILE (GLOB_RECURSE LUA_CORE_SRC
			"${LUA_INCLUDE_DIR}/*.h"
			"${LUA_INCLUDE_DIR}/*.hpp"
		)

		SET (LUA_CORE_LIB ${LUA_LIBRARIES})
	ELSE (LUA_FOUND OR LUAJIT_FOUND)
		MESSAGE (FATAL_ERROR "Unable to find lua with version " ${USE_LUA_VERSION})
	ENDIF (LUA_FOUND OR LUAJIT_FOUND)
ENDIF (ENABLE_LUA_BUILTIN)

SET (IRIS_LIBNAME lua_co_await)
IF (BUILD_MONOLITHIC)
	SET (IRIS_CORE_LIBNAME lua_co_await_static)
	LIST (REMOVE_ITEM IRIS_SRC "${PROJECT_SOURCE_DIR}/src/main.cpp")
	ADD_LIBRARY (${IRIS_CORE_LIBNAME} STATIC ${IRIS_SRC} ${LUA_CORE_SRC})
	TARGET_COMPILE_DEFINITIONS (${IRIS_CORE_LIBNAME} PUBLIC IRIS_MONOLITHIC)
ELSE (BUILD_MONOLITHIC)
	SET (IRIS_CORE_LIBNAME lua_co_await)
	ADD_LIBRARY (${IRIS_CORE_LIBNAME} SHARED ${IRIS_SRC} ${LUA_CORE_SRC})
	TARGET_COMPILE_DEFINITIONS (${IRIS_CORE_LIBNAME} PUBLIC IRIS_IMPORT)
	TARGET_COMPILE_DEFINITIONS (${IRIS_CORE_LIBNAME} PRIVATE IRIS_EXPORT)
ENDIF (BUILD_MONOLITHIC)

IF (MSVC)
	# Default to statically-linked runtime.
	IF (BUILD_STANDALONE)
		SET (MSVC_RUNTIME "static")
	ELSE (BUILD_STANDALONE)
		SET (MSVC_RUNTIME "dynamic")
	ENDIF (BUILD_STANDALONE)
	# SET compiler options.
	SET (variables
		CMAKE_C_FLAGS_DEBUG
		CMAKE_C_FLAGS_MINSIZEREL
		CMAKE_C_FLAGS_RELEASE
		CMAKE_C_FLAGS_RELWITHDEBINFO
		CMAKE_CXX_FLAGS_DEBUG
		CMAKE_CXX_FLAGS_MINSIZEREL
		CMAKE_CXX_FLAGS_RELEASE
		CMAKE_CXX_FLAGS_RELWITHDEBINFO
	)

	# ADD_DEFINITIONS (-D_HAS_EXCEPTIONS=0)
	IF (${MSVC_RUNTIME} STREQUAL "static")
		# disable RTTI & Exceptions
		SET (CMAKE_CXX_FLAGS, "/DWIN32 /D_WINDOWS /W3")
		MESSAGE (STATUS
			"MSVC -> forcing use of statically-linked runtime."
		)
		FOREACH (variable ${variables})
			IF (${variable} MATCHES "/MD")
				string(REGEX REPLACE "/MD" "/MT" ${variable} "${${variable}}")
			ENDIF ()
		ENDFOREACH ()
	ELSE ()
		MESSAGE(STATUS
			"MSVC -> forcing use of dynamically-linked runtime."
		)
		FOREACH (variable ${variables})
			IF (${variable} MATCHES "/MT")
				string(REGEX REPLACE "/MT" "/MD" ${variable} "${${variable}}")
			ENDIF ()
		ENDFOREACH ()
	ENDIF ()
ENDIF ()

IF (NOT ENABLE_LUA_BUILTIN)
	TARGET_INCLUDE_DIRECTORIES (${IRIS_CORE_LIBNAME} PUBLIC "${LUA_INCLUDE_DIR}")
	TARGET_COMPILE_DEFINITIONS (${IRIS_CORE_LIBNAME} PUBLIC USE_LUA_LIBRARY)
ENDIF (NOT ENABLE_LUA_BUILTIN)

IF (MSVC)
	IF (NOT BUILD_MONOLITHIC)
		TARGET_COMPILE_DEFINITIONS(${IRIS_CORE_LIBNAME} PRIVATE LUA_BUILD_AS_DLL)
	ENDIF ()
	TARGET_LINK_LIBRARIES (${IRIS_CORE_LIBNAME} ${LUA_CORE_LIB})
ELSE (MSVC)
	TARGET_LINK_LIBRARIES (${IRIS_CORE_LIBNAME} m dl stdc++ pthread ${LUA_CORE_LIB})
ENDIF (MSVC)

# Plugins

SET (IRIS_PLUGINS)
SET (IRIS_PLUGINS_INL_FILE "${PROJECT_BINARY_DIR}/plugins.inl")
SET (IRIS_PLUGINS_INL_DECLARE "")
SET (IRIS_PLUGINS_INL_REGISTER "\nstatic void iris_register_plugins(lua_State* L) {\n")

MACRO (ADD_PLUGIN PLUGIN_NAME)
	ADD_SUBDIRECTORY_WITH_FOLDER ("plugin" plugin/${PLUGIN_NAME})
	SET (IRIS_PLUGINS ${IRIS_PLUGINS} ${PLUGIN_NAME})
	IF (BUILD_MONOLITHIC)
		STRING (APPEND IRIS_PLUGINS_INL_DECLARE "extern \"C\" int luaopen_${PLUGIN_NAME}(lua_State* L)\;\n")
		STRING (APPEND IRIS_PLUGINS_INL_REGISTER "\tluaL_requiref(L, \"${PLUGIN_NAME}\", luaopen_${PLUGIN_NAME}, 1)\;\tlua_pop(L, 1)\;\n")
	ENDIF (BUILD_MONOLITHIC)
ENDMACRO ()

OPTION (ENABLE_UTIL "Enable Util" ON)
IF (ENABLE_UTIL)
	ADD_PLUGIN (util)
ENDIF (ENABLE_UTIL)

STRING (APPEND IRIS_PLUGINS_INL_REGISTER "}\n")
FILE (WRITE ${IRIS_PLUGINS_INL_FILE} ${IRIS_PLUGINS_INL_DECLARE})
FILE (APPEND ${IRIS_PLUGINS_INL_FILE} ${IRIS_PLUGINS_INL_REGISTER})

IF (BUILD_MONOLITHIC)
	ADD_LIBRARY (${IRIS_LIBNAME} SHARED "${PROJECT_SOURCE_DIR}/src/main.cpp" ${IRIS_PLUGINS_INL_FILE})
	TARGET_LINK_LIBRARIES (${IRIS_LIBNAME} ${IRIS_CORE_LIBNAME} ${IRIS_PLUGINS})
	TARGET_COMPILE_DEFINITIONS (${IRIS_LIBNAME} PUBLIC IRIS_IMPORT)
	TARGET_COMPILE_DEFINITIONS (${IRIS_LIBNAME} PRIVATE IRIS_EXPORT)
ENDIF (BUILD_MONOLITHIC)
