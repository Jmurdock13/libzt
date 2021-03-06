if(CMAKE_COMPILER_IS_GNUCC)
    SET(RSN_BASE_C_FLAGS "-std=c99 -pedantic -Wall -Wextra -Wstrict-prototypes -Wno-unused-parameter")
    SET(RSN_BASE_CXX_FLAGS "-Wall")

    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${RSN_BASE_C_FLAGS} -DLUA_USE_MKSTEMP")
    SET(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${RSN_BASE_C_FLAGS}")
    SET(CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL} ${RSN_BASE_C_FLAGS}")
    SET(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${RSN_BASE_C_FLAGS}")
    SET(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} ${RSN_BASE_C_FLAGS}")

    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${RSN_BASE_CXX_FLAGS}")
    SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${RSN_BASE_CXX_FLAGS}")
    SET(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} ${RSN_BASE_CXX_FLAGS}")
    SET(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${RSN_BASE_CXX_FLAGS}")
    SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} ${RSN_BASE_CXX_FLAGS}")

    # set(CMAKE_BUILD_TYPE distribution)
    # set(CMAKE_C_FLAGS_DISTRIBUTION "-Wall -Wextra -O2")
    if (UNIX)
        if(APPLE)
            set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wshorten-64-to-32 -D_BSD_SOURCE")
        endif(APPLE)
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
    endif(UNIX)

    if (${CMAKE_BUILD_TYPE} STREQUAL Debug)
        SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DDEBUG -g3")
    ENDIF(${CMAKE_BUILD_TYPE} STREQUAL Debug)

endif(CMAKE_COMPILER_IS_GNUCC)

IF(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
  # SET(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
      # "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
      # FORCE)
ENDIF(NOT CMAKE_BUILD_TYPE)
