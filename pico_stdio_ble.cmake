function(pico_enable_stdio_ble TARGET)
    target_link_libraries(${TARGET} PRIVATE pico_stdio_ble)
endfunction()
