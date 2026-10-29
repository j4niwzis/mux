# Protocols, each in a folder of its own, src/proto/<name>/, with its own
# CMakeLists.txt -- found by that alone: a new folder is a new protocol, with
# no list to add it to. Built where its option, MUX_PROTOCOL_<NAME>, is on;
# left out whole where it is off -- not a file of it compiled. Each folder names
# its sources by layer, through mux_protocol(); the targets read them from
# there, and the modules that gather every protocol's -- mux.proto.enabled.*
# -- are written from the protocols turned on (mux_protocols_composed()).
# Nothing else in the build names a protocol.

file(GLOB mux_protocol_folders LIST_DIRECTORIES true RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/src/proto"
     CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/proto/*/CMakeLists.txt")
list(TRANSFORM mux_protocol_folders REPLACE "/CMakeLists.txt$" "")
set(mux_generated "${CMAKE_BINARY_DIR}/generated")

# A protocol's sources, by where they go:
#   CONFIG  what the settings read: its tag's state, what it keeps (mux_config)
#   CORE    its client, changes, links, requests (mux_modules)
#   UI      its screen interfaces (mux_modules, where the window is built)
#   UI_IMPL their implementation units (private sources of mux_modules)
#   APP     its glue to the program (mux_app)
#   SPLIT   its accounts, each in a unit of its own outside a release build
#   POINTS  the modules mux.protocols re-exports: mux.proto.<name> unless said
#   ORDER   its place in the list -- protocol_t's alternatives follow it, and
#           the first is where an address no protocol owns goes -- then by name
function(mux_protocol name)
  cmake_parse_arguments(PARSE_ARGV 1 one "" "ORDER" "CONFIG;CORE;UI;UI_IMPL;APP;SPLIT;POINTS;LOOM")
  # The parts of loom it needs (mux_find_package(loom COMPONENTS ...)).
  set_property(GLOBAL APPEND PROPERTY MUX_LOOM_COMPONENTS ${one_LOOM})
  if(NOT DEFINED one_ORDER)
    message(FATAL_ERROR "src/proto/${name}/CMakeLists.txt: mux_protocol(${name}) needs its ORDER.")
  endif()
  foreach(layer IN ITEMS CONFIG CORE UI UI_IMPL APP SPLIT)
    list(TRANSFORM one_${layer} PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
    set_property(GLOBAL APPEND PROPERTY MUX_PROTOCOL_${layer} ${one_${layer}})
  endforeach()
  if(NOT one_POINTS)
    set(one_POINTS mux.proto.${name})
  endif()
  set_property(GLOBAL APPEND PROPERTY MUX_PROTOCOL_POINTS ${one_POINTS})
  # Ranked as text, sorted naturally: 10 before 20, then by name.
  set_property(GLOBAL APPEND PROPERTY MUX_PROTOCOLS_RANKED "${one_ORDER}:${name}")
endfunction()

# Each protocol found: its option, and its folder where that is on.
foreach(name IN LISTS mux_protocol_folders)
  string(TOUPPER "${name}" upper)
  option(MUX_PROTOCOL_${upper} "Build the ${name} protocol" ON)
  if(MUX_PROTOCOL_${upper})
    add_subdirectory(src/proto/${name})
  endif()
endforeach()
get_property(mux_protocols GLOBAL PROPERTY MUX_PROTOCOLS_RANKED)
list(SORT mux_protocols COMPARE NATURAL)
list(TRANSFORM mux_protocols REPLACE "^[^:]*:" "")
if(NOT mux_protocols)
  string(TOUPPER "${mux_protocol_folders}" all)
  list(TRANSFORM all PREPEND MUX_PROTOCOL_)
  string(REPLACE ";" ", " all "${all}")
  message(FATAL_ERROR "mux needs at least one protocol: turn on one of ${all}.")
endif()
# Every protocol found turned on: the tests that try them side by side are
# built only then (test/CMakeLists.txt).
list(LENGTH mux_protocols mux_protocols_on)
list(LENGTH mux_protocol_folders mux_protocols_found)
if(mux_protocols_on EQUAL mux_protocols_found)
  set(mux_all_protocols ON)
else()
  set(mux_all_protocols OFF)
endif()
foreach(layer IN ITEMS CONFIG CORE UI UI_IMPL APP SPLIT POINTS)
  string(TOLOWER "${layer}" lower)
  get_property(mux_protocol_${lower} GLOBAL PROPERTY MUX_PROTOCOL_${layer})
endforeach()

# A module re-exporting one module of each protocol turned on: `pattern`
# with <p> for the protocol's name. Written only where it changed, so a
# configure that changes nothing rebuilds nothing.
function(mux_enabled_module out module pattern)
  set(text "// Written by CMake from the protocols turned on (cmake/protocols.cmake).\nexport module ${module};\n")
  foreach(name IN LISTS mux_protocols)
    string(REPLACE "<p>" "${name}" one "${pattern}")
    string(APPEND text "export import ${one};\n")
  endforeach()
  set(file "${mux_generated}/proto/enabled/${module}.cc")
  file(CONFIGURE OUTPUT "${file}" CONTENT "${text}" @ONLY)
  set(${out} "${file}" PARENT_SCOPE)
endfunction()

# The protocol list -- what protocol_t is one of, in their ORDER -- and each protocol's tag by its name, beside the
# modules of their states.
set(mux_protocol_tags)
set(mux_protocol_names)
foreach(name IN LISTS mux_protocols)
  list(APPEND mux_protocol_tags "proto::id<proto::${name}::state>")
  string(APPEND mux_protocol_names "using ${name} = mux::proto::id<mux::proto::${name}::state>;\n")
endforeach()
string(REPLACE ";" ", " mux_protocol_tags "${mux_protocol_tags}")
mux_enabled_module(mux_enabled_state_imports mux.proto.enabled.state "mux.proto.<p>.state")
file(CONFIGURE OUTPUT "${mux_generated}/proto/enabled/mux.proto.enabled.list.cc" CONTENT
"// Written by CMake from the protocols turned on (cmake/protocols.cmake).
export module mux.proto.enabled.list;
export import mux.proto.identity;
export import mux.proto.enabled.state;
export namespace mux {
using protocols = protocol_list<@mux_protocol_tags@>;
namespace protocol {
@mux_protocol_names@}  // namespace protocol
}  // namespace mux
" @ONLY)

mux_enabled_module(mux_enabled_kept mux.proto.enabled.kept "mux.proto.<p>.kept")
mux_enabled_module(mux_enabled_changes mux.proto.enabled.changes "mux.proto.<p>.changes")
mux_enabled_module(mux_enabled_links mux.proto.enabled.links "mux.proto.<p>.links")
mux_enabled_module(mux_enabled_make mux.proto.enabled.make "mux.proto.<p>.make")
mux_enabled_module(mux_enabled_demo mux.proto.enabled.demo "mux.proto.<p>.demo")
mux_enabled_module(mux_enabled_ui mux.proto.enabled.ui "mux.ui.proto.<p>")
mux_enabled_module(mux_enabled_app mux.proto.enabled.app "mux.app.proto.<p>")
set(text "// Written by CMake from the protocols turned on (cmake/protocols.cmake).\nexport module mux.proto.enabled.points;\n")
foreach(one IN LISTS mux_protocol_points)
  string(APPEND text "export import ${one};\n")
endforeach()
file(CONFIGURE OUTPUT "${mux_generated}/proto/enabled/mux.proto.enabled.points.cc" CONTENT "${text}" @ONLY)

# Each protocol's account, instantiated for a sink: the imports of their
# clients and a line each, put into the units that make them
# (src/proto/accounts.cc.in, src/proto/cli.cc.in).
set(mux_account_imports "")
foreach(name IN LISTS mux_protocols)
  string(APPEND mux_account_imports "import mux.proto.${name}.client;\n")
endforeach()
function(mux_account_lines out format)
  set(text "")
  foreach(name IN LISTS mux_protocols)
    string(REPLACE "<p>" "${name}" one "${format}")
    string(APPEND text "${one}\n")
  endforeach()
  set(${out} "${text}" PARENT_SCOPE)
endfunction()
mux_account_lines(mux_accounts_extern "extern template class mux::proto::<p>::client::account<mux::app::post_change>;")
mux_account_lines(mux_accounts_made "template class mux::proto::<p>::client::account<mux::app::post_change>;")
mux_account_lines(mux_accounts_destroy_declared "void destroy_account(mux::proto::<p>::client::account_of<post_change>* one);")
mux_account_lines(mux_accounts_destroy_defined "void mux::app::destroy_account(mux::proto::<p>::client::account_of<post_change>* one) { delete one; }")
mux_account_lines(mux_cli_accounts "template class mux::proto::<p>::client::account<print_change>;")
configure_file(src/proto/accounts.cc.in "${mux_generated}/proto/accounts.cc" @ONLY)
configure_file(src/proto/cli.cc.in "${mux_generated}/proto/cli.cc" @ONLY)
set(mux_enabled_config ${mux_enabled_state_imports} "${mux_generated}/proto/enabled/mux.proto.enabled.list.cc" ${mux_enabled_kept})
set(mux_enabled_core ${mux_enabled_changes} ${mux_enabled_links} ${mux_enabled_make}
    "${mux_generated}/proto/enabled/mux.proto.enabled.points.cc")
