CMAKE_MINIMUM_REQUIRED (VERSION 3.0)
PROJECT (iris_demo)

######

SET (STDLIB stdc++)

ENABLE_TESTING()

OPTION (ENABLE_COROUTINE "Enable Iris Coroutine support" ON)
OPTION (ENABLE_LUA "Enable Lua Binding support" ON)

SET (USE_LUA_VERSION "Builtin" CACHE STRING "Lua version")
SET_PROPERTY (CACHE USE_LUA_VERSION PROPERTY STRINGS "Builtin" "LuaJIT" "5.1" "5.2" "5.3" "5.4")
SET (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

IF (ENABLE_COROUTINE)
	SET (CMAKE_CXX_STANDARD 20)
ELSE (ENABLE_COROUTINE)
	IF (ENABLE_LUA)
		SET (CMAKE_CXX_STANDARD 17)
	ELSE (ENABLE_LUA)
		SET (CMAKE_CXX_STANDARD 11)
	ENDIF (ENABLE_LUA)
ENDIF (ENABLE_COROUTINE)

INCLUDE_DIRECTORIES ("${PROJECT_BINARY_DIR}")
STRING (REPLACE "/" "\\" BUILTIN_SOURCE_DIR "${PROJECT_SOURCE_DIR}")

FILE (GLOB_RECURSE IRIS_DISPATCHER_DEMO_SRC
	"${PROJECT_SOURCE_DIR}/src/iris_common.h"
	"${PROJECT_SOURCE_DIR}/src/iris_common.inl"
	"${PROJECT_SOURCE_DIR}/src/iris_dispatcher.h"
	"${PROJECT_SOURCE_DIR}/test/iris_dispatcher_demo.cpp"
)

ADD_EXECUTABLE (iris_dispatcher_demo ${IRIS_DISPATCHER_DEMO_SRC})
ADD_TEST (test_dispatcher iris_dispatcher_demo)

IF (NOT MSVC)
	TARGET_LINK_LIBRARIES(iris_dispatcher_demo m ${STDLIB} pthread)
ENDIF (NOT MSVC)

FILE (GLOB_RECURSE IRIS_SYSTEM_DEMO_SRC
	"${PROJECT_SOURCE_DIR}/src/iris_common.h"
	"${PROJECT_SOURCE_DIR}/src/iris_common.inl"
	"${PROJECT_SOURCE_DIR}/src/iris_system.h"
	"${PROJECT_SOURCE_DIR}/test/iris_system_demo.cpp"
)

ADD_EXECUTABLE (iris_system_demo ${IRIS_SYSTEM_DEMO_SRC})
ADD_TEST (test_system iris_system_demo)

IF (NOT MSVC)
	TARGET_LINK_LIBRARIES(iris_system_demo m ${STDLIB} pthread)
ENDIF (NOT MSVC)

FILE (GLOB_RECURSE IRIS_TREE_DEMO_SRC
	"${PROJECT_SOURCE_DIR}/src/iris_common.h"
	"${PROJECT_SOURCE_DIR}/src/iris_common.inl"
	"${PROJECT_SOURCE_DIR}/src/iris_tree.h"
	"${PROJECT_SOURCE_DIR}/test/iris_tree_demo.cpp"
)

ADD_EXECUTABLE (iris_tree_demo ${IRIS_TREE_DEMO_SRC})
ADD_TEST (test_tree iris_tree_demo)

IF (NOT MSVC)
	TARGET_LINK_LIBRARIES(iris_tree_demo m ${STDLIB} pthread)
ENDIF (NOT MSVC)

FILE (GLOB_RECURSE IRIS_BUFFER_DEMO_SRC
	"${PROJECT_SOURCE_DIR}/src/iris_common.h"
	"${PROJECT_SOURCE_DIR}/src/iris_common.inl"
	"${PROJECT_SOURCE_DIR}/src/iris_buffer.h"
	"${PROJECT_SOURCE_DIR}/test/iris_buffer_demo.cpp"
)

ADD_EXECUTABLE (iris_buffer_demo ${IRIS_BUFFER_DEMO_SRC})
ADD_TEST (test_buffer iris_buffer_demo)

IF (NOT MSVC)
	TARGET_LINK_LIBRARIES(iris_buffer_demo m ${STDLIB} pthread)
ENDIF (NOT MSVC)

IF (ENABLE_COROUTINE)
	FILE (GLOB_RECURSE IRIS_COROUTINE_DEMO_SRC
		"${PROJECT_SOURCE_DIR}/src/iris_common.h"
		"${PROJECT_SOURCE_DIR}/src/iris_common.inl"
		"${PROJECT_SOURCE_DIR}/src/iris_dispatcher.h"
		"${PROJECT_SOURCE_DIR}/src/iris_coroutine.h"
		"${PROJECT_SOURCE_DIR}/test/iris_coroutine_demo.cpp"
	)
	
	ADD_EXECUTABLE (iris_coroutine_demo ${IRIS_COROUTINE_DEMO_SRC})
	ADD_TEST (test_coroutine iris_coroutine_demo)

	IF (NOT MSVC)
		TARGET_LINK_LIBRARIES(iris_coroutine_demo m ${STDLIB} pthread)
	ENDIF (NOT MSVC)

	FILE (GLOB_RECURSE IRIS_ENGINE_DEMO_SRC
		"${PROJECT_SOURCE_DIR}/src/iris_common.h"
		"${PROJECT_SOURCE_DIR}/src/iris_common.inl"
		"${PROJECT_SOURCE_DIR}/src/iris_dispatcher.h"
		"${PROJECT_SOURCE_DIR}/src/iris_coroutine.h"
		"${PROJECT_SOURCE_DIR}/test/iris_engine_demo.cpp"
	)
	
	ADD_EXECUTABLE (iris_engine_demo ${IRIS_ENGINE_DEMO_SRC})
	ADD_TEST (test_engine iris_engine_demo)

	IF (NOT MSVC)
		TARGET_LINK_LIBRARIES(iris_engine_demo m ${STDLIB} pthread)
	ENDIF (NOT MSVC)
ENDIF (ENABLE_COROUTINE)

IF (ENABLE_LUA)
	FILE (GLOB_RECURSE IRIS_LUA_DEMO_SRC
		"${PROJECT_SOURCE_DIR}/src/optional/iris_lua.h"
		"${PROJECT_SOURCE_DIR}/test/iris_lua_demo.cpp"
	)

	IF (${USE_LUA_VERSION} STREQUAL "Builtin")
		SET (ENABLE_LUA_BUILTIN ON)
	ELSE (${USE_LUA_VERSION} STREQUAL "Builtin")
		SET (ENABLE_LUA_BUILTIN OFF)
	ENDIF (${USE_LUA_VERSION} STREQUAL "Builtin")

	IF (ENABLE_LUA_BUILTIN)
		MESSAGE (STATUS "Use built-in lua version.")
		# default to lua 5.4
		FILE (GLOB_RECURSE IRIS_LUA_CORE_SRC
			"${PROJECT_SOURCE_DIR}/src/optional/lua/src/*.h"
			"${PROJECT_SOURCE_DIR}/src/optional/lua/src/*.hpp"
			"${PROJECT_SOURCE_DIR}/src/optional/lua/src/*.c"
		)

		LIST (REMOVE_ITEM IRIS_LUA_CORE_SRC "${PROJECT_SOURCE_DIR}/src/optional/lua/src/lua.c")
		LIST (REMOVE_ITEM IRIS_LUA_CORE_SRC "${PROJECT_SOURCE_DIR}/src/optional/lua/src/luac.c")
		SET (LUALIB)
	ELSE (ENABLE_LUA_BUILTIN)
		IF (${USE_LUA_VERSION} STREQUAL "LuaJIT")
			FIND_PACKAGE (LuaJIT)
		ELSE (${USE_LUA_VERSION} STREQUAL "LuaJIT")
			FIND_PACKAGE (Lua ${USE_LUA_VERSION} REQUIRED)
		ENDIF (${USE_LUA_VERSION} STREQUAL "LuaJIT")

		IF (LUA_FOUND OR LUAJIT_FOUND)
			MESSAGE (STATUS "Find lua version " ${USE_LUA_VERSION})
			FILE (GLOB_RECURSE IRIS_LUA_CORE_SRC
				"${LUA_INCLUDE_DIR}/*.h"
				"${LUA_INCLUDE_DIR}/*.hpp"
			)
	
			SET (LUALIB ${LUA_LIBRARIES})
		ELSE (LUA_FOUND OR LUAJIT_FOUND)
			MESSAGE (FATAL_ERROR "Unable to find lua with version " ${USE_LUA_VERSION})
		ENDIF (LUA_FOUND OR LUAJIT_FOUND)
	ENDIF (ENABLE_LUA_BUILTIN)

	ADD_EXECUTABLE (iris_lua_demo ${IRIS_LUA_DEMO_SRC} ${IRIS_LUA_CORE_SRC})

	IF (ENABLE_COROUTINE)
		TARGET_COMPILE_DEFINITIONS (iris_lua_demo PRIVATE USE_LUA_COROUTINE)
	ENDIF (ENABLE_COROUTINE)

	IF (NOT ENABLE_LUA_BUILTIN)
		TARGET_INCLUDE_DIRECTORIES (iris_lua_demo PUBLIC ${LUA_INCLUDE_DIR})
		TARGET_COMPILE_DEFINITIONS (iris_lua_demo PUBLIC USE_LUA_LIBRARY)
	ENDIF (NOT ENABLE_LUA_BUILTIN)
	
	ADD_TEST (test_lua iris_lua_demo)

	IF (MSVC)
		TARGET_LINK_LIBRARIES(iris_lua_demo ${LUALIB})
	ELSE (MSVC)
		TARGET_LINK_LIBRARIES(iris_lua_demo m ${STDLIB} ${LUALIB})
	ENDIF (MSVC)
ENDIF (ENABLE_LUA)
