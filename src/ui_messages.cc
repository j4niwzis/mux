// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui.messages -- A chat's messages, as the window shows them: a bubble,
// its pieces and pictures, its text; the field one writes in; the timeline.
// Above the plugins' and protocols' UI modules, whose trailers and sticker
// views a bubble holds -- plain calls here -- and below what shows messages
// for a plugin (a reaction's events, as bubbles).
export module mux.ui.messages;

// What its parts are made with, seen by what imports them, as a
// partition's own imports were within one module.
export import chevron.escape;
export import splice;
export import skia;
export import skiff.paint;
export import skiff.scene;
export import skiff.compose;
export import skiff.nodes.box;
export import skiff.nodes.flow;
export import skiff.nodes.icon;
export import skiff.nodes.image;
export import skiff.nodes.text;
export import skiff.widgets.loader;
export import skiff.widgets.pill;
export import mux.platform.audio;
export import mux.core;
export import mux.config;
export import mux.logic.links;
export import mux.protocols;
export import skiff.model;
export import mux.plugins.types;
export import skiff.widgets.button;
export import skiff.widgets.textarea;
export import skiff.nodes.scroll;
export import skiff.widgets.wallpaper;
export import mux.platform.video;

export import :message_pieces;
export import :message_media;
export import :message_text;
export import :message;
export import :composer;
export import :timeline;
