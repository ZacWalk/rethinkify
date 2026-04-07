//! Software rendering into a `u32` pixel buffer. Draws glyph runs via cosmic-text/swash,
//! panel backgrounds, splitters, selection highlights, the blinking cursor, line numbers,
//! and scrollbars. Rebuilds cosmic-text buffers when dirty flags are set.

use cosmic_text::{
    Attrs, AttrsList, Buffer, BufferLine, Color, Family, FontSystem, LineEnding as CtLineEnding,
    Shaping, SwashCache, Weight,
};
use std::fs;
use std::num::NonZeroU32;

use crate::app::App;
use crate::app::FocusPanel;
use crate::syntax::{self, build_attrs_list, highlight_line};
use crate::ui;
use crate::views::{ViewMode, format_csv_table, format_hex_view};

// ─── Glyph rendering ───────────────────────────────────────────────────────

#[allow(clippy::too_many_arguments)]
pub(crate) fn draw_buffer(
    buf: &mut Buffer,
    font_sys: &mut FontSystem,
    swash: &mut SwashCache,
    pixels: &mut [u32],
    buf_width: u32,
    _buf_height: u32,
    offset_x: f32,
    offset_y: f32,
    clip_x: u32,
    clip_y: u32,
    clip_w: u32,
    clip_h: u32,
) {
    let clip_right = clip_x + clip_w;
    let clip_bottom = clip_y + clip_h;

    for run in buf.layout_runs() {
        let run_top = offset_y + run.line_y;
        // Skip runs that are entirely above the clip region
        if run_top + run.line_height < clip_y as f32 {
            continue;
        }
        // Stop once runs are entirely below the clip region
        if run_top > clip_bottom as f32 {
            break;
        }
        for glyph in run.glyphs.iter() {
            let physical = glyph.physical((offset_x, offset_y + run.line_y), 1.0);
            let Some(image) = swash.get_image(font_sys, physical.cache_key) else {
                continue;
            };

            let left = physical.x + image.placement.left;
            let top = physical.y - image.placement.top;

            let color = glyph.color_opt.unwrap_or(Color::rgb(0xDE, 0xDE, 0xDE));

            for r in 0..image.placement.height as i32 {
                for c in 0..image.placement.width as i32 {
                    let px = left + c;
                    let py = top + r;
                    if px < clip_x as i32
                        || px >= clip_right as i32
                        || py < clip_y as i32
                        || py >= clip_bottom as i32
                    {
                        continue;
                    }
                    let idx = (py as u32 * buf_width + px as u32) as usize;
                    if idx >= pixels.len() {
                        continue;
                    }
                    let img_idx = (r * image.placement.width as i32 + c) as usize;
                    if img_idx >= image.data.len() {
                        continue;
                    }
                    let alpha = image.data[img_idx] as f32 / 255.0;
                    if alpha < 0.01 {
                        continue;
                    }

                    let bg = pixels[idx];
                    let bg_r = ((bg >> 16) & 0xFF) as f32;
                    let bg_g = ((bg >> 8) & 0xFF) as f32;
                    let bg_b = (bg & 0xFF) as f32;

                    let final_r = (color.r() as f32 * alpha + bg_r * (1.0 - alpha)) as u32;
                    let final_g = (color.g() as f32 * alpha + bg_g * (1.0 - alpha)) as u32;
                    let final_b = (color.b() as f32 * alpha + bg_b * (1.0 - alpha)) as u32;

                    pixels[idx] = (final_r << 16) | (final_g << 8) | final_b;
                }
            }
        }
    }
}

// ─── Panel layout ───────────────────────────────────────────────────────────

/// Geometry, text metrics, and scroll state for a panel — used by common
/// drawing helpers (caret, scrollbar, highlights, line numbers).
pub(crate) struct PanelLayout {
    /// Panel origin (top-left corner) in pixels.
    pub x: u32,
    pub y: u32,
    /// Panel size in pixels.
    pub w: u32,
    pub h: u32,
    /// Inner padding in pixels.
    pub padding: f32,
    /// Left gutter width for line numbers (0 when unused).
    pub gutter: f32,
    /// Measured character width and line height.
    pub char_width: f32,
    pub line_height: f32,
    /// Total number of content lines.
    pub total_lines: usize,
    /// First visible line (vertical scroll offset, 0-based).
    pub scroll_line: usize,
}

impl PanelLayout {
    /// Number of fully visible text lines.
    fn visible_lines(&self) -> usize {
        if self.line_height <= 0.0 {
            return 0;
        }
        ((self.h as f32 - self.padding) / self.line_height) as usize
    }

    /// X coordinate where text content begins (after padding + gutter).
    fn text_x(&self) -> f32 {
        self.x as f32 + self.padding + self.gutter
    }

    /// Y coordinate for a content line, accounting for scroll.
    /// Returns `None` if the line is scrolled out of view.
    fn line_y(&self, line: usize) -> Option<f32> {
        if line < self.scroll_line {
            return None;
        }
        let y = self.y as f32 + self.padding + (line - self.scroll_line) as f32 * self.line_height;
        if y + self.line_height > (self.y + self.h) as f32 {
            return None;
        }
        Some(y)
    }

    /// Draw a 2-px-wide caret at (`col`, `line`) in content coordinates.
    fn draw_caret(
        &self,
        pixels: &mut [u32],
        buf_w: u32,
        buf_h: u32,
        col: usize,
        line: usize,
        color: u32,
    ) {
        let Some(cy) = self.line_y(line) else { return };
        let cx = self.text_x() + col as f32 * self.char_width;
        if cx >= self.x as f32 && cx < (self.x + self.w) as f32 {
            ui::fill_rect(
                pixels,
                buf_w,
                buf_h,
                cx as u32,
                cy as u32,
                2,
                self.line_height as u32,
                color,
            );
        }
    }

    /// Draw a full-width highlight bar on a single line.
    fn draw_line_highlight(
        &self,
        pixels: &mut [u32],
        buf_w: u32,
        buf_h: u32,
        line: usize,
        color: u32,
    ) {
        let Some(cy) = self.line_y(line) else { return };
        ui::fill_rect(
            pixels,
            buf_w,
            buf_h,
            self.x,
            cy as u32,
            self.w,
            self.line_height as u32,
            color,
        );
    }

    /// Draw selection highlight across a range of lines.
    fn draw_selection(
        &self,
        pixels: &mut [u32],
        buf_w: u32,
        buf_h: u32,
        sel: &crate::document::TextSelection,
        line_len: impl Fn(usize) -> usize,
        color: u32,
    ) {
        let sel = sel.normalized();
        for y in sel.start.y..=sel.end.y {
            let Some(cy) = self.line_y(y) else {
                if y < self.scroll_line {
                    continue;
                }
                break;
            };
            let start_x = if y == sel.start.y {
                sel.start.x as f32 * self.char_width
            } else {
                0.0
            };
            let end_x = if y == sel.end.y {
                sel.end.x as f32 * self.char_width
            } else {
                line_len(y) as f32 * self.char_width + self.char_width
            };
            let sx = self.text_x() + start_x;
            let sw = (end_x - start_x).max(1.0);
            ui::fill_rect(
                pixels,
                buf_w,
                buf_h,
                sx as u32,
                cy as u32,
                sw as u32,
                self.line_height as u32,
                color,
            );
        }
    }

    /// Draw a vertical scrollbar on the right edge of the panel.
    #[allow(clippy::too_many_arguments)]
    fn draw_scrollbar(
        &self,
        pixels: &mut [u32],
        buf_w: u32,
        buf_h: u32,
        scrollbar_w: u32,
        margin: u32,
        is_dragging: bool,
        is_hover: bool,
    ) {
        let visible = self.visible_lines();
        if self.total_lines <= visible {
            return;
        }
        let sb_x = self.x + self.w - scrollbar_w;
        let sb_h = self.h;
        let thumb_w = scrollbar_w - margin * 2;
        let thumb_h = ((visible as f32 / self.total_lines as f32) * sb_h as f32).max(20.0) as u32;
        let max_scroll = self.total_lines.saturating_sub(visible).max(1);
        let thumb_y = self.y
            + ((self.scroll_line as f32 / max_scroll as f32) * (sb_h - thumb_h) as f32) as u32;

        let thumb_color = if is_dragging {
            ui::SPLITTER_DRAG
        } else if is_hover {
            ui::SCROLLBAR_HOVER
        } else {
            ui::SCROLLBAR_THUMB
        };

        ui::fill_rect(
            pixels,
            buf_w,
            buf_h,
            sb_x,
            self.y,
            scrollbar_w,
            sb_h,
            ui::SCROLLBAR_BG,
        );
        ui::fill_rect(
            pixels,
            buf_w,
            buf_h,
            sb_x + margin,
            thumb_y,
            thumb_w,
            thumb_h,
            thumb_color,
        );
    }
}

/// Draw right-aligned line numbers in the gutter area of a panel.
fn draw_line_numbers(
    panel: &PanelLayout,
    line_num_buf: &mut Buffer,
    font_sys: &mut FontSystem,
    swash: &mut SwashCache,
    pixels: &mut [u32],
    buf_w: u32,
    buf_h: u32,
) {
    if panel.gutter <= 0.0 || panel.total_lines == 0 {
        return;
    }

    let visible = panel.visible_lines();
    let start = panel.scroll_line;
    let end = (start + visible + 1).min(panel.total_lines);
    let digits = ((panel.total_lines as f32).log10().floor() as usize + 1).max(2);

    let mut text = String::new();
    for line in start..end {
        use std::fmt::Write;
        let _ = writeln!(text, "{:>width$}", line + 1, width = digits);
    }

    let attrs = Attrs::new()
        .family(Family::Name("Consolas"))
        .color(Color::rgb(0x5A, 0x5A, 0x5A));
    line_num_buf.set_text(font_sys, &text, &attrs, Shaping::Advanced, None);
    line_num_buf.set_size(
        font_sys,
        Some(panel.gutter - panel.padding * 0.5),
        Some(panel.h as f32),
    );
    line_num_buf.shape_until_scroll(font_sys, false);

    draw_buffer(
        line_num_buf,
        font_sys,
        swash,
        pixels,
        buf_w,
        buf_h,
        panel.x as f32 + panel.padding * 0.5,
        panel.y as f32 + panel.padding,
        panel.x,
        panel.y,
        panel.gutter as u32,
        panel.h,
    );
}

// ─── Rendering methods ──────────────────────────────────────────────────────

impl App {
    pub(crate) fn update_doc_buffer(&mut self) {
        let Some(font_sys) = &mut self.font_system else {
            return;
        };
        let Some(doc_buf) = &mut self.doc_buffer else {
            return;
        };
        let Some(doc_path) = &self.active_doc_path else {
            doc_buf.set_text(
                font_sys,
                "No file open\n\nUse the file list or Ctrl+O to open a file.",
                &Attrs::new()
                    .family(Family::Name("Consolas"))
                    .color(Color::rgb(0x80, 0x80, 0x80)),
                Shaping::Advanced,
                None,
            );
            return;
        };

        let Some(doc) = self.open_docs.get(doc_path) else {
            return;
        };
        let ext = doc_path.extension().and_then(|s| s.to_str()).unwrap_or("");

        match self.view_mode {
            ViewMode::Hex => {
                self.doc_window_start = 0;
                // Re-read raw bytes for hex view
                let hex_text = if let Ok(data) = fs::read(doc_path) {
                    format_hex_view(&data).join("\n")
                } else {
                    doc.text()
                };
                doc_buf.set_text(
                    font_sys,
                    &hex_text,
                    &Attrs::new()
                        .family(Family::Name("Consolas"))
                        .color(Color::rgb(0xDE, 0xDE, 0xDE)),
                    Shaping::Advanced,
                    None,
                );
            }
            ViewMode::Csv => {
                self.doc_window_start = 0;
                let csv_lines = format_csv_table(&doc.text());
                let csv_text = csv_lines.join("\n");
                doc_buf.set_text(
                    font_sys,
                    &csv_text,
                    &Attrs::new()
                        .family(Family::Name("Consolas"))
                        .color(Color::rgb(0xDE, 0xDE, 0xDE)),
                    Shaping::Advanced,
                    None,
                );
                // Bold first line
                if !csv_lines.is_empty() && !doc_buf.lines.is_empty() {
                    let line_len = csv_lines[0].len();
                    let mut attrs = AttrsList::new(
                        &Attrs::new()
                            .family(Family::Name("Consolas"))
                            .color(Color::rgb(0xDE, 0xDE, 0xDE)),
                    );
                    attrs.add_span(
                        0..line_len,
                        &Attrs::new()
                            .family(Family::Name("Consolas"))
                            .weight(Weight::BOLD)
                            .color(Color::rgb(0xFF, 0xFF, 0xFF)),
                    );
                    doc_buf.lines[0].set_attrs_list(attrs);
                }
            }
            ViewMode::MarkdownPreview => {
                let default_attrs = Attrs::new()
                    .family(Family::Name("Calibri"))
                    .color(Color::rgb(0xDE, 0xDE, 0xDE));
                let syntax = syntax::detect_syntax(Some("md"));
                self.doc_window_start = 0;
                Self::incremental_update(
                    doc_buf,
                    font_sys,
                    &doc.lines,
                    doc.dirty_from,
                    &default_attrs,
                    Family::Name("Calibri"),
                    syntax,
                    &mut self.syntax_cookies,
                    0,
                );
            }
            ViewMode::EditText => {
                let default_attrs = Attrs::new()
                    .family(Family::Name("Consolas"))
                    .color(Color::rgb(0xDE, 0xDE, 0xDE));
                let syntax = syntax::detect_syntax(Some(ext));

                // Invalidate full cookie cache when content changed
                if doc.dirty_from.is_some() {
                    self.full_syntax_cookies.clear();
                }

                // ── Windowed buffer: only hold ±500 lines around the viewport ──
                let line_count = doc.lines.len();
                let half = 500usize;
                let window_start = doc.scroll_line.saturating_sub(half);
                let window_end = (doc.scroll_line + half).min(line_count);

                // Determine dirty_from relative to the window
                let window_dirty =
                    if self.doc_window_start != window_start || self.syntax_cookies.is_empty() {
                        // Window moved or first build — full rebuild of window
                        Some(0)
                    } else {
                        doc.dirty_from.and_then(|d| {
                            if d >= window_end {
                                None // change is outside window
                            } else if d < window_start {
                                Some(0) // change before window, rebuild all
                            } else {
                                Some(d - window_start)
                            }
                        })
                    };

                // Compute syntax cookie entering the first window line using
                // the full-document cookie cache for O(1) lookup instead of
                // re-scanning from line 0 every time.
                let line_count = doc.lines.len();
                let initial_cookie = if self.full_syntax_cookies.is_empty()
                    || self.full_syntax_cookies.len() != line_count
                {
                    // Build full cookie cache from scratch
                    self.full_syntax_cookies.clear();
                    self.full_syntax_cookies.reserve(line_count);
                    let mut cookie = 0u32;
                    for line in &doc.lines {
                        let (_, new_cookie) = highlight_line(syntax, cookie, line);
                        cookie = new_cookie;
                        self.full_syntax_cookies.push(cookie);
                    }
                    if window_start > 0 {
                        self.full_syntax_cookies[window_start - 1]
                    } else {
                        0u32
                    }
                } else if window_start > 0 {
                    self.full_syntax_cookies[window_start - 1]
                } else {
                    0u32
                };

                Self::incremental_update(
                    doc_buf,
                    font_sys,
                    &doc.lines[window_start..window_end],
                    window_dirty,
                    &default_attrs,
                    Family::Name("Consolas"),
                    syntax,
                    &mut self.syntax_cookies,
                    initial_cookie,
                );

                self.doc_window_start = window_start;
            }
        }

        self.rendered_doc_lines = doc_buf.lines.len();

        // Clear dirty_from now that the buffer is synced
        if let Some(doc) = self.open_docs.get_mut(doc_path) {
            doc.dirty_from = None;
        }
    }

    /// Incrementally sync the cosmic-text buffer with document lines.
    /// Only updates lines from `dirty_from` onward, preserving cached
    /// shape/layout for unchanged lines.
    /// `initial_cookie` is the syntax state entering `doc_lines[0]`.
    #[allow(clippy::too_many_arguments)]
    fn incremental_update(
        doc_buf: &mut Buffer,
        font_sys: &mut FontSystem,
        doc_lines: &[String],
        dirty_from: Option<usize>,
        default_attrs: &Attrs,
        font_family: Family,
        syntax: syntax::SyntaxType,
        syntax_cookies: &mut Vec<u32>,
        initial_cookie: u32,
    ) {
        let doc_line_count = doc_lines.len();
        let buf_line_count = doc_buf.lines.len();

        // Determine if we can do an incremental update or need a full rebuild.
        // Full rebuild when: buffer is empty, or dirty_from is 0 with major structural change
        let needs_full_rebuild = buf_line_count == 0
            || dirty_from == Some(0)
                && (doc_line_count as isize - buf_line_count as isize).unsigned_abs()
                    > doc_line_count / 2;

        if needs_full_rebuild {
            // Full rebuild path — same as before but caches cookies
            let text: String = doc_lines.join("\n");
            doc_buf.set_text(font_sys, &text, default_attrs, Shaping::Advanced, None);

            // Full syntax highlighting pass, cache cookies
            syntax_cookies.clear();
            syntax_cookies.reserve(doc_line_count);
            let mut cookie = initial_cookie;
            for (i, line) in doc_lines.iter().enumerate() {
                if i >= doc_buf.lines.len() {
                    break;
                }
                let (blocks, new_cookie) = highlight_line(syntax, cookie, line);
                cookie = new_cookie;
                if !blocks.is_empty() {
                    let attrs_list = build_attrs_list(line, &blocks, default_attrs, font_family);
                    doc_buf.lines[i].set_attrs_list(attrs_list);
                }
                syntax_cookies.push(cookie);
            }
            return;
        }

        let dirty_from = match dirty_from {
            Some(d) => d.min(doc_line_count.saturating_sub(1)),
            None => return, // Nothing changed
        };

        // ── Sync buffer lines with document lines ──
        // Lines before dirty_from are unchanged — their BufferLine shape/layout caches are valid.

        // Remove excess buffer lines or add new ones to match document line count
        if doc_line_count < buf_line_count {
            doc_buf.lines.truncate(doc_line_count);
        }

        // Update text for existing lines from dirty_from onward
        let shared_end = doc_line_count.min(buf_line_count);
        for (i, doc_line) in doc_lines
            .iter()
            .enumerate()
            .take(shared_end)
            .skip(dirty_from)
        {
            let ending = if i < doc_line_count - 1 {
                CtLineEnding::Lf
            } else {
                CtLineEnding::None
            };
            // set_text only invalidates shape/layout if text actually changed
            doc_buf.lines[i].set_text(doc_line, ending, AttrsList::new(default_attrs));
        }

        // Append new lines beyond the old buffer length
        for (i, doc_line) in doc_lines
            .iter()
            .enumerate()
            .take(doc_line_count)
            .skip(buf_line_count)
        {
            let ending = if i < doc_line_count - 1 {
                CtLineEnding::Lf
            } else {
                CtLineEnding::None
            };
            doc_buf.lines.push(BufferLine::new(
                doc_line.as_str(),
                ending,
                AttrsList::new(default_attrs),
                Shaping::Advanced,
            ));
        }

        // Fix the ending on the old last line if it moved
        if dirty_from < shared_end && buf_line_count != doc_line_count {
            let old_last = buf_line_count.saturating_sub(1);
            if old_last >= dirty_from && old_last < doc_buf.lines.len() {
                let correct_ending = if old_last < doc_line_count - 1 {
                    CtLineEnding::Lf
                } else {
                    CtLineEnding::None
                };
                doc_buf.lines[old_last].set_ending(correct_ending);
            }
        }

        // Also fix ending on the new last line
        if let Some(last) = doc_buf.lines.last_mut() {
            last.set_ending(CtLineEnding::None);
        }

        doc_buf.set_redraw(true);

        // ── Incremental syntax highlighting ──
        // Start from dirty_from using the cached cookie from the previous line.
        // Stop early when the output cookie matches the cached one (no downstream change).
        syntax_cookies.resize(doc_line_count, 0);

        let start_cookie = if dirty_from > 0 {
            syntax_cookies[dirty_from - 1]
        } else {
            initial_cookie
        };

        let mut cookie = start_cookie;
        for i in dirty_from..doc_line_count {
            if i >= doc_buf.lines.len() {
                break;
            }
            let (blocks, new_cookie) = highlight_line(syntax, cookie, &doc_lines[i]);
            cookie = new_cookie;

            if !blocks.is_empty() {
                let attrs_list =
                    build_attrs_list(&doc_lines[i], &blocks, default_attrs, font_family);
                doc_buf.lines[i].set_attrs_list(attrs_list);
            } else {
                // Reset to default attrs if no highlighting blocks
                doc_buf.lines[i].set_attrs_list(AttrsList::new(default_attrs));
            }

            let old_cookie = syntax_cookies[i];
            syntax_cookies[i] = cookie;

            // If the cookie hasn't changed from this point, lines below are still valid
            if cookie == old_cookie && i > dirty_from {
                break;
            }
        }
    }

    pub(crate) fn update_list_buffer(&mut self) {
        // Rebuild flat items before borrowing font_system
        if self.list_mode == crate::app::ListMode::Files {
            self.rebuild_flat_items();
        }

        let Some(font_sys) = &mut self.font_system else {
            return;
        };
        let Some(list_buf) = &mut self.list_buffer else {
            return;
        };

        match self.list_mode {
            crate::app::ListMode::Files => {
                let mut text = String::new();
                let root_name = self
                    .root_path
                    .file_name()
                    .unwrap_or_default()
                    .to_string_lossy();
                text.push_str(&format!("EXPLORER: {}\n", root_name));

                for (i, item) in self.flat_items.iter().enumerate() {
                    let indent = "  ".repeat(item.depth);
                    let icon = if item.is_folder {
                        if item.expanded { "▾ " } else { "▸ " }
                    } else {
                        "  "
                    };
                    let marker = if i == self.selected_item_idx {
                        "→ "
                    } else {
                        "  "
                    };
                    text.push_str(&format!("{}{}{}{}\n", marker, indent, icon, item.name));
                }

                let default_attrs = Attrs::new()
                    .family(Family::Name("Calibri"))
                    .color(Color::rgb(0xDE, 0xDE, 0xDE));
                list_buf.set_text(font_sys, &text, &default_attrs, Shaping::Advanced, None);

                // Style the header
                if !list_buf.lines.is_empty() {
                    let hl = list_buf.lines[0].text().len();
                    let mut attrs = AttrsList::new(&default_attrs);
                    attrs.add_span(
                        0..hl,
                        &Attrs::new()
                            .family(Family::Name("Calibri"))
                            .weight(Weight::BOLD)
                            .color(Color::rgb(0xFF, 0xFF, 0xFF)),
                    );
                    list_buf.lines[0].set_attrs_list(attrs);
                }

                // Style folders and modified files
                for (i, item) in self.flat_items.iter().enumerate() {
                    let line_idx = i + 1; // +1 for header
                    if line_idx >= list_buf.lines.len() {
                        break;
                    }
                    let line_text = list_buf.lines[line_idx].text().to_string();
                    let line_len = line_text.len();
                    if line_len == 0 {
                        continue;
                    }

                    let color = if item.modified {
                        Color::rgb(0xFF, 0x44, 0x44)
                    } else if item.is_folder {
                        Color::rgb(0xEE, 0xCC, 0x22)
                    } else if i == self.selected_item_idx {
                        Color::rgb(0xFF, 0xFF, 0xFF)
                    } else {
                        continue;
                    };

                    let mut attrs = AttrsList::new(&default_attrs);
                    attrs.add_span(
                        0..line_len,
                        &Attrs::new().family(Family::Name("Calibri")).color(color),
                    );
                    list_buf.lines[line_idx].set_attrs_list(attrs);
                }
            }
            crate::app::ListMode::Search => {
                let mut text = String::new();
                text.push_str(&format!("SEARCH: {}\n", self.search_query));
                let total: usize = self.search_groups.iter().map(|g| g.results.len()).sum();
                text.push_str(&format!("{} results\n\n", total));

                for group in &self.search_groups {
                    text.push_str(&format!(
                        "{} ({})\n",
                        group.relative_path,
                        group.results.len()
                    ));
                    if group.expanded {
                        for result in &group.results {
                            text.push_str(&format!(
                                "  {}:  {}\n",
                                result.line_number, result.line_text
                            ));
                        }
                    }
                }

                let default_attrs = Attrs::new()
                    .family(Family::Name("Calibri"))
                    .color(Color::rgb(0xDE, 0xDE, 0xDE));
                list_buf.set_text(font_sys, &text, &default_attrs, Shaping::Advanced, None);

                // Style search header line
                if !list_buf.lines.is_empty() {
                    let header_text = list_buf.lines[0].text().to_string();
                    let hl = header_text.len();
                    let prefix_len = "SEARCH: ".len().min(hl);
                    let mut attrs = AttrsList::new(&default_attrs);
                    attrs.add_span(
                        0..prefix_len,
                        &Attrs::new()
                            .family(Family::Name("Calibri"))
                            .weight(Weight::BOLD)
                            .color(Color::rgb(0xFF, 0xFF, 0xFF)),
                    );
                    if hl > prefix_len {
                        attrs.add_span(
                            prefix_len..hl,
                            &Attrs::new()
                                .family(Family::Name("Calibri"))
                                .color(Color::rgb(0xFF, 0xFF, 0xFF)),
                        );
                    }
                    list_buf.lines[0].set_attrs_list(attrs);
                }

                // Style file group headers yellow
                let mut line_idx = 3; // skip header, count line, blank line
                for group in &self.search_groups {
                    if line_idx < list_buf.lines.len() {
                        let line_text = list_buf.lines[line_idx].text().to_string();
                        let line_len = line_text.len();
                        if line_len > 0 {
                            let mut attrs = AttrsList::new(&default_attrs);
                            attrs.add_span(
                                0..line_len,
                                &Attrs::new()
                                    .family(Family::Name("Calibri"))
                                    .color(Color::rgb(0xEE, 0xCC, 0x22)),
                            );
                            list_buf.lines[line_idx].set_attrs_list(attrs);
                        }
                    }
                    line_idx += 1;
                    if group.expanded {
                        line_idx += group.results.len();
                    }
                }
            }
        }
    }

    pub(crate) fn update_console_buffer(&mut self) {
        let Some(font_sys) = &mut self.font_system else {
            return;
        };
        let Some(console_buf) = &mut self.console_buffer else {
            return;
        };

        let mut text = String::new();
        for line in &self.console.lines {
            text.push_str(&line.text);
            text.push('\n');
        }
        text.push_str(&format!("> {}", self.console.input));

        let default_attrs = Attrs::new()
            .family(Family::Name("Consolas"))
            .color(Color::rgb(0xCC, 0xCC, 0xCC));
        console_buf.set_text(font_sys, &text, &default_attrs, Shaping::Advanced, None);

        // Color command echo lines yellow
        let mut line_idx = 0;
        for cline in &self.console.lines {
            if line_idx >= console_buf.lines.len() {
                break;
            }
            if cline.is_command {
                let len = cline.text.len();
                let mut attrs = AttrsList::new(&default_attrs);
                attrs.add_span(
                    0..len,
                    &Attrs::new()
                        .family(Family::Name("Consolas"))
                        .color(Color::rgb(0xDE, 0xDE, 0x00)),
                );
                console_buf.lines[line_idx].set_attrs_list(attrs);
            }
            line_idx += 1;
        }

        // Color prompt line
        if line_idx < console_buf.lines.len() {
            let prompt_text = format!("> {}", self.console.input);
            let len = prompt_text.len();
            let mut attrs = AttrsList::new(&default_attrs);
            attrs.add_span(
                0..2.min(len),
                &Attrs::new()
                    .family(Family::Name("Consolas"))
                    .color(Color::rgb(0x00, 0xCC, 0x00)),
            );
            console_buf.lines[line_idx].set_attrs_list(attrs);
        }
    }

    pub(crate) fn render(&mut self) {
        let Some(window) = &self.window else { return };
        let size = window.inner_size();
        if size.width == 0 || size.height == 0 {
            return;
        }

        // ── Pre-compute DPI-scaled constants ────────────────────────────
        let splitter_w = self.dpi(6.0);
        let hit_margin = 8.0 * self.scale_factor;
        let scrollbar_w = self.dpi(14.0);
        let sb_margin = self.dpi(3.0);
        let padding = (6.0 * self.scale_factor as f32).round();

        // ── Panel geometry ──────────────────────────────────────────────
        let panel_w = (size.width as f32 * self.panel_ratio) as u32;
        let console_y = (size.height as f32 * self.console_ratio) as u32;
        let doc_x = panel_w + splitter_w;
        let doc_w = size.width.saturating_sub(doc_x);
        let doc_h = console_y;
        let console_area_y = console_y + splitter_w;
        let console_h = size.height.saturating_sub(console_area_y);

        // ── Snapshot mutable state before borrowing buffers ──────────────
        let focus = self.focus;
        let view_mode = self.view_mode;
        let list_mode = self.list_mode;
        let is_dragging_scrollbar = self.is_dragging_scrollbar;
        let scrollbar_hover = self.scrollbar_hover;
        let word_wrap = self.word_wrap;
        let caret_visible = {
            let now = std::time::Instant::now();
            if now.duration_since(self.caret_last_toggle).as_millis() >= 530 {
                self.caret_visible = !self.caret_visible;
                self.caret_last_toggle = now;
            }
            self.caret_visible
        };

        // Document state
        let doc_is_edit = view_mode == ViewMode::EditText;
        let (doc_scroll_line, doc_cursor, doc_total_lines, doc_has_sel, doc_sel) = {
            let doc = self
                .active_doc_path
                .as_ref()
                .and_then(|p| self.open_docs.get(p));
            match doc {
                Some(d) => (
                    d.scroll_line,
                    d.cursor,
                    if doc_is_edit {
                        d.line_count()
                    } else {
                        self.rendered_doc_lines
                    },
                    d.has_selection(),
                    d.selection(),
                ),
                None => (
                    0,
                    crate::document::TextLocation::default(),
                    0,
                    false,
                    crate::document::TextSelection::default(),
                ),
            }
        };

        // Line-number gutter width (EditText only)
        let gutter = if doc_is_edit && doc_total_lines > 0 {
            let digits = if doc_total_lines <= 1 {
                2
            } else {
                ((doc_total_lines as f32).log10().floor() as usize + 1).max(2)
            };
            (digits as f32 + 1.5) * self.char_width
        } else {
            0.0
        };

        // List state
        let list_scroll = self.file_scroll;
        let list_total_lines = match list_mode {
            crate::app::ListMode::Files => self.flat_items.len() + 1,
            crate::app::ListMode::Search => {
                3 + self
                    .search_groups
                    .iter()
                    .map(|g| 1 + if g.expanded { g.results.len() } else { 0 })
                    .sum::<usize>()
            }
        };
        let selected_item_idx = self.selected_item_idx;
        let search_cursor = self.search_cursor;

        // Console state
        let console_scroll = self.console.scroll_offset;
        let console_total_lines = self.console.lines.len() + 1;
        let console_input_cursor = self.console.input_cursor;

        // ── Surface & pixel buffer ──────────────────────────────────────
        let Some(surface) = &mut self.surface else {
            return;
        };
        surface
            .resize(
                NonZeroU32::new(size.width).unwrap(),
                NonZeroU32::new(size.height).unwrap(),
            )
            .unwrap();
        let mut pixels = surface.buffer_mut().unwrap();
        pixels.fill(ui::MAIN_BG);

        // ── Backgrounds & splitters ─────────────────────────────────────
        ui::fill_rect(
            &mut pixels,
            size.width,
            size.height,
            0,
            0,
            panel_w,
            size.height,
            ui::PANEL_BG,
        );

        let vsplitter_color = if self.is_dragging_panel {
            ui::SPLITTER_DRAG
        } else if (self.cursor_pos.x - panel_w as f64).abs() < hit_margin {
            ui::SPLITTER_HOVER
        } else {
            ui::SPLITTER_COLOR
        };
        ui::fill_rect(
            &mut pixels,
            size.width,
            size.height,
            panel_w,
            0,
            splitter_w,
            size.height,
            vsplitter_color,
        );

        let hsplitter_color = if self.is_dragging_console {
            ui::SPLITTER_DRAG
        } else if self.cursor_pos.x > panel_w as f64
            && (self.cursor_pos.y - console_y as f64).abs() < hit_margin
        {
            ui::SPLITTER_HOVER
        } else {
            ui::SPLITTER_COLOR
        };
        ui::fill_rect(
            &mut pixels,
            size.width,
            size.height,
            doc_x,
            console_y,
            doc_w,
            splitter_w,
            hsplitter_color,
        );

        ui::fill_rect(
            &mut pixels,
            size.width,
            size.height,
            doc_x,
            console_area_y,
            doc_w,
            console_h,
            ui::CONSOLE_BG,
        );

        // ── Build PanelLayouts ──────────────────────────────────────────
        let list_panel = PanelLayout {
            x: 0,
            y: 0,
            w: panel_w,
            h: size.height,
            padding,
            gutter: 0.0,
            char_width: self.list_char_width,
            line_height: self.list_line_height,
            total_lines: list_total_lines,
            scroll_line: list_scroll,
        };

        let doc_panel = PanelLayout {
            x: doc_x,
            y: 0,
            w: doc_w,
            h: doc_h,
            padding,
            gutter,
            char_width: self.char_width,
            line_height: self.line_height,
            total_lines: doc_total_lines,
            scroll_line: doc_scroll_line,
        };

        let console_panel = PanelLayout {
            x: doc_x,
            y: console_area_y,
            w: doc_w,
            h: console_h,
            padding,
            gutter: 0.0,
            char_width: self.console_char_width,
            line_height: self.console_line_height,
            total_lines: console_total_lines,
            scroll_line: console_scroll,
        };

        // ── Highlights (drawn before text) ──────────────────────────────

        // Current line highlight in editor
        if doc_is_edit {
            doc_panel.draw_line_highlight(
                &mut pixels,
                size.width,
                size.height,
                doc_cursor.y,
                ui::CURRENT_LINE_BG,
            );
        }

        // Selection highlight in editor
        if doc_is_edit && doc_has_sel {
            let doc = self
                .active_doc_path
                .as_ref()
                .and_then(|p| self.open_docs.get(p))
                .unwrap();
            doc_panel.draw_selection(
                &mut pixels,
                size.width,
                size.height,
                &doc_sel,
                |y| doc.line_text(y).len(),
                ui::SELECTION_BG,
            );
        }

        // Selected item highlight in file list
        if list_mode == crate::app::ListMode::Files {
            let sel_color = if focus == FocusPanel::List {
                ui::SELECTION_BG
            } else {
                ui::LIST_INACTIVE_BG
            };
            list_panel.draw_line_highlight(
                &mut pixels,
                size.width,
                size.height,
                selected_item_idx + 1,
                sel_color,
            );
        }

        // Search input box background
        if list_mode == crate::app::ListMode::Search {
            list_panel.draw_line_highlight(
                &mut pixels,
                size.width,
                size.height,
                0,
                ui::CURRENT_LINE_BG,
            );
            // Highlight selected search result
            if !self.search_groups.is_empty() {
                let mut line_idx = 3usize; // header + count + blank
                let mut result_idx = 0usize;
                for group in &self.search_groups {
                    line_idx += 1; // group header
                    if group.expanded {
                        for _r in &group.results {
                            if result_idx == self.search_selected_idx {
                                list_panel.draw_line_highlight(
                                    &mut pixels,
                                    size.width,
                                    size.height,
                                    line_idx,
                                    ui::SELECTION_BG,
                                );
                            }
                            line_idx += 1;
                            result_idx += 1;
                        }
                    } else {
                        result_idx += group.results.len();
                    }
                }
            }
        }

        // ── Shape & draw cosmic-text buffers ────────────────────────────
        let font_sys = self.font_system.as_mut().unwrap();
        let swash = self.swash_cache.as_mut().unwrap();
        let list_buf = self.list_buffer.as_mut().unwrap();
        let doc_buf = self.doc_buffer.as_mut().unwrap();
        let console_buf = self.console_buffer.as_mut().unwrap();

        // List panel
        list_buf.set_size(
            font_sys,
            Some(panel_w as f32 - padding * 2.0),
            Some(size.height as f32),
        );
        list_buf.shape_until_scroll(font_sys, false);

        if let Some(run) = list_buf.layout_runs().next() {
            if run.line_height > 0.0 {
                self.list_line_height = run.line_height;
            }
            for glyph in run.glyphs.iter() {
                if glyph.w > 0.0 {
                    self.list_char_width = glyph.w;
                    break;
                }
            }
        }

        let list_scroll_offset = list_scroll as f32 * self.list_line_height;
        draw_buffer(
            list_buf,
            font_sys,
            swash,
            &mut pixels,
            size.width,
            size.height,
            padding,
            padding - list_scroll_offset,
            0,
            0,
            panel_w,
            size.height,
        );

        // Document panel — scroll offset is relative to the buffer window start
        let window_scroll = doc_scroll_line.saturating_sub(self.doc_window_start);
        let visible = (doc_h as f32 / self.line_height).ceil() as usize + 1;
        let total_doc_h = (window_scroll + visible) as f32 * self.line_height;
        let text_area_w = doc_w as f32 - padding * 2.0 - gutter;
        if word_wrap {
            doc_buf.set_size(font_sys, Some(text_area_w), Some(total_doc_h));
        } else {
            doc_buf.set_size(font_sys, Some(f32::MAX), Some(total_doc_h));
        }
        doc_buf.shape_until_scroll(font_sys, false);

        if let Some(run) = doc_buf.layout_runs().next() {
            for glyph in run.glyphs.iter() {
                if glyph.w > 0.0 {
                    self.char_width = glyph.w;
                    break;
                }
            }
            if run.line_height > 0.0 {
                self.line_height = run.line_height;
            }
        }

        let doc_scroll_offset = window_scroll as f32 * self.line_height;
        let gutter_px = gutter as u32;
        draw_buffer(
            doc_buf,
            font_sys,
            swash,
            &mut pixels,
            size.width,
            size.height,
            doc_x as f32 + padding + gutter,
            padding - doc_scroll_offset,
            doc_x + gutter_px,
            0,
            doc_w - gutter_px,
            doc_h,
        );

        // Gutter background + line numbers
        if doc_is_edit && gutter > 0.0 {
            ui::fill_rect(
                &mut pixels,
                size.width,
                size.height,
                doc_x,
                0,
                gutter_px,
                doc_h,
                ui::GUTTER_BG,
            );
        }
        if doc_is_edit
            && gutter > 0.0
            && let Some(line_num_buf) = self.line_num_buffer.as_mut()
        {
            draw_line_numbers(
                &doc_panel,
                line_num_buf,
                font_sys,
                swash,
                &mut pixels,
                size.width,
                size.height,
            );
        }

        // Console panel — buffer height must cover all content, not just panel height
        let console_scroll_offset = console_scroll as f32 * self.console_line_height;
        let console_content_h =
            (console_total_lines as f32 + 1.0) * self.console_line_height + padding * 2.0;
        console_buf.set_size(
            font_sys,
            Some(doc_w as f32 - padding * 2.0),
            Some(console_content_h.max(console_h as f32)),
        );
        console_buf.shape_until_scroll(font_sys, false);

        if let Some(run) = console_buf.layout_runs().next() {
            for glyph in run.glyphs.iter() {
                if glyph.w > 0.0 {
                    self.console_char_width = glyph.w;
                    break;
                }
            }
            if run.line_height > 0.0 {
                self.console_line_height = run.line_height;
            }
        }

        draw_buffer(
            console_buf,
            font_sys,
            swash,
            &mut pixels,
            size.width,
            size.height,
            doc_x as f32 + padding,
            console_area_y as f32 + padding - console_scroll_offset,
            doc_x,
            console_area_y,
            doc_w,
            console_h,
        );

        // ── Carets ──────────────────────────────────────────────────────
        if doc_is_edit && focus == FocusPanel::Document && caret_visible {
            doc_panel.draw_caret(
                &mut pixels,
                size.width,
                size.height,
                doc_cursor.x,
                doc_cursor.y,
                0xFFFFFF,
            );
        }

        if focus == FocusPanel::Console && caret_visible {
            console_panel.draw_caret(
                &mut pixels,
                size.width,
                size.height,
                console_input_cursor + 2,
                console_total_lines - 1,
                0x00CC00,
            );
        }

        if focus == FocusPanel::List && list_mode == crate::app::ListMode::Search && caret_visible {
            // Measure actual pixel position of text "SEARCH: " using char_width
            let prefix_chars = "SEARCH: ".chars().count();
            list_panel.draw_caret(
                &mut pixels,
                size.width,
                size.height,
                prefix_chars + search_cursor,
                0,
                0xFFFFFF,
            );
        }

        // ── Scrollbars ──────────────────────────────────────────────────
        doc_panel.draw_scrollbar(
            &mut pixels,
            size.width,
            size.height,
            scrollbar_w,
            sb_margin,
            is_dragging_scrollbar,
            scrollbar_hover,
        );
        list_panel.draw_scrollbar(
            &mut pixels,
            size.width,
            size.height,
            scrollbar_w,
            sb_margin,
            self.is_dragging_list_scrollbar,
            self.list_scrollbar_hover,
        );
        console_panel.draw_scrollbar(
            &mut pixels,
            size.width,
            size.height,
            scrollbar_w,
            sb_margin,
            false,
            false,
        );

        // ── Focus indicator ─────────────────────────────────────────────
        let (fi_x, fi_y, fi_w) = match focus {
            FocusPanel::List => (0u32, 0u32, panel_w),
            FocusPanel::Document => (doc_x, 0, doc_w),
            FocusPanel::Console => (doc_x, console_area_y, doc_w),
        };
        ui::fill_rect(
            &mut pixels,
            size.width,
            size.height,
            fi_x,
            fi_y,
            fi_w,
            2,
            0x1166CC,
        );

        pixels.present().unwrap();
    }
}
