add_executable(usb_msc
)

target_sources(usb_msc PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/main.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.c
        ${CMAKE_CURRENT_LIST_DIR}/msc_disk.c
        ${CMAKE_CURRENT_LIST_DIR}/fpga_program.c
        ${CMAKE_CURRENT_LIST_DIR}/fat_util.c
        ${CMAKE_CURRENT_LIST_DIR}/flash_util.c
        )

target_include_directories(usb_msc PUBLIC
        ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(usb_msc PUBLIC pico_stdlib pico_unique_id tinyusb_device tinyusb_board hardware_spi hardware_dma)

pico_add_extra_outputs(usb_msc)
