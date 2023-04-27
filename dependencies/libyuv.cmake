include(FetchContent)

find_package(libyuv QUIET)

if (NOT LIBYUV_FOUND)
    # try pkgconfig just to be sure
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_search_module(LIBYUV yuv libyuv)
    endif()
endif()

if (LIBYUV_FOUND)
    message(STATUS "Using system version of libyuv")
else()
    find_package(Git REQUIRED)

    message(STATUS "Fetching and building libyuv")

    # libyuv
    FetchContent_Declare(
            yuv
            GIT_REPOSITORY https://github.com/jrsnen/libyuv.git
            GIT_TAG        3e10a91ba61d175109a19f282cb9bca756c057e8
    )

    set(TEST OFF CACHE BOOL "" FORCE)
    set(LIBYUV_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(yuv)

    unset(TEST)
    unset(LIBYUV_INSTALL)

    include_directories(${yuv_SOURCE_DIR}/include)
endif()
