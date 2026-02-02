cmake_minimum_required(VERSION 3.10)
project(CustomAllocator C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(custom_alloc 
    main.c 
    custom_alloc.h
    custom_alloc.c
    )

target_compile_definitions(custom_alloc
    PRIVATE
        $<$<CONFIG:Debug>:CUSTOM_ALLOC_DEBUG>    
)

if(SMALL_POOL_BLOCKS)
    add_compile_definitions(SMALL_POOL_BLOCKS=${SMALL_POOL_BLOCKS})
endif()

if(LARGE_POOL_BLOCKS)
    add_compile_definitions(LARGE_POOL_BLOCKS=${LARGE_POOL_BLOCKS})
endif()