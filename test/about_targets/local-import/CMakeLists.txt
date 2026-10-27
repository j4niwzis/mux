# A system package discovered in a dependency directory remains local.
add_library(ICU::data INTERFACE IMPORTED)
add_library(local-user INTERFACE)
target_link_libraries(local-user INTERFACE ICU::data)
