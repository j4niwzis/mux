# Plugins, each in a folder of its own, src/plugins/<name>/, with its own
# CMakeLists.txt -- found by that alone: a new folder is a new plugin, with no
# list to add it to. Built where its option, MUX_PLUGIN_<NAME>, is on; left
# out whole where it is off. Each folder names its sources by layer through
# mux_plugin(); the modules that gather every plugin's -- mux.plugins.* --
# and the list of them, mux::enabled_plugins, are written from those turned
# on. What a plugin extends and how is C++'s (mux.composition): nothing else
# in the build names a plugin.

file(GLOB mux_plugin_folders LIST_DIRECTORIES true RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/src/plugins"
     CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/*/CMakeLists.txt")
list(TRANSFORM mux_plugin_folders REPLACE "/CMakeLists.txt$" "")

# A plugin's sources, by where they go:
#   CHANGES   what it tells the program from an account -- its changes,
#             one change_list, `changes`, in its namespace: under mux.core,
#             on mux.core.ids alone; the module mux.plugin.<name>.changes
#   TYPES     what it names -- its feature, pages, requests, what is shown of
#             it -- and what it says it provides: light, seen by mux.ui;
#             the module mux.plugin.<name>.types
#   UI        what it puts in the window: mux.plugin.<name>.ui, on mux.ui.base
#   UI_TOP    what it shows of messages: mux.plugin.<name>.ui_top, on
#             mux.ui.messages
#   APP       its part of the program: mux.plugin.<name>.app
#   MATRIX    what it hears of a Matrix account's events, and sends: seen
#             where the account is made (mux.plugins.matrix); built where
#             MUX_PROTOCOL_MATRIX is on -- mux.plugin.<name>.matrix
#   ORDER     its place among the plugins (where they come in turn: tabs,
#             parts), then by name
#   REQUIRES  the plugins it needs, turned on too
#   LIBRARIES what it links -- a call's WebRTC -- linked with the window
function(mux_plugin name)
  cmake_parse_arguments(PARSE_ARGV 1 one "" "ORDER" "CHANGES;TYPES;UI;UI_TOP;APP;MATRIX;REQUIRES;LIBRARIES")
  if(NOT DEFINED one_ORDER)
    message(FATAL_ERROR "src/plugins/${name}/CMakeLists.txt: mux_plugin(${name}) needs its ORDER.")
  endif()
  if(NOT MUX_PROTOCOL_MATRIX)
    set(one_MATRIX)
  endif()
  if(one_CHANGES)
    set_property(GLOBAL APPEND PROPERTY MUX_PLUGINS_WITH_CHANGES ${name})
  endif()
  foreach(layer IN ITEMS CHANGES TYPES UI UI_TOP APP MATRIX)
    list(TRANSFORM one_${layer} PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
    set_property(GLOBAL APPEND PROPERTY MUX_PLUGIN_${layer} ${one_${layer}})
    if(one_${layer})
      string(TOLOWER "${layer}" lower)
      set_property(GLOBAL APPEND PROPERTY MUX_PLUGIN_MODULES_${layer} mux.plugin.${name}.${lower})
    endif()
  endforeach()
  set_property(GLOBAL PROPERTY MUX_PLUGIN_REQUIRES_${name} ${one_REQUIRES})
  set_property(GLOBAL APPEND PROPERTY MUX_PLUGIN_LIBRARIES ${one_LIBRARIES})
  set_property(GLOBAL APPEND PROPERTY MUX_PLUGINS_RANKED "${one_ORDER}:${name}")
endfunction()

foreach(name IN LISTS mux_plugin_folders)
  string(TOUPPER "${name}" upper)
  option(MUX_PLUGIN_${upper} "Build the ${name} plugin" ON)
  if(MUX_PLUGIN_${upper})
    add_subdirectory(src/plugins/${name})
  endif()
endforeach()
get_property(mux_plugins GLOBAL PROPERTY MUX_PLUGINS_RANKED)
list(SORT mux_plugins COMPARE NATURAL)
list(TRANSFORM mux_plugins REPLACE "^[^:]*:" "")
# What a plugin needs, turned on: said here, not deep in a template.
foreach(name IN LISTS mux_plugins)
  get_property(needs GLOBAL PROPERTY MUX_PLUGIN_REQUIRES_${name})
  foreach(needed IN LISTS needs)
    if(NOT needed IN_LIST mux_plugins)
      string(TOUPPER "${name}" upper)
      string(TOUPPER "${needed}" needed_upper)
      message(FATAL_ERROR "The ${name} plugin needs the ${needed} plugin: turn on MUX_PLUGIN_${needed_upper}, "
                          "or turn off MUX_PLUGIN_${upper}.")
    endif()
  endforeach()
endforeach()
# Every plugin found turned on: the tests that try them with the rest are
# built only then (test/CMakeLists.txt).
list(LENGTH mux_plugins mux_plugins_on)
list(LENGTH mux_plugin_folders mux_plugins_found)
if(mux_plugins_on EQUAL mux_plugins_found)
  set(mux_all_plugins ON)
else()
  set(mux_all_plugins OFF)
endif()
get_property(mux_plugin_libraries GLOBAL PROPERTY MUX_PLUGIN_LIBRARIES)
foreach(layer IN ITEMS CHANGES TYPES UI UI_TOP APP MATRIX)
  string(TOLOWER "${layer}" lower)
  get_property(mux_plugin_${lower} GLOBAL PROPERTY MUX_PLUGIN_${layer})
  get_property(mux_plugin_modules_${lower} GLOBAL PROPERTY MUX_PLUGIN_MODULES_${layer})
endforeach()

# The modules gathering a layer of every plugin turned on, and the list.
function(mux_plugins_module out module modules extra)
  set(text "// Written by CMake from the plugins turned on (cmake/plugins.cmake).\nexport module ${module};\n")
  list(REMOVE_ITEM modules "")
  foreach(one IN LISTS modules)
    string(APPEND text "export import ${one};\n")
  endforeach()
  string(APPEND text "${extra}")
  set(file "${mux_generated}/plugins/${module}.cc")
  file(CONFIGURE OUTPUT "${file}" CONTENT "${text}" @ONLY)
  set(${out} "${file}" PARENT_SCOPE)
endfunction()
set(features "${mux_plugins}")
list(TRANSFORM features PREPEND "plugins::")
list(TRANSFORM features APPEND "::feature")
string(REPLACE ";" ", " features "${features}")
mux_plugins_module(mux_plugins_types mux.plugins.types "mux.composition;${mux_plugin_modules_types}"
  "export namespace mux {\n// The plugins turned on, in their ORDER.\nusing enabled_plugins = plugin_list<${features}>;\n}  // namespace mux\n")
mux_plugins_module(mux_plugins_ui mux.plugins.ui "mux.plugins.types;${mux_plugin_modules_ui}" "")
# What plugins show above the window's messages (a reaction's events, as
# bubbles): on mux.ui.messages, under the window.
mux_plugins_module(mux_plugins_ui_top mux.plugins.ui.top "mux.plugins.ui;${mux_plugin_modules_ui_top}" "")
mux_plugins_module(mux_plugins_app mux.plugins.app "mux.plugins.types;${mux_plugin_modules_app}" "")
# The plugins' changes, in their ORDER: one list, joined into mux::change_t.
get_property(mux_plugins_with_changes GLOBAL PROPERTY MUX_PLUGINS_WITH_CHANGES)
set(change_lists)
foreach(name IN LISTS mux_plugins)
  if(name IN_LIST mux_plugins_with_changes)
    list(APPEND change_lists "plugins::${name}::changes")
  endif()
endforeach()
string(REPLACE ";" ", " change_lists "${change_lists}")
mux_plugins_module(mux_plugins_changes mux.plugins.changes "mux.core.ids;${mux_plugin_modules_changes}"
  "export namespace mux {\n// Every plugin's own changes, one list: joined into mux::change_t (mux.core).\ntemplate <class... Lists>\nstruct joined_change_lists {\n  using type = change_list<>;\n};\ntemplate <class... A>\nstruct joined_change_lists<change_list<A...>> {\n  using type = change_list<A...>;\n};\ntemplate <class... A, class... B, class... Rest>\nstruct joined_change_lists<change_list<A...>, change_list<B...>, Rest...>\n    : joined_change_lists<change_list<A..., B...>, Rest...> {};\nusing plugin_changes = typename joined_change_lists<${change_lists}>::type;\n}  // namespace mux\n")
# What the plugins hear of a Matrix account's events: imported where the
# account is made, so that it finds them (mux.app.network).
mux_plugins_module(mux_plugins_matrix mux.plugins.matrix "${mux_plugin_modules_matrix}" "")
