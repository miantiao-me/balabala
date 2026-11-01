idf_component_register(
  SRCS "main.c" "balabala_input.c" "balabala_input_idf.c" "balabala_storage.c" "balabala_display.c" "balabala_runtime.c" "balabala_network.c" "balabala_network_idf.c"
  INCLUDE_DIRS "."
  REQUIRES heap esp_system esp_pm efuse esp_adc esp_driver_gpio esp_timer esp_driver_spi esp_driver_sdspi fatfs nvs_flash esp_netif esp_event esp_wifi network_provisioning json_generator
)
if(CONFIG_BALABALA_AUTOTEST)
  if(NOT BALABALA_AUTOTEST_RUST)
    message(FATAL_ERROR "autotest requires matching Rust feature archive")
  endif()
  target_sources(${COMPONENT_LIB} PRIVATE "balabala_autotest.c")
  if(CONFIG_BALABALA_AUTOTEST_PHASE EQUAL 2)
    target_sources(${COMPONENT_LIB} PRIVATE "balabala_alloc_diag.c")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BALABALA_AUTOTEST_PHASE2=1)
  endif()
elseif(BALABALA_AUTOTEST_RUST)
  message(FATAL_ERROR "autotest Rust archive requires matching Kconfig")
endif()
if(NOT RUST_ARCHIVE)
  set(RUST_ARCHIVE "${CMAKE_CURRENT_LIST_DIR}/../../.build/rust-target/riscv32imc-unknown-none-elf/release/libbalabala_probe.a")
endif()
add_prebuilt_library(balabala_native "${RUST_ARCHIVE}" REQUIRES heap esp_system)
target_link_libraries(${COMPONENT_LIB} PRIVATE balabala_native)

# 将公开应用入口纳入实际C3链接。
foreach(api create destroy key status storage_status storage_error render display_complete import_deck load save_export save_ack platform_requests network_status network_info network_shutdown_blocked download_ready download_failed)
  target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=balabala_app_${api}")
endforeach()

# 保留公开平台服务入口，便于独立集成/检查最终符号。
foreach(api input_init input_decode input_sample input_to_app input_idf_init input_idf_poll input_idf_deinit storage_init storage_import_decks storage_show_import_error storage_restore storage_save)
  target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=balabala_${api}")
endforeach()

# 人工清理不由产品调度调用，仍须保留最终固件中的主线程诊断入口。
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=balabala_network_retry_cleanup")
