//! Keyboard and mouse input handling. Translates key presses and mouse events into
//! document edits, list navigation, and console interaction for each focus panel.

use crate::app::{App, FocusPanel, ListMode};
use crate::document::TextLocation;
use crate::views::ViewMode;
use winit::keyboard::{Key, NamedKey};

impl App {
    pub(crate) fn doc_pos_from_mouse(&self, mouse_x: f64, mouse_y: f64) -> Option<TextLocation> {
        let Some(window) = &self.window else {
            return None;
        };
        let size = window.inner_size();
        let splitter_w = self.dpi(6.0);
        let panel_w = (size.width as f32 * self.panel_ratio) as u32;
        let doc_x = panel_w + splitter_w;
        let padding = (6.0 * self.scale_factor as f32).round();

        // Compute gutter width (same logic as render.rs)
        let doc = self.active_doc()?;
        let gutter = if self.view_mode == ViewMode::EditText && doc.line_count() > 0 {
            let total = doc.line_count();
            let digits = if total <= 1 {
                2
            } else {
                ((total as f32).log10().floor() as usize + 1).max(2)
            };
            (digits as f32 + 1.5) * self.char_width
        } else {
            0.0
        };

        let x_in_doc = mouse_x - doc_x as f64 - padding as f64 - gutter as f64;
        let y_in_doc = mouse_y - padding as f64;

        if x_in_doc < 0.0 || y_in_doc < 0.0 {
            return None;
        }

        let col = (x_in_doc as f32 / self.char_width).round() as usize;
        let line = (y_in_doc as f32 / self.line_height) as usize + doc.scroll_line;
        let line = line.min(doc.line_count().saturating_sub(1));
        let col = col.min(doc.line_text(line).len());

        Some(TextLocation::new(line, col))
    }

    pub(crate) fn list_item_from_mouse(&self, mouse_y: f64) -> Option<usize> {
        let padding = (6.0 * self.scale_factor as f32).round();
        let y = mouse_y as f32 - padding;
        if y < 0.0 {
            return None;
        }
        let line_idx = (y / self.list_line_height) as usize;
        if line_idx == 0 {
            return None; // Header
        }
        let item_idx = line_idx - 1 + self.file_scroll;
        if item_idx < self.flat_items.len() {
            Some(item_idx)
        } else {
            None
        }
    }

    /// Return the raw line index (0-based, accounting for scroll) under the mouse in the list panel.
    pub(crate) fn list_item_from_mouse_raw(&self, mouse_y: f64) -> Option<usize> {
        let padding = (6.0 * self.scale_factor as f32).round();
        let y = mouse_y as f32 - padding;
        if y < 0.0 {
            return None;
        }
        let line_idx = (y / self.list_line_height) as usize + self.file_scroll;
        Some(line_idx)
    }

    pub(crate) fn handle_doc_key(&mut self, key: &Key, shift: bool, ctrl: bool) -> bool {
        let rendered_lines = self.rendered_doc_lines;
        let is_edit = self.view_mode == ViewMode::EditText;
        let doc = match self.active_doc_mut() {
            Some(d) => d,
            None => return false,
        };

        let content_changed = match key {
            // Navigation — no content change, no buffer rebuild needed
            Key::Named(NamedKey::ArrowLeft) if ctrl => {
                doc.move_word_left(shift);
                false
            }
            Key::Named(NamedKey::ArrowRight) if ctrl => {
                doc.move_word_right(shift);
                false
            }
            Key::Named(NamedKey::ArrowLeft) => {
                doc.move_left(shift);
                false
            }
            Key::Named(NamedKey::ArrowRight) => {
                doc.move_right(shift);
                false
            }
            Key::Named(NamedKey::ArrowUp) if ctrl => {
                doc.scroll_line = doc.scroll_line.saturating_sub(1);
                false
            }
            Key::Named(NamedKey::ArrowDown) if ctrl => {
                let max = if is_edit {
                    doc.line_count()
                } else {
                    rendered_lines
                };
                doc.scroll_line = doc.scroll_line.saturating_add(1).min(max.saturating_sub(1));
                false
            }
            Key::Named(NamedKey::ArrowUp) => {
                doc.move_up(shift);
                false
            }
            Key::Named(NamedKey::ArrowDown) => {
                doc.move_down(shift);
                false
            }
            Key::Named(NamedKey::Home) if ctrl => {
                doc.move_doc_start(shift);
                false
            }
            Key::Named(NamedKey::End) if ctrl => {
                doc.move_doc_end(shift);
                false
            }
            Key::Named(NamedKey::Home) => {
                doc.move_home(shift);
                false
            }
            Key::Named(NamedKey::End) => {
                doc.move_end(shift);
                false
            }
            Key::Named(NamedKey::PageUp) => {
                doc.page_up(20, shift);
                false
            }
            Key::Named(NamedKey::PageDown) => {
                doc.page_down(20, shift);
                false
            }
            // Content changes — require buffer rebuild
            Key::Named(NamedKey::Backspace) => {
                doc.edit_backspace();
                true
            }
            Key::Named(NamedKey::Delete) => {
                doc.edit_delete_forward();
                true
            }
            Key::Named(NamedKey::Enter) => {
                doc.edit_insert("\n");
                true
            }
            Key::Named(NamedKey::Tab) if shift => {
                doc.edit_untab();
                true
            }
            Key::Named(NamedKey::Tab) => {
                doc.edit_tab();
                true
            }
            _ => return false,
        };

        if content_changed {
            self.doc_dirty = true;
        }
        true
    }

    pub(crate) fn handle_ctrl_key(&mut self, c: &str) -> bool {
        match c.to_lowercase().as_str() {
            "s" => {
                if self.modifiers.shift_key() {
                    self.save_all();
                } else {
                    self.save_active_doc();
                }
                true
            }
            "n" => {
                self.new_document();
                true
            }
            "o" => {
                if let Some(path) = rfd::FileDialog::new().pick_file() {
                    self.open_file(&path.clone());
                }
                true
            }
            "z" => {
                if let Some(doc) = self.active_doc_mut() {
                    doc.undo();
                    self.doc_dirty = true;
                }
                true
            }
            "y" => {
                if let Some(doc) = self.active_doc_mut() {
                    doc.redo();
                    self.doc_dirty = true;
                }
                true
            }
            "c" => {
                if let Some(doc) = self.active_doc() {
                    let text = if doc.has_selection() {
                        doc.selected_text()
                    } else {
                        // Copy whole line
                        let line = doc.line_text(doc.cursor.y);
                        format!("{}\n", line)
                    };
                    if let Some(clip) = &mut self.clipboard {
                        let _ = clip.set_text(&text);
                    }
                }
                true
            }
            "x" => {
                if let Some(doc) = self.active_doc() {
                    let text = doc.selected_text();
                    if let Some(clip) = &mut self.clipboard {
                        let _ = clip.set_text(&text);
                    }
                }
                if let Some(doc) = self.active_doc_mut() {
                    doc.edit_delete_selection();
                    self.doc_dirty = true;
                }
                true
            }
            "v" => {
                let text = self
                    .clipboard
                    .as_mut()
                    .and_then(|c| c.get_text().ok())
                    .unwrap_or_default()
                    .replace("\r\n", "\n")
                    .replace('\r', "\n");
                if !text.is_empty()
                    && let Some(doc) = self.active_doc_mut()
                {
                    doc.edit_insert(&text);
                    self.doc_dirty = true;
                }
                true
            }
            "a" => {
                if let Some(doc) = self.active_doc_mut() {
                    doc.select_all();
                }
                true
            }
            "r" => {
                if let Some(doc) = self.active_doc_mut() {
                    doc.reformat_json();
                    self.doc_dirty = true;
                }
                true
            }
            "m" => {
                self.view_mode = if self.view_mode == ViewMode::MarkdownPreview {
                    ViewMode::EditText
                } else {
                    ViewMode::MarkdownPreview
                };
                self.doc_dirty = true;
                true
            }
            "f" => {
                if self.modifiers.shift_key() {
                    self.list_mode = if self.list_mode == ListMode::Search {
                        ListMode::Files
                    } else {
                        self.search_cursor = self.search_query.len();
                        ListMode::Search
                    };
                    self.focus = FocusPanel::List;
                    self.list_dirty = true;
                }
                true
            }
            "d" => {
                if let Some(doc) = self.active_doc_mut() {
                    doc.duplicate_line();
                    self.doc_dirty = true;
                }
                true
            }
            "k" => {
                if self.modifiers.shift_key()
                    && let Some(doc) = self.active_doc_mut()
                {
                    doc.delete_line();
                    self.doc_dirty = true;
                }
                true
            }
            _ => false,
        }
    }

    pub(crate) fn handle_list_key(&mut self, key: &Key) -> bool {
        // Search mode input editing
        if self.list_mode == ListMode::Search {
            match key {
                Key::Named(NamedKey::Backspace) => {
                    if self.search_cursor > 0 {
                        self.search_cursor -= 1;
                        self.search_query.remove(self.search_cursor);
                        let query = self.search_query.clone();
                        self.request_search(query);
                        self.list_dirty = true;
                    }
                    return true;
                }
                Key::Named(NamedKey::Delete) => {
                    if self.search_cursor < self.search_query.len() {
                        self.search_query.remove(self.search_cursor);
                        let query = self.search_query.clone();
                        self.request_search(query);
                        self.list_dirty = true;
                    }
                    return true;
                }
                Key::Named(NamedKey::Home) => {
                    self.search_cursor = 0;
                    return true;
                }
                Key::Named(NamedKey::End) => {
                    self.search_cursor = self.search_query.len();
                    return true;
                }
                Key::Named(NamedKey::ArrowLeft) => {
                    if self.search_cursor > 0 {
                        self.search_cursor -= 1;
                    }
                    return true;
                }
                Key::Named(NamedKey::ArrowRight) => {
                    if self.search_cursor < self.search_query.len() {
                        self.search_cursor += 1;
                    }
                    return true;
                }
                Key::Named(NamedKey::Escape) => {
                    self.list_mode = ListMode::Files;
                    self.list_dirty = true;
                    return true;
                }
                Key::Named(NamedKey::Enter) => {
                    let query = self.search_query.clone();
                    self.request_search(query);
                    self.list_dirty = true;
                    return true;
                }
                _ => {}
            }
        }

        match key {
            Key::Named(NamedKey::ArrowDown) => {
                if self.selected_item_idx + 1 < self.flat_items.len() {
                    self.selected_item_idx += 1;
                    // Preview file on arrow navigation
                    if !self.flat_items[self.selected_item_idx].is_folder {
                        let path = self.flat_items[self.selected_item_idx].path.clone();
                        self.open_file(&path);
                    }
                }
                self.list_dirty = true;
                true
            }
            Key::Named(NamedKey::ArrowUp) => {
                if self.selected_item_idx > 0 {
                    self.selected_item_idx -= 1;
                    if !self.flat_items[self.selected_item_idx].is_folder {
                        let path = self.flat_items[self.selected_item_idx].path.clone();
                        self.open_file(&path);
                    }
                }
                self.list_dirty = true;
                true
            }
            Key::Named(NamedKey::Enter) => {
                if self.selected_item_idx < self.flat_items.len() {
                    if self.flat_items[self.selected_item_idx].is_folder {
                        self.toggle_folder_for_item(self.selected_item_idx);
                        self.list_dirty = true;
                    } else {
                        let path = self.flat_items[self.selected_item_idx].path.clone();
                        self.open_file(&path);
                        self.focus = FocusPanel::Document;
                    }
                }
                true
            }
            Key::Named(NamedKey::F2) => {
                // Rename: switch to console with rename command pre-filled
                if self.selected_item_idx < self.flat_items.len()
                    && !self.flat_items[self.selected_item_idx].is_folder
                {
                    let rel_path = self.flat_items[self.selected_item_idx]
                        .path
                        .strip_prefix(&self.root_path)
                        .unwrap_or(&self.flat_items[self.selected_item_idx].path)
                        .display()
                        .to_string();
                    self.focus = FocusPanel::Console;
                    self.console.input = format!("rename {} ", rel_path);
                    self.console.input_cursor = self.console.input.len();
                    self.console_dirty = true;
                }
                true
            }
            Key::Named(NamedKey::Delete) => {
                // Delete selected file
                if self.selected_item_idx < self.flat_items.len()
                    && !self.flat_items[self.selected_item_idx].is_folder
                {
                    let path = self.flat_items[self.selected_item_idx].path.clone();
                    self.delete_path(&path);
                }
                true
            }
            Key::Named(NamedKey::Escape) => {
                if self.list_mode == ListMode::Search {
                    self.list_mode = ListMode::Files;
                    self.list_dirty = true;
                }
                true
            }
            _ => false,
        }
    }

    pub(crate) fn handle_console_key(&mut self, key: &Key) -> bool {
        match key {
            Key::Named(NamedKey::Enter) => {
                let root = self.root_path.clone();
                let action = self.console.execute(&root, &mut self.open_docs);
                if let Some(action) = action {
                    self.handle_command_action(action);
                }
                self.console_dirty = true;
                true
            }
            Key::Named(NamedKey::ArrowUp) => {
                self.console.history_up();
                self.console_dirty = true;
                true
            }
            Key::Named(NamedKey::ArrowDown) => {
                self.console.history_down();
                self.console_dirty = true;
                true
            }
            Key::Named(NamedKey::Backspace) => {
                if self.console.input_cursor > 0 {
                    self.console.input_cursor -= 1;
                    self.console.input.remove(self.console.input_cursor);
                    self.console_dirty = true;
                }
                true
            }
            Key::Named(NamedKey::Delete) => {
                if self.console.input_cursor < self.console.input.len() {
                    self.console.input.remove(self.console.input_cursor);
                    self.console_dirty = true;
                }
                true
            }
            Key::Named(NamedKey::Home) => {
                self.console.input_cursor = 0;
                self.console_dirty = true;
                true
            }
            Key::Named(NamedKey::End) => {
                self.console.input_cursor = self.console.input.len();
                self.console_dirty = true;
                true
            }
            Key::Named(NamedKey::ArrowLeft) => {
                if self.console.input_cursor > 0 {
                    self.console.input_cursor -= 1;
                    self.console_dirty = true;
                }
                true
            }
            Key::Named(NamedKey::ArrowRight) => {
                if self.console.input_cursor < self.console.input.len() {
                    self.console.input_cursor += 1;
                    self.console_dirty = true;
                }
                true
            }
            Key::Named(NamedKey::Escape) => {
                self.focus = FocusPanel::Document;
                true
            }
            _ => false,
        }
    }
}
