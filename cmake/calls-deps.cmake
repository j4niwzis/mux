# Source fallbacks for call builds, including Android target sysroots.
# Meson runs on the host; cmake-everywhere supplies its cross file.
cme_declare_port(NAME muxpcre2 PROVIDES PCRE2 libpcre2-8
  VERSION 10.44 GITHUB_REPOSITORY PCRE2Project/pcre2 GIT_TAG pcre2-10.44
  LICENSE BSD-3-Clause
  OPTIONS "PCRE2_BUILD_PCRE2_8 ON" "PCRE2_BUILD_PCRE2_16 OFF" "PCRE2_BUILD_PCRE2_32 OFF"
          "PCRE2_BUILD_TESTS OFF" "PCRE2_BUILD_PCRE2GREP OFF" "BUILD_SHARED_LIBS OFF"
  TARGETS pcre2-8 SYSTEM_PKGCONFIG "libpcre2-8:pcre2-8" LINK_NAMES "pcre2-8=pcre2-8")

cme_declare_port(NAME muxglib PROVIDES GLIB glib-2.0 gobject-2.0 gio-2.0 gmodule-2.0 gthread-2.0
  VERSION 2.80.0 GITHUB_REPOSITORY GNOME/glib GIT_TAG 2.80.0
  LICENSE LGPL-2.1-or-later IMPORT meson DEPENDS muxpcre2 libffi zlib
  IMPORT_TARGETS "glib-2.0=GLIB::glib" "gobject-2.0=GLIB::gobject" "gio-2.0=GLIB::gio"
                 "gmodule-2.0=GLIB::gmodule" "gthread-2.0=GLIB::gthread"
  SYSTEM_PKGCONFIG "glib-2.0:GLIB::glib" "gobject-2.0:GLIB::gobject" "gio-2.0:GLIB::gio"
                   "gmodule-2.0:GLIB::gmodule" "gthread-2.0:GLIB::gthread"
  LINK_NAMES "glib-2.0=GLIB::glib" "gobject-2.0=GLIB::gobject" "gio-2.0=GLIB::gio"
             "gmodule-2.0=GLIB::gmodule" "gthread-2.0=GLIB::gthread"
  TARGETS GLIB::glib GLIB::gobject GLIB::gio GLIB::gmodule GLIB::gthread CHECK_HEADER glib.h
  OPTIONS "tests false" "installed_tests false" "nls disabled"
          "libmount disabled" "selinux disabled" "xattr false" "documentation false"
          "introspection disabled" "sysprof disabled" "libelf disabled" "man-pages disabled")

function(cme_adapt_muxglib source binary)
  set(configured "${CMAKE_BINARY_DIR}/_cme/muxglib-probe")
  foreach(component glib gobject gio gmodule gthread)
    target_include_directories(muxglib_${component}-2.0 INTERFACE
      "$<BUILD_INTERFACE:${source}>" "$<BUILD_INTERFACE:${source}/glib>"
      "$<BUILD_INTERFACE:${source}/${component}>" "$<BUILD_INTERFACE:${configured}>"
      "$<BUILD_INTERFACE:${configured}/glib>")
  endforeach()
  set_property(GLOBAL PROPERTY MUX_GLIB_MKENUMS "${configured}/gobject/glib-mkenums")
  find_package(Threads REQUIRED)
  target_link_libraries(muxglib_glib-2.0 INTERFACE pcre2-8 Threads::Threads ${CMAKE_DL_LIBS})
  target_link_libraries(muxglib_gobject-2.0 INTERFACE GLIB::glib FFI::ffi)
  target_link_libraries(muxglib_gmodule-2.0 INTERFACE GLIB::glib ${CMAKE_DL_LIBS})
  target_link_libraries(muxglib_gthread-2.0 INTERFACE GLIB::glib Threads::Threads)
  target_link_libraries(muxglib_gio-2.0 INTERFACE GLIB::gobject GLIB::gmodule ZLIB::ZLIB)
endfunction()

cme_declare_port(NAME muxnice PROVIDES LibNice nice
  VERSION 0.1.23 GITHUB_REPOSITORY libnice/libnice GIT_TAG 0.1.23
  LICENSE MPL-1.1 IMPORT meson DEPENDS muxglib openssl
  PATCHES "${CMAKE_CURRENT_LIST_DIR}/patches/libnice-native-codegen.patch"
  IMPORT_TARGETS "nice=LibNice::LibNice" SYSTEM_PKGCONFIG "nice:LibNice::LibNice"
  LINK_NAMES "nice=LibNice::LibNice" TARGETS LibNice::LibNice CHECK_HEADER nice/agent.h
  OPTIONS "gupnp disabled" "gstreamer disabled" "examples disabled"
          "tests disabled" "gtk_doc disabled" "introspection disabled" "crypto-library openssl")

function(cme_adapt_muxnice source binary)
  cme_header_prefix(public nice "${source}/agent")
  target_include_directories(muxnice_nice INTERFACE
    "$<BUILD_INTERFACE:${public}>" "$<BUILD_INTERFACE:${source}>" "$<BUILD_INTERFACE:${source}/nice>"
    "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/_cme/muxnice-probe>"
    "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/_cme/muxnice-probe/agent>")
  target_link_libraries(muxnice_nice INTERFACE GLIB::gio GLIB::gthread OpenSSL::SSL OpenSSL::Crypto)
  cme_export_variable(LibNice LibNice_FOUND TRUE)
endfunction()

# GNOME's Meson module needs a host code generator, not an executable
# compiled for the Android target. The source GLib configures this Python
# tool at setup time; installed GLib supplies it through pkg-config.
function(cme_prepare_muxnice source)
  get_property(codegen GLOBAL PROPERTY MUX_GLIB_MKENUMS)
  set(options ${CME_OPTIONS_muxnice})
  list(FILTER options EXCLUDE REGEX "^mux-mkenums ")
  if(codegen)
    list(APPEND options "mux-mkenums ${codegen}")
  endif()
  cme_options(muxnice ${options})
endfunction()
