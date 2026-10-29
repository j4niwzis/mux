# The real client, with its protocol state and account stores, on the DOM.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set_target_properties(mux PROPERTIES OUTPUT_NAME mux SUFFIX .js)
set(mux_browser_exports _main _malloc _free _skiff_web_event _skiff_browser_pointer _skiff_browser_input _skiff_browser_key _skiff_browser_dialog _skiff_browser_selection _skiff_browser_file _skiff_browser_file_error)
if(MUX_PLUGIN_CALLS AND MUX_CALLS)
  list(APPEND mux_browser_exports _mux_web_call_event)
endif()
list(JOIN mux_browser_exports "," mux_browser_exports)
target_link_options(mux PRIVATE
  -fexceptions -pthread -sASYNCIFY -sASYNCIFY_STACK_SIZE=2097152
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 -sMAXIMUM_MEMORY=2147483648
  -sSTACK_SIZE=8388608 -sPTHREAD_POOL_SIZE=12 -sEXIT_RUNTIME=1
  -sFORCE_FILESYSTEM=1 -sFETCH -lidbfs.js
  "-sEXPORTED_RUNTIME_METHODS=ccall,UTF8ToString"
  "-sEXPORTED_FUNCTIONS=${mux_browser_exports}")
set(mux_web_dir "${CMAKE_CURRENT_BINARY_DIR}/web")
add_custom_command(TARGET mux POST_BUILD
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/package_web.py"
    --build "${CMAKE_CURRENT_BINARY_DIR}" --output "${mux_web_dir}"
  VERBATIM)
