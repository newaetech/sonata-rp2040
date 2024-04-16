add_executable(usb_msc
)

target_sources(usb_msc PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/main.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.c
        ${CMAKE_CURRENT_LIST_DIR}/msc_disk.c
        ${CMAKE_CURRENT_LIST_DIR}/fpga_program.c
        ${CMAKE_CURRENT_LIST_DIR}/fat_util.c
        ${CMAKE_CURRENT_LIST_DIR}/flash_util.c
        ${CMAKE_CURRENT_LIST_DIR}/config.c
        ${CMAKE_CURRENT_LIST_DIR}/error.c
        ${CMAKE_CURRENT_LIST_DIR}/crc32.c
        ${CMAKE_CURRENT_LIST_DIR}/tests.c
        )

target_include_directories(usb_msc PUBLIC
        ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(usb_msc PUBLIC pico_stdlib pico_unique_id tinyusb_device tinyusb_board hardware_spi hardware_dma)

pico_add_extra_outputs(usb_msc)

option(TESTING_BUILD "Do test build" OFF)

# option(DVjjEBUG_LEVEL)

# if ((NOT DEFINED DEBUG_LEVEL))
#         set(DEBUG_LEVEL "1")
#         message("Test set debug")
# endif()

# set(DEBUG_LEVEL "1")

if (NOT "${DEBUG_LEVEL}")
        set(DEBUG_LEVEL "1")
endif()

if (NOT ("${DEBUG_LEVEL}" MATCHES "^(0|1|2|3|4|5)$"))
        message("Invalid debug level ${DEBUG_LEVEL}, setting to 1")
        set(DEBUG_LEVEL "1")
endif()

if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
        message("Building as Debug with level ${DEBUG_LEVEL}" )
        add_compile_definitions("DEBUG_LEVEL=${DEBUG_LEVEL}")
endif()

if (TESTING_BUILD)
        message("Doing test build" )
        # add_compile_definitions("DEBUG_LEVEL=${DEBUG_LEVEL}")
        add_compile_definitions("TESTING_BUILD=1")
endif()

# The additional / is important to remove the last character from the path.
# Note that it does not matter if the OS uses / or \, because we are only
# saving the path size.
string(LENGTH "${CMAKE_SOURCE_DIR}/" SOURCE_PATH_SIZE)
add_definitions("-DSOURCE_PATH_SIZE=${SOURCE_PATH_SIZE}")