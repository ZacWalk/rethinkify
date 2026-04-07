//! Main `App` struct implementing winit `ApplicationHandler`. Owns the window, surface,
//! font system, and all editor state. Drives the event loop, layout, panel focus, and
//! coordinates rendering, input, menus, console commands, file tree, and search.

use crate::background::{BgEvent, BgWork, BgWorker};
use crate::commands::{CommandAction, ConsoleLine, ConsoleState, copy_dir_recursive};
use crate::file_tree::{FileTreeNode, FlatItem, flatten_tree};
use crate::search::SearchGroup;
use crate::views::ViewMode;
use cosmic_text::{Buffer, FontSystem, Metrics, SwashCache};
use muda::{
    Menu, MenuEvent, MenuItem, PredefinedMenuItem, Submenu,
    accelerator::{Accelerator, Code, Modifiers},
};
use softbuffer::{Context, Surface};
use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};
use std::rc::Rc;
use winit::{
    application::ApplicationHandler,
    dpi::{PhysicalPosition, PhysicalSize},
    event::{ElementState, MouseButton, MouseScrollDelta, WindowEvent},
    event_loop::{ActiveEventLoop, EventLoop, EventLoopProxy},
    keyboard::{Key, ModifiersState, NamedKey},
    raw_window_handle::{HasWindowHandle, RawWindowHandle},
    window::{CursorIcon, Window, WindowId},
};

use crate::config::AppConfig;
use crate::document::{Document, TextLocation};

// ─── View mode & focus ──────────────────────────────────────────────────────

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ListMode {
    Files,
    Search,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FocusPanel {
    List,
    Document,
    Console,
}

// ─── Main App ───────────────────────────────────────────────────────────────

pub struct App {
    pub(crate) window: Option<Rc<Window>>,
    pub(crate) context: Option<Context<Rc<Window>>>,
    pub(crate) surface: Option<Surface<Rc<Window>, Rc<Window>>>,
    pub(crate) menu: Option<Menu>,
    pub(crate) font_system: Option<FontSystem>,
    pub(crate) swash_cache: Option<SwashCache>,

    // State
    pub(crate) view_mode: ViewMode,
    pub(crate) list_mode: ListMode,
    pub(crate) focus: FocusPanel,
    pub(crate) modifiers: ModifiersState,

    // Layout
    pub(crate) panel_ratio: f32,
    pub(crate) console_ratio: f32,
    pub(crate) is_dragging_panel: bool,
    pub(crate) is_dragging_console: bool,
    pub(crate) is_dragging_scrollbar: bool,
    pub(crate) is_dragging_list_scrollbar: bool,
    pub(crate) is_selecting_text: bool,
    pub(crate) scrollbar_drag_offset: f32,
    pub(crate) scrollbar_hover: bool,
    pub(crate) list_scrollbar_hover: bool,
    pub(crate) cursor_pos: PhysicalPosition<f64>,
    pub(crate) scale_factor: f64,

    // File tree
    pub(crate) root_path: PathBuf,
    pub(crate) tree: FileTreeNode,
    pub(crate) flat_items: Vec<FlatItem>,
    pub(crate) selected_item_idx: usize,
    pub(crate) file_scroll: usize,

    // Documents
    pub(crate) open_docs: HashMap<PathBuf, Document>,
    pub(crate) active_doc_path: Option<PathBuf>,
    pub(crate) new_doc_counter: usize,

    // Search
    pub(crate) search_query: String,
    pub(crate) search_groups: Vec<SearchGroup>,
    pub(crate) search_selected_idx: usize,
    pub(crate) search_cursor: usize,

    // Console
    pub(crate) console: ConsoleState,

    // Display state
    pub(crate) word_wrap: bool,
    pub(crate) rendered_doc_lines: usize,
    pub(crate) char_width: f32,
    pub(crate) line_height: f32,
    pub(crate) list_line_height: f32,
    pub(crate) list_char_width: f32,
    pub(crate) console_char_width: f32,
    pub(crate) console_line_height: f32,

    // Config
    pub(crate) config: AppConfig,

    // Cosmic-text buffers
    pub(crate) doc_buffer: Option<Buffer>,
    pub(crate) list_buffer: Option<Buffer>,
    pub(crate) console_buffer: Option<Buffer>,
    pub(crate) line_num_buffer: Option<Buffer>,

    // Clipboard
    pub(crate) clipboard: Option<arboard::Clipboard>,

    // Dirty flags
    pub(crate) doc_dirty: bool,
    pub(crate) list_dirty: bool,
    pub(crate) console_dirty: bool,

    // Syntax highlighting cache (cookie after each line, window-scoped)
    pub(crate) syntax_cookies: Vec<u32>,

    // Full-document syntax cookies for computing initial_cookie efficiently
    pub(crate) full_syntax_cookies: Vec<u32>,

    // Windowed buffer: first document line present in the cosmic-text Buffer
    pub(crate) doc_window_start: usize,

    // Double-click tracking
    pub(crate) last_click_time: std::time::Instant,
    pub(crate) last_click_pos: PhysicalPosition<f64>,

    // Caret blink
    pub(crate) caret_visible: bool,
    pub(crate) caret_last_toggle: std::time::Instant,

    // Per-document view mode memory
    pub(crate) saved_view_modes: HashMap<PathBuf, ViewMode>,

    // HWND for context menus
    pub(crate) hwnd: isize,

    // Background worker
    pub(crate) bg_worker: Option<BgWorker>,
    pub(crate) search_seq: u64,
    pub(crate) pending_open_path: Option<PathBuf>,

    // Event loop proxy for spawning the worker
    pub(crate) event_proxy: Option<EventLoopProxy<BgEvent>>,
}

impl App {
    pub fn new() -> Self {
        let config = AppConfig::load();
        let root = config.root_folder.clone();
        let tree = FileTreeNode::empty(&root);

        let clip = arboard::Clipboard::new().ok();

        Self {
            window: None,
            context: None,
            surface: None,
            menu: None,
            font_system: None,
            swash_cache: None,

            view_mode: ViewMode::EditText,
            list_mode: ListMode::Files,
            focus: FocusPanel::List,
            modifiers: ModifiersState::empty(),

            panel_ratio: config.panel_ratio,
            console_ratio: config.console_ratio,
            is_dragging_panel: false,
            is_dragging_console: false,
            is_dragging_scrollbar: false,
            is_dragging_list_scrollbar: false,
            is_selecting_text: false,
            scrollbar_drag_offset: 0.0,
            scrollbar_hover: false,
            list_scrollbar_hover: false,
            cursor_pos: PhysicalPosition::new(0.0, 0.0),
            scale_factor: 1.0,

            root_path: root,
            tree,
            flat_items: Vec::new(),
            selected_item_idx: 0,
            file_scroll: 0,

            open_docs: HashMap::new(),
            active_doc_path: None,
            new_doc_counter: 0,

            search_query: String::new(),
            search_groups: Vec::new(),
            search_selected_idx: 0,
            search_cursor: 0,

            console: ConsoleState::new(),

            word_wrap: config.word_wrap,
            rendered_doc_lines: 0,
            char_width: 8.0,
            line_height: 18.0,
            list_line_height: 17.0,
            list_char_width: 7.0,
            console_char_width: 8.0,
            console_line_height: 18.0,

            config,

            doc_buffer: None,
            list_buffer: None,
            console_buffer: None,
            line_num_buffer: None,

            clipboard: clip,

            doc_dirty: true,
            list_dirty: true,
            console_dirty: true,

            syntax_cookies: Vec::new(),
            full_syntax_cookies: Vec::new(),
            doc_window_start: 0,

            last_click_time: std::time::Instant::now(),
            last_click_pos: PhysicalPosition::new(0.0, 0.0),

            caret_visible: true,
            caret_last_toggle: std::time::Instant::now(),

            saved_view_modes: HashMap::new(),
            hwnd: 0,
            bg_worker: None,
            search_seq: 0,
            pending_open_path: None,
            event_proxy: None,
        }
    }

    pub(crate) fn dpi(&self, px: f32) -> u32 {
        (px * self.scale_factor as f32).round().max(1.0) as u32
    }

    /// Ensure background worker is running and return a reference.
    fn ensure_bg_worker(&mut self) {
        if self.bg_worker.is_none()
            && let Some(proxy) = self.event_proxy.clone()
        {
            self.bg_worker = Some(BgWorker::spawn(proxy));
        }
    }

    /// Request a background directory scan. Result arrives via `BgEvent::TreeReady`.
    pub(crate) fn request_tree_scan(&mut self) {
        self.ensure_bg_worker();
        if let Some(w) = &self.bg_worker {
            w.send(BgWork::ScanTree(self.root_path.clone()));
        }
    }

    /// Request a background file load. Result arrives via `BgEvent::FileLoaded`.
    pub(crate) fn request_file_load(&mut self, path: PathBuf) {
        self.ensure_bg_worker();
        if let Some(w) = &self.bg_worker {
            w.send(BgWork::LoadFile(path));
        }
    }

    /// Request a background search. Result arrives via `BgEvent::SearchDone`.
    pub(crate) fn request_search(&mut self, query: String) {
        self.ensure_bg_worker();
        self.search_seq += 1;
        if let Some(w) = &self.bg_worker {
            w.send(BgWork::Search {
                root: self.root_path.clone(),
                query,
                seq: self.search_seq,
            });
        }
    }

    pub(crate) fn rebuild_flat_items(&mut self) {
        self.flat_items.clear();
        flatten_tree(&self.tree, 0, &mut self.flat_items, &self.open_docs);
    }

    pub(crate) fn active_doc(&self) -> Option<&Document> {
        self.active_doc_path
            .as_ref()
            .and_then(|p| self.open_docs.get(p))
    }

    pub(crate) fn active_doc_mut(&mut self) -> Option<&mut Document> {
        self.active_doc_path
            .as_ref()
            .and_then(|p| self.open_docs.get_mut(p))
    }

    pub(crate) fn open_file(&mut self, path: &PathBuf) {
        // Save current document's view mode
        if let Some(prev_path) = &self.active_doc_path {
            self.saved_view_modes
                .insert(prev_path.clone(), self.view_mode);
        }

        if self.open_docs.contains_key(path) {
            // Already loaded — just activate
            self.activate_loaded_doc(path);
        } else {
            // Request background load and remember what we're waiting for
            self.pending_open_path = Some(path.clone());
            self.request_file_load(path.clone());
        }
    }

    /// Activate a document that is already in `open_docs`.
    pub(crate) fn activate_loaded_doc(&mut self, path: &PathBuf) {
        self.active_doc_path = Some(path.clone());

        // Force full buffer rebuild for the newly active document
        if let Some(doc) = self.open_docs.get_mut(path) {
            doc.dirty_from = Some(0);
        }

        // Restore saved view mode, or auto-detect
        if let Some(&saved_mode) = self.saved_view_modes.get(path) {
            self.view_mode = saved_mode;
        } else {
            let ext = path.extension().and_then(|s| s.to_str()).unwrap_or("");
            match ext {
                "md" | "markdown" => self.view_mode = ViewMode::MarkdownPreview,
                "csv" => self.view_mode = ViewMode::Csv,
                _ => {
                    if let Some(doc) = self.open_docs.get(path) {
                        if doc.is_binary {
                            self.view_mode = ViewMode::Hex;
                        } else {
                            self.view_mode = ViewMode::EditText;
                        }
                    }
                }
            }
        }

        self.doc_dirty = true;
        self.list_dirty = true;
        self.syntax_cookies.clear();
        self.full_syntax_cookies.clear();
        self.doc_window_start = 0;
    }

    pub(crate) fn new_document(&mut self) {
        self.new_doc_counter += 1;
        let name = if self.new_doc_counter == 1 {
            "new.md".to_string()
        } else {
            format!("new-{}.md", self.new_doc_counter)
        };
        let path = self.root_path.join(&name);
        let mut doc = Document::new();
        doc.path = Some(path.clone());
        doc.modified = true;
        self.open_docs.insert(path.clone(), doc);
        self.active_doc_path = Some(path);
        self.view_mode = ViewMode::EditText;
        self.doc_dirty = true;
        self.list_dirty = true;
    }

    pub(crate) fn save_active_doc(&mut self) {
        if let Some(path) = self.active_doc_path.clone()
            && let Some(doc) = self.open_docs.get_mut(&path)
        {
            match doc.save() {
                Ok(()) => {
                    self.console.lines.push(ConsoleLine {
                        text: format!("Saved {}", path.display()),
                        is_command: false,
                    });
                }
                Err(e) => {
                    self.console.lines.push(ConsoleLine {
                        text: format!("Error saving: {}", e),
                        is_command: false,
                    });
                }
            }
            self.console_dirty = true;
            self.list_dirty = true;
        }
    }

    pub(crate) fn save_all(&mut self) {
        let paths: Vec<PathBuf> = self.open_docs.keys().cloned().collect();
        for path in paths {
            if let Some(doc) = self.open_docs.get_mut(&path)
                && doc.modified
            {
                let _ = doc.save();
            }
        }
        self.list_dirty = true;
        self.console_dirty = true;
    }

    pub(crate) fn toggle_folder_for_item(&mut self, idx: usize) {
        if idx >= self.flat_items.len() {
            return;
        }
        let path = self.flat_items[idx].path.clone();
        fn toggle_in_tree(node: &mut FileTreeNode, target: &Path) -> bool {
            if node.path == target {
                node.toggle_expand();
                return true;
            }
            for child in &mut node.children {
                if toggle_in_tree(child, target) {
                    return true;
                }
            }
            false
        }
        toggle_in_tree(&mut self.tree, &path);
        self.rebuild_flat_items();
    }

    pub(crate) fn handle_command_action(&mut self, action: CommandAction) {
        match action {
            CommandAction::NewFile => self.new_document(),
            CommandAction::Save => self.save_active_doc(),
            CommandAction::SaveAs => {
                if let Some(path) = rfd::FileDialog::new().save_file()
                    && let Some(doc_path) = self.active_doc_path.clone()
                    && let Some(doc) = self.open_docs.get_mut(&doc_path)
                {
                    let _ = doc.save_as(path);
                }
            }
            CommandAction::SaveAll => self.save_all(),
            CommandAction::Exit => {
                // Handle via event loop exit
            }
            CommandAction::Undo => {
                if let Some(doc) = self.active_doc_mut() {
                    doc.undo();
                    self.doc_dirty = true;
                }
            }
            CommandAction::Redo => {
                if let Some(doc) = self.active_doc_mut() {
                    doc.redo();
                    self.doc_dirty = true;
                }
            }
            CommandAction::Search(query) => {
                self.list_mode = ListMode::Search;
                self.search_query = query.clone();
                self.request_search(query);
                self.list_dirty = true;
            }
            CommandAction::ReformatJson => {
                if let Some(doc) = self.active_doc_mut() {
                    doc.reformat_json();
                    self.doc_dirty = true;
                }
            }
            CommandAction::SortRemoveDuplicates => {
                if let Some(doc) = self.active_doc_mut() {
                    doc.sort_remove_duplicates();
                    self.doc_dirty = true;
                }
            }
            CommandAction::ToggleWordWrap => {
                self.word_wrap = !self.word_wrap;
                self.doc_dirty = true;
            }
            CommandAction::ToggleMarkdown => {
                self.view_mode = if self.view_mode == ViewMode::MarkdownPreview {
                    ViewMode::EditText
                } else {
                    ViewMode::MarkdownPreview
                };
                self.doc_dirty = true;
            }
            CommandAction::RefreshFolder => {
                self.request_tree_scan();
            }
            CommandAction::DeletePath(path) => {
                self.delete_path(&path);
            }
            CommandAction::RenamePath(path, new_name) => {
                self.rename_path(&path, &new_name);
            }
            CommandAction::CopyPath(src, dst) => {
                self.copy_path(&src, &dst);
            }
            CommandAction::MovePath(src, dst) => {
                self.move_path(&src, &dst);
            }
        }
    }

    // ─── File management operations ─────────────────────────────────────

    pub(crate) fn delete_path(&mut self, path: &Path) {
        if !path.starts_with(&self.root_path) {
            self.console.lines.push(ConsoleLine {
                text: "Path must stay within the current root folder.".into(),
                is_command: false,
            });
            self.console_dirty = true;
            return;
        }
        if !path.exists() {
            self.console.lines.push(ConsoleLine {
                text: "Path not found.".into(),
                is_command: false,
            });
            self.console_dirty = true;
            return;
        }

        let was_active = self
            .active_doc_path
            .as_ref()
            .is_some_and(|p| p == path || p.starts_with(path));

        let result = if path.is_dir() {
            fs::remove_dir_all(path)
        } else {
            fs::remove_file(path)
        };

        match result {
            Ok(()) => {
                let display = path.strip_prefix(&self.root_path).unwrap_or(path).display();
                self.console.lines.push(ConsoleLine {
                    text: format!("Deleted {}.", display),
                    is_command: false,
                });
                // Remove from open docs
                self.open_docs.remove(&path.to_path_buf());
                if was_active {
                    self.active_doc_path = None;
                }
                self.request_tree_scan();
                self.doc_dirty = true;
            }
            Err(e) => {
                self.console.lines.push(ConsoleLine {
                    text: format!("Failed to delete: {}", e),
                    is_command: false,
                });
            }
        }
        self.console_dirty = true;
    }

    pub(crate) fn rename_path(&mut self, source: &Path, new_name: &str) {
        if !source.starts_with(&self.root_path) {
            self.console.lines.push(ConsoleLine {
                text: "Path must stay within the current root folder.".into(),
                is_command: false,
            });
            self.console_dirty = true;
            return;
        }
        if new_name.is_empty() || new_name.contains('/') || new_name.contains('\\') {
            self.console.lines.push(ConsoleLine {
                text: "New name must not contain path separators.".into(),
                is_command: false,
            });
            self.console_dirty = true;
            return;
        }
        let dest = source.parent().unwrap_or(&self.root_path).join(new_name);
        if dest.exists() {
            self.console.lines.push(ConsoleLine {
                text: "Destination already exists.".into(),
                is_command: false,
            });
            self.console_dirty = true;
            return;
        }
        match fs::rename(source, &dest) {
            Ok(()) => {
                let src_display = source
                    .strip_prefix(&self.root_path)
                    .unwrap_or(source)
                    .display();
                let dst_display = dest
                    .strip_prefix(&self.root_path)
                    .unwrap_or(&dest)
                    .display();
                self.console.lines.push(ConsoleLine {
                    text: format!("Renamed {} to {}.", src_display, dst_display),
                    is_command: false,
                });
                // Update open docs reference
                if let Some(doc) = self.open_docs.remove(&source.to_path_buf()) {
                    let mut doc = doc;
                    doc.path = Some(dest.clone());
                    self.open_docs.insert(dest.clone(), doc);
                    if self.active_doc_path.as_deref() == Some(source) {
                        self.active_doc_path = Some(dest);
                    }
                }
                self.request_tree_scan();
                self.doc_dirty = true;
            }
            Err(e) => {
                self.console.lines.push(ConsoleLine {
                    text: format!("Failed to rename: {}", e),
                    is_command: false,
                });
            }
        }
        self.console_dirty = true;
    }

    pub(crate) fn copy_path(&mut self, src: &Path, dst: &Path) {
        if !src.starts_with(&self.root_path) || !dst.starts_with(&self.root_path) {
            self.console.lines.push(ConsoleLine {
                text: "Paths must stay within the current root folder.".into(),
                is_command: false,
            });
            self.console_dirty = true;
            return;
        }
        if dst.exists() {
            self.console.lines.push(ConsoleLine {
                text: "Destination already exists.".into(),
                is_command: false,
            });
            self.console_dirty = true;
            return;
        }
        let result = if src.is_dir() {
            copy_dir_recursive(src, dst)
        } else {
            fs::copy(src, dst).map(|_| ())
        };
        match result {
            Ok(()) => {
                let s = src.strip_prefix(&self.root_path).unwrap_or(src).display();
                let d = dst.strip_prefix(&self.root_path).unwrap_or(dst).display();
                self.console.lines.push(ConsoleLine {
                    text: format!("Copied {} to {}.", s, d),
                    is_command: false,
                });
                self.request_tree_scan();
            }
            Err(e) => {
                self.console.lines.push(ConsoleLine {
                    text: format!("Failed to copy: {}", e),
                    is_command: false,
                });
            }
        }
        self.console_dirty = true;
    }

    pub(crate) fn move_path(&mut self, src: &Path, dst: &Path) {
        if !src.starts_with(&self.root_path) || !dst.starts_with(&self.root_path) {
            self.console.lines.push(ConsoleLine {
                text: "Paths must stay within the current root folder.".into(),
                is_command: false,
            });
            self.console_dirty = true;
            return;
        }
        if dst.exists() {
            self.console.lines.push(ConsoleLine {
                text: "Destination already exists.".into(),
                is_command: false,
            });
            self.console_dirty = true;
            return;
        }
        match fs::rename(src, dst) {
            Ok(()) => {
                let s = src.strip_prefix(&self.root_path).unwrap_or(src).display();
                let d = dst.strip_prefix(&self.root_path).unwrap_or(dst).display();
                self.console.lines.push(ConsoleLine {
                    text: format!("Moved {} to {}.", s, d),
                    is_command: false,
                });
                // Update open doc reference
                if let Some(doc) = self.open_docs.remove(&src.to_path_buf()) {
                    let mut doc = doc;
                    doc.path = Some(dst.to_path_buf());
                    self.open_docs.insert(dst.to_path_buf(), doc);
                    if self.active_doc_path.as_deref() == Some(src) {
                        self.active_doc_path = Some(dst.to_path_buf());
                    }
                }
                self.request_tree_scan();
                self.doc_dirty = true;
            }
            Err(e) => {
                self.console.lines.push(ConsoleLine {
                    text: format!("Failed to move: {}", e),
                    is_command: false,
                });
            }
        }
        self.console_dirty = true;
    }

    pub(crate) fn create_new_file_at(&mut self, path: &Path) {
        // Avoid collision: append -2, -3, etc.
        let mut target = path.to_path_buf();
        let stem = path
            .file_stem()
            .unwrap_or_default()
            .to_string_lossy()
            .to_string();
        let ext = path
            .extension()
            .map(|e| format!(".{}", e.to_string_lossy()))
            .unwrap_or_default();
        let parent = path.parent().unwrap_or(&self.root_path);
        let mut counter = 2;
        while target.exists() {
            target = parent.join(format!("{}-{}{}", stem, counter, ext));
            counter += 1;
        }
        match fs::write(&target, "") {
            Ok(()) => {
                self.console.lines.push(ConsoleLine {
                    text: format!(
                        "Created {}.",
                        target
                            .strip_prefix(&self.root_path)
                            .unwrap_or(&target)
                            .display()
                    ),
                    is_command: false,
                });
                self.request_tree_scan();
                self.open_file(&target);
            }
            Err(e) => {
                self.console.lines.push(ConsoleLine {
                    text: format!("Failed to create file: {}", e),
                    is_command: false,
                });
            }
        }
        self.console_dirty = true;
    }

    pub(crate) fn create_new_folder_at(&mut self, parent: &Path) {
        let mut target = parent.join("new-folder");
        let mut counter = 2;
        while target.exists() {
            target = parent.join(format!("new-folder-{}", counter));
            counter += 1;
        }
        match fs::create_dir(&target) {
            Ok(()) => {
                self.console.lines.push(ConsoleLine {
                    text: format!(
                        "Created folder {}.",
                        target
                            .strip_prefix(&self.root_path)
                            .unwrap_or(&target)
                            .display()
                    ),
                    is_command: false,
                });
                self.request_tree_scan();
            }
            Err(e) => {
                self.console.lines.push(ConsoleLine {
                    text: format!("Failed to create folder: {}", e),
                    is_command: false,
                });
            }
        }
        self.console_dirty = true;
    }

    pub(crate) fn ensure_cursor_visible(&mut self) {
        let visible_lines = if let Some(window) = &self.window {
            let size = window.inner_size();
            let console_y = (size.height as f32 * self.console_ratio) as u32;
            (console_y as f32 / self.line_height) as usize
        } else {
            1
        };
        if let Some(doc) = self.active_doc_mut() {
            let cursor_y = doc.cursor.y;
            if cursor_y < doc.scroll_line {
                doc.scroll_line = cursor_y;
            } else if cursor_y >= doc.scroll_line + visible_lines {
                doc.scroll_line = cursor_y.saturating_sub(visible_lines.saturating_sub(1));
            }
            self.doc_dirty = true;
        }
    }

    pub(crate) fn update_window_title(&mut self) {
        if let Some(window) = &self.window {
            let title = if let Some(path) = &self.active_doc_path {
                let name = path.file_name().unwrap_or_default().to_string_lossy();
                let modified = self.open_docs.get(path).is_some_and(|d| d.modified);
                if modified {
                    format!("{}* — Rethinkify", name)
                } else {
                    format!("{} — Rethinkify", name)
                }
            } else {
                "Rethinkify".to_string()
            };
            window.set_title(&title);
        }
    }

    pub(crate) fn show_about(&mut self) {
        let mut doc = Document::new();
        doc.lines = vec![
            "# Rethinkify".into(),
            "".into(),
            "A lightweight text editor with multi-file search, markdown preview,".into(),
            "CSV table view, hex view, and a built-in console.".into(),
            "".into(),
            "## Keyboard Shortcuts".into(),
            "".into(),
            "| Shortcut | Action |".into(),
            "|---|---|".into(),
            "| Ctrl+N | New file |".into(),
            "| Ctrl+O | Open file |".into(),
            "| Ctrl+S | Save |".into(),
            "| Ctrl+Shift+S | Save all |".into(),
            "| Ctrl+Z | Undo |".into(),
            "| Ctrl+Y | Redo |".into(),
            "| Ctrl+C | Copy (line if no selection) |".into(),
            "| Ctrl+X | Cut |".into(),
            "| Ctrl+V | Paste |".into(),
            "| Ctrl+A | Select all |".into(),
            "| Ctrl+D | Duplicate line |".into(),
            "| Ctrl+Shift+K | Delete line |".into(),
            "| Ctrl+Left/Right | Word navigation |".into(),
            "| Ctrl+Shift+Left/Right | Word selection |".into(),
            "| Ctrl+Home/End | Document start/end |".into(),
            "| Home | Smart home (toggle indent/col 0) |".into(),
            "| Ctrl+M | Toggle markdown preview |".into(),
            "| Ctrl+R | Reformat JSON |".into(),
            "| Ctrl+Shift+F | Search in files |".into(),
            "| Alt+Z | Toggle word wrap |".into(),
            "| F1 | About / Help |".into(),
            "| F5 | Refresh folder |".into(),
            "| F8 / Shift+F8 | Next/Previous search result |".into(),
            "| Tab / Shift+Tab | Indent / Unindent |".into(),
            "| Ctrl+Wheel | Zoom font size |".into(),
            "".into(),
            "## Console Commands".into(),
            "".into(),
            "| Command | Description |".into(),
            "|---|---|".into(),
            "| help | Show available commands |".into(),
            "| new | Create new file |".into(),
            "| open <file> | Open a file |".into(),
            "| save / saveas | Save current file |".into(),
            "| saveall | Save all modified files |".into(),
            "| find <text> | Search in files |".into(),
            "| reformat | Reformat JSON |".into(),
            "| sort | Sort and remove duplicate lines |".into(),
            "| wordwrap | Toggle word wrap |".into(),
            "| markdown | Toggle markdown preview |".into(),
            "| refresh | Refresh file tree |".into(),
            "| tree | Show directory tree |".into(),
            "| echo <text> | Print text |".into(),
            "| calc <expr> | Evaluate arithmetic |".into(),
            "| cp <src> <dst> | Copy file or folder |".into(),
            "| mv <src> <dst> | Move file or folder |".into(),
            "| rename <path> <name> | Rename file |".into(),
            "| rm <path> | Delete file or folder |".into(),
            "| exit | Exit application |".into(),
            "".into(),
            "Output redirection: command > file, command >> file".into(),
            "".into(),
            "Press Escape to close.".into(),
        ];
        doc.read_only = true;
        let path = PathBuf::from("__about__");
        self.open_docs.insert(path.clone(), doc);
        self.active_doc_path = Some(path);
        self.view_mode = ViewMode::MarkdownPreview;
        self.doc_dirty = true;
    }

    pub(crate) fn navigate_search_result(&mut self, forward: bool) {
        let total: usize = self.search_groups.iter().map(|g| g.results.len()).sum();
        if total == 0 {
            return;
        }
        if forward {
            self.search_selected_idx = (self.search_selected_idx + 1) % total;
        } else {
            self.search_selected_idx = if self.search_selected_idx == 0 {
                total - 1
            } else {
                self.search_selected_idx - 1
            };
        }
        // Find the result at search_selected_idx without allocating
        let mut remaining = self.search_selected_idx;
        for group in &self.search_groups {
            if remaining < group.results.len() {
                let result = &group.results[remaining];
                let path = result.path.clone();
                let line = result.line_number.saturating_sub(1);
                let col = result.match_start;
                let len = result.match_len;
                self.open_file(&path);
                if let Some(doc) = self.active_doc_mut() {
                    doc.cursor = TextLocation { y: line, x: col };
                    doc.anchor = TextLocation {
                        y: line,
                        x: col + len,
                    };
                    doc.scroll_line = line.saturating_sub(5);
                }
                break;
            }
            remaining -= group.results.len();
        }
        self.focus = FocusPanel::Document;
        self.view_mode = ViewMode::EditText;
        self.doc_dirty = true;
        self.list_dirty = true;
    }

    /// Handle a click on a specific line in the search results list.
    pub(crate) fn handle_search_click(&mut self, line: usize) {
        // Line layout: 0=search input header, 1=result count, 2=blank,
        // then for each group: 1 header line + N result lines (if expanded).
        if line < 3 {
            return;
        }
        let mut current_line = 3usize;
        let mut result_idx = 0usize;
        for group in &self.search_groups {
            if line == current_line {
                // Clicked on group header — no action for now
                return;
            }
            current_line += 1;
            if group.expanded {
                for result in &group.results {
                    if line == current_line {
                        self.search_selected_idx = result_idx;
                        let path = result.path.clone();
                        let ln = result.line_number.saturating_sub(1);
                        let col = result.match_start;
                        let len = result.match_len;
                        self.open_file(&path);
                        if let Some(doc) = self.active_doc_mut() {
                            doc.cursor = TextLocation { y: ln, x: col };
                            doc.anchor = TextLocation {
                                y: ln,
                                x: col + len,
                            };
                            doc.scroll_line = ln.saturating_sub(5);
                        }
                        self.view_mode = ViewMode::EditText;
                        self.doc_dirty = true;
                        self.list_dirty = true;
                        return;
                    }
                    current_line += 1;
                    result_idx += 1;
                }
            } else {
                result_idx += group.results.len();
            }
        }
    }
}

impl ApplicationHandler<BgEvent> for App {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        if self.window.is_some() {
            return;
        }

        let attributes = Window::default_attributes()
            .with_title("Rethinkify")
            .with_inner_size(PhysicalSize::new(
                self.config.window_width,
                self.config.window_height,
            ));
        let window = Rc::new(event_loop.create_window(attributes).unwrap());

        // Set up menu
        let mut handle_isize = 0isize;
        #[cfg(target_os = "windows")]
        if let Ok(handle) = window.window_handle()
            && let RawWindowHandle::Win32(hwnd) = handle.as_raw()
        {
            handle_isize = hwnd.hwnd.get();
        }
        #[cfg(target_os = "macos")]
        if let Ok(handle) = window.window_handle()
            && let RawWindowHandle::AppKit(appkit) = handle.as_raw()
        {
            handle_isize = appkit.ns_view.as_ptr() as isize;
        }

        self.hwnd = handle_isize;

        let menu = Menu::new();

        let file_menu = Submenu::new("File", true);
        file_menu
            .append(&MenuItem::with_id(
                "new",
                "New\tCtrl+N",
                true,
                Some(Accelerator::new(Some(Modifiers::CONTROL), Code::KeyN)),
            ))
            .unwrap();
        file_menu
            .append(&MenuItem::with_id(
                "open_file",
                "Open File\tCtrl+O",
                true,
                Some(Accelerator::new(Some(Modifiers::CONTROL), Code::KeyO)),
            ))
            .unwrap();
        file_menu
            .append(&MenuItem::with_id(
                "open_folder",
                "Open Folder...",
                true,
                None,
            ))
            .unwrap();

        // Open Recent Root Folder submenu
        if !self.config.recent_root_folders.is_empty() {
            let recent_menu = Submenu::new("Open Recent Root Folder", true);
            for (i, folder) in self.config.recent_root_folders.iter().enumerate() {
                let name = folder
                    .file_name()
                    .unwrap_or_default()
                    .to_string_lossy()
                    .to_string();
                let _ = recent_menu.append(&MenuItem::with_id(
                    format!("recent_root_{}", i),
                    &name,
                    folder.is_dir(),
                    None,
                ));
            }
            file_menu.append(&recent_menu).unwrap();
        }

        file_menu.append(&PredefinedMenuItem::separator()).unwrap();
        file_menu
            .append(&MenuItem::with_id(
                "save",
                "Save\tCtrl+S",
                true,
                Some(Accelerator::new(Some(Modifiers::CONTROL), Code::KeyS)),
            ))
            .unwrap();
        file_menu
            .append(&MenuItem::with_id("save_as", "Save As...", true, None))
            .unwrap();
        file_menu
            .append(&MenuItem::with_id(
                "save_all",
                "Save All\tCtrl+Shift+S",
                true,
                Some(Accelerator::new(
                    Some(Modifiers::CONTROL | Modifiers::SHIFT),
                    Code::KeyS,
                )),
            ))
            .unwrap();
        file_menu.append(&PredefinedMenuItem::separator()).unwrap();
        file_menu
            .append(&MenuItem::with_id("quit", "Exit", true, None))
            .unwrap();
        menu.append(&file_menu).unwrap();

        let edit_menu = Submenu::new("Edit", true);
        edit_menu
            .append(&MenuItem::with_id("undo", "Undo\tCtrl+Z", true, None))
            .unwrap();
        edit_menu
            .append(&MenuItem::with_id("redo", "Redo\tCtrl+Y", true, None))
            .unwrap();
        edit_menu.append(&PredefinedMenuItem::separator()).unwrap();
        edit_menu
            .append(&MenuItem::with_id("cut", "Cut\tCtrl+X", true, None))
            .unwrap();
        edit_menu
            .append(&MenuItem::with_id("copy", "Copy\tCtrl+C", true, None))
            .unwrap();
        edit_menu
            .append(&MenuItem::with_id("paste", "Paste\tCtrl+V", true, None))
            .unwrap();
        edit_menu.append(&PredefinedMenuItem::separator()).unwrap();
        edit_menu
            .append(&MenuItem::with_id(
                "select_all",
                "Select All\tCtrl+A",
                true,
                None,
            ))
            .unwrap();
        edit_menu.append(&PredefinedMenuItem::separator()).unwrap();
        edit_menu
            .append(&MenuItem::with_id(
                "reformat",
                "Reformat JSON\tCtrl+R",
                true,
                None,
            ))
            .unwrap();
        edit_menu
            .append(&MenuItem::with_id(
                "sort",
                "Sort & Remove Duplicates",
                true,
                None,
            ))
            .unwrap();
        menu.append(&edit_menu).unwrap();

        let view_menu = Submenu::new("View", true);
        view_menu
            .append(&MenuItem::with_id(
                "word_wrap",
                "Toggle Word Wrap\tAlt+Z",
                true,
                None,
            ))
            .unwrap();
        view_menu
            .append(&MenuItem::with_id(
                "markdown",
                "Toggle Markdown\tCtrl+M",
                true,
                None,
            ))
            .unwrap();
        view_menu.append(&PredefinedMenuItem::separator()).unwrap();
        view_menu
            .append(&MenuItem::with_id(
                "search",
                "Search in Files\tCtrl+Shift+F",
                true,
                None,
            ))
            .unwrap();
        view_menu
            .append(&MenuItem::with_id(
                "refresh",
                "Refresh Folder\tF5",
                true,
                None,
            ))
            .unwrap();
        menu.append(&view_menu).unwrap();

        let help_menu = Submenu::new("Help", true);
        help_menu
            .append(&MenuItem::with_id("about", "About\tF1", true, None))
            .unwrap();
        menu.append(&help_menu).unwrap();

        #[cfg(target_os = "windows")]
        unsafe {
            menu.init_for_hwnd(handle_isize).unwrap();
        }
        #[cfg(target_os = "macos")]
        {
            menu.init_for_nsapp();
        }

        let context = Context::new(window.clone()).unwrap();
        let surface = Surface::new(&context, window.clone()).unwrap();

        let mut font_system = FontSystem::new();
        let swash_cache = SwashCache::new();
        let metrics = Metrics::new(self.config.font_size, self.config.font_size + 4.0);

        let doc_buffer = Buffer::new(&mut font_system, metrics);
        let list_buffer = Buffer::new(
            &mut font_system,
            Metrics::new(self.config.list_font_size, self.config.list_font_size + 4.0),
        );
        let console_buffer = Buffer::new(
            &mut font_system,
            Metrics::new(
                self.config.console_font_size,
                self.config.console_font_size + 4.0,
            ),
        );
        let line_num_buffer = Buffer::new(&mut font_system, metrics);

        self.scale_factor = window.scale_factor();
        self.window = Some(window);
        self.context = Some(context);
        self.surface = Some(surface);
        self.menu = Some(menu);
        self.font_system = Some(font_system);
        self.swash_cache = Some(swash_cache);

        self.doc_buffer = Some(doc_buffer);
        self.list_buffer = Some(list_buffer);
        self.console_buffer = Some(console_buffer);
        self.line_num_buffer = Some(line_num_buffer);

        // Open recent file if configured
        if let Some(recent) = self.config.recent_file.clone()
            && recent.exists()
        {
            self.open_file(&recent);
        }

        // Kick off initial background tree scan
        self.request_tree_scan();
    }

    fn window_event(&mut self, event_loop: &ActiveEventLoop, _id: WindowId, event: WindowEvent) {
        match event {
            WindowEvent::CloseRequested => {
                // Save config
                self.config.panel_ratio = self.panel_ratio;
                self.config.console_ratio = self.console_ratio;
                self.config.root_folder = self.root_path.clone();
                self.config.recent_file = self.active_doc_path.clone();
                self.config.word_wrap = self.word_wrap;
                if let Some(w) = &self.window {
                    let size = w.inner_size();
                    self.config.window_width = size.width;
                    self.config.window_height = size.height;
                }
                self.config.save();
                event_loop.exit();
            }

            WindowEvent::ModifiersChanged(mods) => {
                self.modifiers = mods.state();
            }

            WindowEvent::Resized(_) | WindowEvent::RedrawRequested => {
                // Check if the doc window needs rebuilding due to scroll
                if !self.doc_dirty && self.view_mode == ViewMode::EditText {
                    let scroll = self.active_doc().map(|d| d.scroll_line).unwrap_or(0);
                    let window_end = self.doc_window_start
                        + self.doc_buffer.as_ref().map_or(0, |b| b.lines.len());
                    if scroll < self.doc_window_start.saturating_add(50)
                        || scroll + 100 >= window_end
                    {
                        self.doc_dirty = true;
                    }
                }
                if self.doc_dirty {
                    self.update_doc_buffer();
                    self.doc_dirty = false;
                }
                if self.list_dirty {
                    self.update_list_buffer();
                    self.list_dirty = false;
                }
                if self.console_dirty {
                    self.update_console_buffer();
                    self.console_dirty = false;
                }
                self.render();
            }

            WindowEvent::CursorMoved { position, .. } => {
                self.cursor_pos = position;
                if let Some(window) = self.window.clone() {
                    let size = window.inner_size();
                    let hit_margin = 8.0 * self.scale_factor;
                    if self.is_dragging_panel {
                        self.panel_ratio =
                            (position.x / size.width as f64).clamp(0.05, 0.50) as f32;
                        window.request_redraw();
                    } else if self.is_dragging_console {
                        self.console_ratio =
                            (position.y / size.height as f64).clamp(0.30, 0.95) as f32;
                        window.request_redraw();
                    } else if self.is_dragging_scrollbar {
                        // Update scroll position while dragging doc scrollbar
                        let console_y = (size.height as f32 * self.console_ratio) as u32;
                        let doc_h = console_y;
                        let visible_lines = (doc_h as f32 / self.line_height) as usize;
                        let drag_offset = self.scrollbar_drag_offset;
                        let mouse_y = position.y as f32;
                        let rendered = self.rendered_doc_lines;
                        let is_edit = self.view_mode == ViewMode::EditText;
                        if let Some(doc) = self.active_doc_mut() {
                            let line_count = if is_edit { doc.line_count() } else { rendered };
                            let max_scroll = line_count.saturating_sub(visible_lines).max(1);
                            let thumb_h = ((visible_lines as f32 / line_count as f32)
                                * doc_h as f32)
                                .max(20.0);
                            let track_h = doc_h as f32 - thumb_h;
                            if track_h > 0.0 {
                                let new_thumb_y = (mouse_y - drag_offset).clamp(0.0, track_h);
                                let ratio = new_thumb_y / track_h;
                                doc.scroll_line = (ratio * max_scroll as f32).round() as usize;
                                doc.scroll_line = doc.scroll_line.min(max_scroll);
                            }
                        }
                        window.request_redraw();
                    } else if self.is_dragging_list_scrollbar {
                        // Update list scroll position while dragging list scrollbar
                        let panel_h = size.height as f32;
                        let list_total = self.flat_items.len() + 1;
                        let visible = (panel_h / self.list_line_height) as usize;
                        if list_total > visible {
                            let max_scroll = list_total.saturating_sub(visible).max(1);
                            let thumb_h =
                                ((visible as f32 / list_total as f32) * panel_h).max(20.0);
                            let track_h = panel_h - thumb_h;
                            if track_h > 0.0 {
                                let new_thumb_y = (position.y as f32 - self.scrollbar_drag_offset)
                                    .clamp(0.0, track_h);
                                let ratio = new_thumb_y / track_h;
                                self.file_scroll = (ratio * max_scroll as f32).round() as usize;
                                self.file_scroll = self.file_scroll.min(max_scroll);
                            }
                        }
                        self.list_dirty = true;
                        window.request_redraw();
                    } else if self.is_selecting_text {
                        // Update selection while dragging
                        if let Some(loc) = self.doc_pos_from_mouse(position.x, position.y)
                            && let Some(doc) = self.active_doc_mut()
                        {
                            doc.set_cursor(loc, true);
                            self.doc_dirty = true;
                        }
                        window.request_redraw();
                    } else {
                        // Update cursor icon and scrollbar hover
                        let panel_x = size.width as f64 * self.panel_ratio as f64;
                        let console_y_pos = size.height as f64 * self.console_ratio as f64;
                        let scrollbar_w = self.dpi(14.0) as f64;
                        let doc_x = panel_x + self.dpi(6.0) as f64;
                        let doc_right = size.width as f64;
                        let sb_x = doc_right - scrollbar_w;

                        let was_hover = self.scrollbar_hover;
                        self.scrollbar_hover = position.x >= sb_x
                            && position.x <= doc_right
                            && position.x > doc_x
                            && position.y >= 0.0
                            && position.y < console_y_pos;

                        // List scrollbar hover
                        let list_sb_x = panel_x - scrollbar_w;
                        let was_list_hover = self.list_scrollbar_hover;
                        self.list_scrollbar_hover =
                            position.x >= list_sb_x && position.x < panel_x && position.y >= 0.0;

                        if (position.x - panel_x).abs() < hit_margin {
                            window.set_cursor(CursorIcon::EwResize);
                        } else if position.x > panel_x
                            && (position.y - console_y_pos).abs() < hit_margin
                        {
                            window.set_cursor(CursorIcon::NsResize);
                        } else {
                            window.set_cursor(CursorIcon::Default);
                        }
                        if self.scrollbar_hover != was_hover {
                            self.doc_dirty = true;
                        }
                        if self.list_scrollbar_hover != was_list_hover {
                            self.list_dirty = true;
                        }
                        window.request_redraw();
                    }
                }
            }

            WindowEvent::MouseInput {
                state,
                button: MouseButton::Left,
                ..
            } => {
                if let Some(window) = self.window.clone() {
                    let size = window.inner_size();
                    let panel_x = size.width as f64 * self.panel_ratio as f64;
                    let console_y_pos = size.height as f64 * self.console_ratio as f64;
                    let hit_margin = 8.0 * self.scale_factor;

                    if state == ElementState::Pressed {
                        // Splitter detection
                        if (self.cursor_pos.x - panel_x).abs() < hit_margin {
                            self.is_dragging_panel = true;
                            return;
                        }
                        if self.cursor_pos.x > panel_x
                            && (self.cursor_pos.y - console_y_pos).abs() < hit_margin
                        {
                            self.is_dragging_console = true;
                            return;
                        }

                        // Scrollbar click detection
                        if self.scrollbar_hover {
                            let doc_h = console_y_pos as f32;
                            let visible_lines = (doc_h / self.line_height) as usize;
                            let click_y = self.cursor_pos.y as f32;
                            let rendered = self.rendered_doc_lines;
                            let is_edit = self.view_mode == ViewMode::EditText;
                            if let Some(doc) = self.active_doc_mut() {
                                let line_count = if is_edit { doc.line_count() } else { rendered };
                                if line_count > visible_lines {
                                    let max_scroll =
                                        line_count.saturating_sub(visible_lines).max(1);
                                    let thumb_h = ((visible_lines as f32 / line_count as f32)
                                        * doc_h)
                                        .max(20.0);
                                    let track_h = doc_h - thumb_h;
                                    let thumb_y = if track_h > 0.0 {
                                        (doc.scroll_line as f32 / max_scroll as f32) * track_h
                                    } else {
                                        0.0
                                    };
                                    if click_y >= thumb_y && click_y < thumb_y + thumb_h {
                                        // Click on thumb: start drag
                                        self.scrollbar_drag_offset = click_y - thumb_y;
                                        self.is_dragging_scrollbar = true;
                                    } else {
                                        // Click above/below thumb: jump scroll
                                        let ratio = (click_y - thumb_h * 0.5).clamp(0.0, track_h)
                                            / track_h.max(1.0);
                                        doc.scroll_line =
                                            (ratio * max_scroll as f32).round() as usize;
                                        doc.scroll_line = doc.scroll_line.min(max_scroll);
                                        self.scrollbar_drag_offset = thumb_h * 0.5;
                                        self.is_dragging_scrollbar = true;
                                        self.doc_dirty = true;
                                    }
                                }
                            }
                            window.request_redraw();
                            return;
                        }

                        // List scrollbar click detection
                        if self.list_scrollbar_hover {
                            let panel_h = size.height as f32;
                            let list_total = self.flat_items.len() + 1;
                            let visible = (panel_h / self.list_line_height) as usize;
                            if list_total > visible {
                                let click_y = self.cursor_pos.y as f32;
                                let max_scroll = list_total.saturating_sub(visible).max(1);
                                let thumb_h =
                                    ((visible as f32 / list_total as f32) * panel_h).max(20.0);
                                let track_h = panel_h - thumb_h;
                                let thumb_y = if track_h > 0.0 {
                                    (self.file_scroll as f32 / max_scroll as f32) * track_h
                                } else {
                                    0.0
                                };
                                if click_y >= thumb_y && click_y < thumb_y + thumb_h {
                                    self.scrollbar_drag_offset = click_y - thumb_y;
                                    self.is_dragging_list_scrollbar = true;
                                } else {
                                    let ratio = (click_y - thumb_h * 0.5).clamp(0.0, track_h)
                                        / track_h.max(1.0);
                                    self.file_scroll = (ratio * max_scroll as f32).round() as usize;
                                    self.file_scroll = self.file_scroll.min(max_scroll);
                                    self.scrollbar_drag_offset = thumb_h * 0.5;
                                    self.is_dragging_list_scrollbar = true;
                                    self.list_dirty = true;
                                }
                            }
                            window.request_redraw();
                            return;
                        }

                        // Determine which panel was clicked
                        if self.cursor_pos.x < panel_x {
                            // List panel click
                            self.focus = FocusPanel::List;
                            if self.list_mode == ListMode::Search {
                                // Handle search result click
                                let click_line = self.list_item_from_mouse_raw(self.cursor_pos.y);
                                if let Some(line) = click_line {
                                    self.handle_search_click(line);
                                }
                                self.list_dirty = true;
                            } else if let Some(idx) = self.list_item_from_mouse(self.cursor_pos.y) {
                                self.selected_item_idx = idx;
                                let is_folder = self.flat_items[idx].is_folder;
                                if is_folder {
                                    self.toggle_folder_for_item(idx);
                                } else {
                                    let path = self.flat_items[idx].path.clone();
                                    self.open_file(&path);
                                }
                                self.list_dirty = true;
                            }
                        } else if self.cursor_pos.y < console_y_pos {
                            // Document panel click
                            self.focus = FocusPanel::Document;
                            self.caret_visible = true;
                            self.caret_last_toggle = std::time::Instant::now();
                            if self.view_mode == ViewMode::EditText {
                                // Check for double-click
                                let now = std::time::Instant::now();
                                let dt = now.duration_since(self.last_click_time);
                                let dx = (self.cursor_pos.x - self.last_click_pos.x).abs();
                                let dy = (self.cursor_pos.y - self.last_click_pos.y).abs();

                                if dt.as_millis() < 400 && dx < 5.0 && dy < 5.0 {
                                    // Double-click: select word
                                    if let Some(loc) = self
                                        .doc_pos_from_mouse(self.cursor_pos.x, self.cursor_pos.y)
                                        && let Some(doc) = self.active_doc_mut()
                                        && let Some(word_sel) = doc.word_at(loc)
                                    {
                                        doc.anchor = word_sel.start;
                                        doc.cursor = word_sel.end;
                                        doc.ideal_char_pos = doc.cursor.x;
                                        self.doc_dirty = true;
                                    }
                                } else {
                                    // Single click: position cursor
                                    let shift = self.modifiers.shift_key();
                                    if let Some(loc) = self
                                        .doc_pos_from_mouse(self.cursor_pos.x, self.cursor_pos.y)
                                        && let Some(doc) = self.active_doc_mut()
                                    {
                                        doc.set_cursor(loc, shift);
                                        self.doc_dirty = true;
                                    }
                                    self.is_selecting_text = true;
                                }

                                self.last_click_time = now;
                                self.last_click_pos = self.cursor_pos;
                            }
                        } else {
                            // Console panel click
                            self.focus = FocusPanel::Console;
                        }
                    } else {
                        self.is_dragging_panel = false;
                        self.is_dragging_console = false;
                        self.is_dragging_scrollbar = false;
                        self.is_dragging_list_scrollbar = false;
                        self.is_selecting_text = false;
                    }

                    self.update_window_title();
                    window.request_redraw();
                }
            }

            // Right-click context menus
            WindowEvent::MouseInput {
                state: ElementState::Pressed,
                button: MouseButton::Right,
                ..
            } => {
                if let Some(window) = self.window.clone() {
                    let size = window.inner_size();
                    let panel_x = size.width as f64 * self.panel_ratio as f64;
                    let console_y_pos = size.height as f64 * self.console_ratio as f64;

                    if self.cursor_pos.x < panel_x {
                        // Right-click in list panel
                        self.focus = FocusPanel::List;
                        if let Some(idx) = self.list_item_from_mouse(self.cursor_pos.y) {
                            self.selected_item_idx = idx;
                            self.list_dirty = true;
                        }
                        self.show_file_tree_context_menu();
                    } else if self.cursor_pos.y < console_y_pos {
                        // Right-click in document panel
                        self.focus = FocusPanel::Document;
                        self.show_editor_context_menu();
                    } else {
                        // Right-click in console panel
                        self.focus = FocusPanel::Console;
                        self.show_console_context_menu();
                    }
                    window.request_redraw();
                }
            }

            WindowEvent::MouseWheel { delta, .. } => {
                // Ctrl+Wheel: zoom font size per panel
                if self.modifiers.control_key() {
                    let zoom_delta = match delta {
                        MouseScrollDelta::LineDelta(_, y) => y,
                        MouseScrollDelta::PixelDelta(pos) => (pos.y / 20.0) as f32,
                    };

                    let Some(window) = self.window.clone() else {
                        return;
                    };
                    let size = window.inner_size();
                    let panel_x = size.width as f64 * self.panel_ratio as f64;
                    let console_y_pos = size.height as f64 * self.console_ratio as f64;

                    if self.cursor_pos.x < panel_x {
                        // Zoom file list / search panel
                        self.config.list_font_size =
                            (self.config.list_font_size + zoom_delta).clamp(8.0, 48.0);
                        if let Some(font_sys) = self.font_system.as_mut() {
                            let metrics = Metrics::new(
                                self.config.list_font_size,
                                self.config.list_font_size + 4.0,
                            );
                            if let Some(buf) = self.list_buffer.as_mut() {
                                buf.set_metrics(font_sys, metrics);
                            }
                        }
                        self.list_dirty = true;
                    } else if self.cursor_pos.y < console_y_pos {
                        // Zoom document
                        self.config.font_size =
                            (self.config.font_size + zoom_delta).clamp(8.0, 48.0);
                        if let Some(font_sys) = self.font_system.as_mut() {
                            let metrics =
                                Metrics::new(self.config.font_size, self.config.font_size + 4.0);
                            if let Some(buf) = self.doc_buffer.as_mut() {
                                buf.set_metrics(font_sys, metrics);
                            }
                            if let Some(buf) = self.line_num_buffer.as_mut() {
                                buf.set_metrics(font_sys, metrics);
                            }
                        }
                        self.doc_dirty = true;
                    } else {
                        // Zoom console
                        self.config.console_font_size =
                            (self.config.console_font_size + zoom_delta).clamp(8.0, 48.0);
                        if let Some(font_sys) = self.font_system.as_mut() {
                            let metrics = Metrics::new(
                                self.config.console_font_size,
                                self.config.console_font_size + 4.0,
                            );
                            if let Some(buf) = self.console_buffer.as_mut() {
                                buf.set_metrics(font_sys, metrics);
                            }
                        }
                        self.console_dirty = true;
                    }

                    window.request_redraw();
                    return;
                }

                let lines = match delta {
                    MouseScrollDelta::LineDelta(_, y) => -(y * 3.0) as i32,
                    MouseScrollDelta::PixelDelta(pos) => -(pos.y / 20.0) as i32,
                };

                let Some(window) = self.window.clone() else {
                    return;
                };
                let size = window.inner_size();
                let panel_x = size.width as f64 * self.panel_ratio as f64;
                let console_y_pos = size.height as f64 * self.console_ratio as f64;

                if self.cursor_pos.x < panel_x {
                    // Scroll file list
                    let new_scroll = (self.file_scroll as i32 + lines).max(0) as usize;
                    let list_total = self.flat_items.len() + 1;
                    let visible = (size.height as f32 / self.list_line_height) as usize;
                    let max_scroll = list_total.saturating_sub(visible);
                    self.file_scroll = new_scroll.min(max_scroll);
                    self.list_dirty = true;
                } else if self.cursor_pos.y < console_y_pos {
                    // Scroll document — only changes viewport offset, no buffer rebuild needed
                    let rendered = self.rendered_doc_lines;
                    let is_edit = self.view_mode == ViewMode::EditText;
                    if let Some(doc) = self.active_doc_mut() {
                        let max = if is_edit { doc.line_count() } else { rendered };
                        let new_scroll = (doc.scroll_line as i32 + lines).max(0) as usize;
                        doc.scroll_line = new_scroll.min(max.saturating_sub(1));
                    }
                } else {
                    // Scroll console
                    self.console.scroll_offset =
                        (self.console.scroll_offset as i32 + lines).max(0) as usize;
                    self.console_dirty = true;
                }
                window.request_redraw();
            }

            WindowEvent::KeyboardInput { event, .. } => {
                if event.state != ElementState::Pressed {
                    return;
                }

                // Reset caret blink on any keypress
                self.caret_visible = true;
                self.caret_last_toggle = std::time::Instant::now();

                let ctrl = self.modifiers.control_key();
                let shift = self.modifiers.shift_key();
                let alt = self.modifiers.alt_key();

                // Global shortcuts
                match &event.logical_key {
                    Key::Named(NamedKey::F1) => {
                        self.show_about();
                        if let Some(w) = &self.window {
                            w.request_redraw();
                        }
                        return;
                    }
                    Key::Named(NamedKey::F5) => {
                        self.handle_command_action(CommandAction::RefreshFolder);
                        if let Some(w) = &self.window {
                            w.request_redraw();
                        }
                        return;
                    }
                    Key::Named(NamedKey::F8) if shift => {
                        // Previous search result — navigate into doc
                        self.navigate_search_result(false);
                        if let Some(w) = &self.window {
                            w.request_redraw();
                        }
                        return;
                    }
                    Key::Named(NamedKey::F8) => {
                        // Next search result — navigate into doc
                        self.navigate_search_result(true);
                        if let Some(w) = &self.window {
                            w.request_redraw();
                        }
                        return;
                    }
                    Key::Named(NamedKey::Escape) => {
                        // Close overlay or return to edit
                        if self
                            .active_doc_path
                            .as_ref()
                            .is_some_and(|p| p.to_str() == Some("__about__"))
                        {
                            self.open_docs.remove(&PathBuf::from("__about__"));
                            self.active_doc_path = None;
                            self.doc_dirty = true;
                        } else if self.view_mode == ViewMode::MarkdownPreview
                            || self.view_mode == ViewMode::Csv
                        {
                            self.view_mode = ViewMode::EditText;
                            self.doc_dirty = true;
                        } else if self.list_mode == ListMode::Search
                            && self.focus == FocusPanel::List
                        {
                            self.list_mode = ListMode::Files;
                            self.list_dirty = true;
                        } else if self.focus == FocusPanel::Console {
                            self.focus = FocusPanel::Document;
                        }
                        if let Some(w) = &self.window {
                            w.request_redraw();
                        }
                        return;
                    }
                    _ => {}
                }

                // Alt+Z: toggle word wrap
                if alt
                    && let Key::Character(c) = &event.logical_key
                    && c == "z"
                {
                    self.word_wrap = !self.word_wrap;
                    self.doc_dirty = true;
                    if let Some(w) = &self.window {
                        w.request_redraw();
                    }
                    return;
                }

                // Ctrl shortcuts
                if ctrl
                    && let Key::Character(c) = &event.logical_key
                    && self.handle_ctrl_key(c)
                {
                    self.ensure_cursor_visible();
                    self.update_window_title();
                    if let Some(w) = &self.window {
                        w.request_redraw();
                    }
                    return;
                }

                // Focus-specific key handling
                let handled = match self.focus {
                    FocusPanel::List => self.handle_list_key(&event.logical_key),
                    FocusPanel::Document => {
                        // handle_doc_key sets doc_dirty internally only for content changes
                        self.handle_doc_key(&event.logical_key, shift, ctrl)
                    }
                    FocusPanel::Console => {
                        if ctrl {
                            if let Key::Character(c) = &event.logical_key {
                                if c == "v" {
                                    let text = self
                                        .clipboard
                                        .as_mut()
                                        .and_then(|c| c.get_text().ok())
                                        .unwrap_or_default()
                                        .replace("\r\n", "")
                                        .replace('\n', "");
                                    self.console
                                        .input
                                        .insert_str(self.console.input_cursor, &text);
                                    self.console.input_cursor += text.len();
                                    self.console_dirty = true;
                                    true
                                } else {
                                    false
                                }
                            } else {
                                false
                            }
                        } else {
                            self.handle_console_key(&event.logical_key)
                        }
                    }
                };

                // Character input for editor or console
                if !handled
                    && !ctrl
                    && !alt
                    && let Key::Character(c) = &event.logical_key
                {
                    match self.focus {
                        FocusPanel::Document => {
                            if self.view_mode == ViewMode::EditText
                                && let Some(doc) = self.active_doc_mut()
                            {
                                doc.edit_insert(c);
                                self.doc_dirty = true;
                            }
                        }
                        FocusPanel::Console => {
                            self.console.input.insert_str(self.console.input_cursor, c);
                            self.console.input_cursor += c.len();
                            self.console_dirty = true;
                        }
                        FocusPanel::List if self.list_mode == ListMode::Search => {
                            self.search_query.insert_str(self.search_cursor, c);
                            self.search_cursor += c.len();
                            let query = self.search_query.clone();
                            self.request_search(query);
                            self.list_dirty = true;
                        }
                        _ => {}
                    }
                }

                self.ensure_cursor_visible();
                self.update_window_title();
                if let Some(w) = &self.window {
                    w.request_redraw();
                }
            }

            _ => {}
        }
    }

    fn user_event(&mut self, _event_loop: &ActiveEventLoop, event: BgEvent) {
        match event {
            BgEvent::TreeReady(tree) => {
                self.tree = tree;
                self.rebuild_flat_items();
                // Restore selection for the active document in the file list
                if let Some(active) = &self.active_doc_path {
                    for (i, item) in self.flat_items.iter().enumerate() {
                        if &item.path == active {
                            self.selected_item_idx = i;
                            break;
                        }
                    }
                }
                self.list_dirty = true;
            }
            BgEvent::FileLoaded(path, result) => {
                match result {
                    Ok(doc) => {
                        self.open_docs.insert(path.clone(), doc);
                        // If this was the file we were waiting to open, activate it now
                        if self.pending_open_path.as_ref() == Some(&path) {
                            self.pending_open_path = None;
                            self.activate_loaded_doc(&path);
                        }
                    }
                    Err(e) => {
                        self.console.lines.push(ConsoleLine {
                            text: format!("Error opening {}: {}", path.display(), e),
                            is_command: false,
                        });
                        self.console_dirty = true;
                        if self.pending_open_path.as_ref() == Some(&path) {
                            self.pending_open_path = None;
                        }
                    }
                }
            }
            BgEvent::SearchDone { query, groups, seq } => {
                // Discard stale results
                if seq < self.search_seq {
                    return;
                }
                let count: usize = groups.iter().map(|g| g.results.len()).sum();
                self.search_groups = groups;
                self.search_selected_idx = 0;
                self.console.lines.push(ConsoleLine {
                    text: format!("Found {} results for '{}'", count, query),
                    is_command: false,
                });
                self.console_dirty = true;
                self.list_dirty = true;
            }
        }
        if let Some(w) = &self.window {
            w.request_redraw();
        }
    }

    fn about_to_wait(&mut self, event_loop: &ActiveEventLoop) {
        if let Ok(event) = MenuEvent::receiver().try_recv() {
            let id_str = event.id().0.as_str();
            match id_str {
                "quit" => {
                    self.config.panel_ratio = self.panel_ratio;
                    self.config.console_ratio = self.console_ratio;
                    self.config.root_folder = self.root_path.clone();
                    self.config.recent_file = self.active_doc_path.clone();
                    self.config.word_wrap = self.word_wrap;
                    if let Some(w) = &self.window {
                        let size = w.inner_size();
                        self.config.window_width = size.width;
                        self.config.window_height = size.height;
                    }
                    self.config.save();
                    event_loop.exit();
                }
                "new" => {
                    self.new_document();
                    self.doc_dirty = true;
                }
                "open_file" => {
                    if let Some(path) = rfd::FileDialog::new().pick_file() {
                        self.open_file(&path.clone());
                    }
                }
                "open_folder" => {
                    if let Some(path) = rfd::FileDialog::new().pick_folder() {
                        self.root_path = path.clone();
                        self.request_tree_scan();
                        self.config.remember_root_folder(&path);
                        self.list_dirty = true;
                    }
                }
                "save" => self.save_active_doc(),
                "save_as" => {
                    if let Some(path) = rfd::FileDialog::new().save_file()
                        && let Some(doc_path) = self.active_doc_path.clone()
                        && let Some(doc) = self.open_docs.get_mut(&doc_path)
                    {
                        let _ = doc.save_as(path);
                    }
                }
                "save_all" => self.save_all(),
                "undo" => {
                    if let Some(doc) = self.active_doc_mut() {
                        doc.undo();
                    }
                    self.doc_dirty = true;
                }
                "redo" => {
                    if let Some(doc) = self.active_doc_mut() {
                        doc.redo();
                    }
                    self.doc_dirty = true;
                }
                "cut" => {
                    let _ = self.handle_ctrl_key("x");
                }
                "copy" => {
                    let _ = self.handle_ctrl_key("c");
                }
                "paste" => {
                    let _ = self.handle_ctrl_key("v");
                }
                "select_all" => {
                    let _ = self.handle_ctrl_key("a");
                }
                "reformat" => {
                    if let Some(doc) = self.active_doc_mut() {
                        doc.reformat_json();
                    }
                    self.doc_dirty = true;
                }
                "sort" => {
                    if let Some(doc) = self.active_doc_mut() {
                        doc.sort_remove_duplicates();
                    }
                    self.doc_dirty = true;
                }
                "word_wrap" => {
                    self.word_wrap = !self.word_wrap;
                    self.doc_dirty = true;
                }
                "markdown" => {
                    self.view_mode = if self.view_mode == ViewMode::MarkdownPreview {
                        ViewMode::EditText
                    } else {
                        ViewMode::MarkdownPreview
                    };
                    self.doc_dirty = true;
                }
                "search" => {
                    self.list_mode = if self.list_mode == ListMode::Search {
                        ListMode::Files
                    } else {
                        ListMode::Search
                    };
                    self.focus = FocusPanel::List;
                    self.list_dirty = true;
                }
                "refresh" => {
                    self.request_tree_scan();
                }
                "about" => {
                    self.show_about();
                }
                id if id.starts_with("ctx_") => {
                    self.handle_context_menu_action(id);
                }
                id if id.starts_with("recent_root_") => {
                    if let Ok(idx) = id["recent_root_".len()..].parse::<usize>()
                        && idx < self.config.recent_root_folders.len()
                    {
                        let folder = self.config.recent_root_folders[idx].clone();
                        if folder.is_dir() {
                            self.root_path = folder;
                            self.request_tree_scan();
                            self.doc_dirty = true;
                        }
                    }
                }
                _ => {}
            }

            self.update_window_title();
            if let Some(w) = &self.window {
                w.request_redraw();
            }
        }

        // Request periodic redraws for caret blink
        if (self.focus == FocusPanel::Document
            || self.focus == FocusPanel::Console
            || (self.focus == FocusPanel::List && self.list_mode == ListMode::Search))
            && let Some(w) = &self.window
        {
            let elapsed = std::time::Instant::now()
                .duration_since(self.caret_last_toggle)
                .as_millis();
            if elapsed >= 530 {
                w.request_redraw();
            }
        }
    }
}

pub fn run() -> Result<(), Box<dyn std::error::Error>> {
    let event_loop = EventLoop::<BgEvent>::with_user_event().build()?;
    let mut app = App::new();
    app.event_proxy = Some(event_loop.create_proxy());
    event_loop.run_app(&mut app)?;
    Ok(())
}
