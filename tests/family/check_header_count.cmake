# 4.2 check 4: the public header count is pinned.
#   cmake -DENGINE=<engine> -DINCLUDE=<dir> -DEXPECT=<n> -P this
if(NOT DEFINED INCLUDE OR NOT DEFINED EXPECT OR NOT DEFINED ENGINE)
    message(FATAL_ERROR "ENGINE, INCLUDE and EXPECT are required")
endif()
file(GLOB_RECURSE _files ${INCLUDE}/*.h)
list(LENGTH _files _n)
if(NOT _n EQUAL EXPECT)
    list(JOIN _files "\n  " _files)
    message(FATAL_ERROR "tap::sr::${ENGINE} has ${_n} public header(s), pinned ${EXPECT}; "
                        "re-pin in tests/CMakeLists.txt if the change is intended:\n  ${_files}")
endif()
message(STATUS "tap::sr::${ENGINE}: ${_n} public header(s), as pinned")
