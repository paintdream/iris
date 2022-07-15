CMAKE_MINIMUM_REQUIRED (VERSION 3.0)
PROJECT (iris_demo)

######

SET (STDLIB stdc++)

ENABLE_TESTING()

OPTION (ENABLE_COROUTINE "Enable Iris Coroutine support" ON)
OPTION (ENABLE_LUA "Enable Lua Binding support" ON)

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
STRING (REPLACE "/" "\\" LOCAL_SOURCE_DIR "${PROJECT_SOURCE_DIR}")

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
		"${PROJECT_SOURCE_DIR}/src/optional/lua/src/*.h"
		"${PROJECT_SOURCE_DIR}/src/optional/lua/src/*.hpp"
		"${PROJECT_SOURCE_DIR}/src/optional/lua/src/*.c"
		"${PROJECT_SOURCE_DIR}/test/iris_lua_demo.cpp"
	)

	LIST (REMOVE_ITEM IRIS_LUA_DEMO_SRC "${PROJECT_SOURCE_DIR}/src/optional/lua/src/lua.c")
	LIST (REMOVE_ITEM IRIS_LUA_DEMO_SRC "${PROJECT_SOURCE_DIR}/src/optional/lua/src/luac.c")

	ADD_EXECUTABLE (iris_lua_demo ${IRIS_LUA_DEMO_SRC})

	IF (ENABLE_COROUTINE)
		TARGET_COMPILE_DEFINITIONS(iris_lua_demo PRIVATE USE_LUA_COROUTINE)
	ENDIF (ENABLE_COROUTINE)
	
	ADD_TEST (test_lua iris_lua_demo)

	IF (NOT MSVC)
		TARGET_LINK_LIBRARIES(iris_lua_demo m ${STDLIB})
	ENDIF (NOT MSVC)
ENDIF (ENABLE_LUA)
