// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.base -- What every part of the window is made of, and what the
// plugins' and protocols' own views are made of too: the palette and the
// looks, the icons and avatars, the controls, names and text, the window's
// requests, the emoji kept. Below the plugins' and protocols' UI modules,
// which import it -- and above them the rest of mux.ui, which imports them:
// their hooks are plain calls there, nothing put off to the program.
export module mux.ui.base;
export import skiff.widgets.pill;

// What its parts are made with, seen by what imports them, as a
// partition's own imports were within one module.
export import splice.bytes;
export import splice;
export import skia;
export import skiff.paint;
export import skiff.scene;
export import skiff.nodes.box;
export import skiff.nodes.text;
export import skiff.compose;
export import skiff.widgets.theme;
export import skiff.widgets.sliderbar;
export import skiff.widgets.button;
export import skiff.widgets.wallpaper;
export import skiff.widgets.motion;
export import skiff.bind;
export import mux.core;
export import mux.protocols;
export import mux.config;
export import mux.logic.text;
export import mux.platform.audio;
export import skiff.nodes.icon;
export import skiff.nodes.image;
export import skiff.nodes.flow;
export import skiff.widgets.avatar;
export import skiff.widgets.model;
export import skiff.widgets.textbox;
export import skiff.model;
export import mux.logic.room_events;
export import skiff.nodes;
export import skiff.widgets;
export import chevron;
export import mux.plugins.types;
export import skiff.widgets.textarea;
export import skiff.nodes.scroll;
export import mux.logic.emoji;
export import mux.logic.reactions;

export import :base;
export import :requests;
export import :icons;
export import :avatars;
export import :controls;
export import :themes;
export import :names;
export import :html;
export import :emoji_kept;
export import :forms;
export import :pages;
export import :emoji_panels;
