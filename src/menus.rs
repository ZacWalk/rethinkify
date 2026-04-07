//! Native context menus via muda. Builds and shows right-click menus for the file tree
//! (new, rename, delete), text editor (undo, redo, cut, copy, paste), and console (copy, paste).

use crate::app::App;
use crate::commands::ConsoleLine;
use muda::{ContextMenu, Menu, MenuItem, PredefinedMenuItem};
use std::path::PathBuf;

use crate::app::FocusPanel;

impl App {
    pub(crate) fn show_file_tree_context_menu(&mut self) {
        let hwnd = self.hwnd;
        if hwnd == 0 {
            return;
        }

        let menu = Menu::new();
        let _ = menu.append(&MenuItem::with_id("ctx_new_file", "New File", true, None));
        let _ = menu.append(&MenuItem::with_id(
            "ctx_new_folder",
            "New Folder",
            true,
            None,
        ));
        let _ = menu.append(&PredefinedMenuItem::separator());
        let _ = menu.append(&MenuItem::with_id(
            "ctx_copy_path",
            "Copy Path",
            self.selected_item_idx < self.flat_items.len(),
            None,
        ));

        let has_file = self.selected_item_idx < self.flat_items.len()
            && !self.flat_items[self.selected_item_idx].is_folder;
        let _ = menu.append(&MenuItem::with_id(
            "ctx_rename",
            "Rename\tF2",
            has_file,
            None,
        ));
        let _ = menu.append(&MenuItem::with_id(
            "ctx_delete",
            "Delete\tDel",
            has_file,
            None,
        ));

        #[cfg(target_os = "windows")]
        unsafe {
            menu.show_context_menu_for_hwnd(hwnd, None);
        }
        #[cfg(target_os = "macos")]
        unsafe {
            menu.show_context_menu_for_nsview(hwnd as *const std::ffi::c_void, None);
        }
    }

    pub(crate) fn show_editor_context_menu(&mut self) {
        let hwnd = self.hwnd;
        if hwnd == 0 {
            return;
        }

        let has_selection = self.active_doc().is_some_and(|d| d.has_selection());
        let can_paste = self
            .clipboard
            .as_mut()
            .and_then(|c| c.get_text().ok())
            .is_some();
        let is_editable = self.active_doc().is_some_and(|d| !d.read_only);

        let menu = Menu::new();
        let _ = menu.append(&MenuItem::with_id(
            "undo",
            "Undo\tCtrl+Z",
            is_editable,
            None,
        ));
        let _ = menu.append(&MenuItem::with_id(
            "redo",
            "Redo\tCtrl+Y",
            is_editable,
            None,
        ));
        let _ = menu.append(&PredefinedMenuItem::separator());
        let _ = menu.append(&MenuItem::with_id(
            "cut",
            "Cut\tCtrl+X",
            has_selection && is_editable,
            None,
        ));
        let _ = menu.append(&MenuItem::with_id("copy", "Copy\tCtrl+C", true, None));
        let _ = menu.append(&MenuItem::with_id(
            "paste",
            "Paste\tCtrl+V",
            can_paste && is_editable,
            None,
        ));
        let _ = menu.append(&PredefinedMenuItem::separator());
        let _ = menu.append(&MenuItem::with_id(
            "select_all",
            "Select All\tCtrl+A",
            true,
            None,
        ));

        #[cfg(target_os = "windows")]
        unsafe {
            menu.show_context_menu_for_hwnd(hwnd, None);
        }
        #[cfg(target_os = "macos")]
        unsafe {
            menu.show_context_menu_for_nsview(hwnd as *const std::ffi::c_void, None);
        }
    }

    pub(crate) fn show_console_context_menu(&mut self) {
        let hwnd = self.hwnd;
        if hwnd == 0 {
            return;
        }

        let can_paste = self
            .clipboard
            .as_mut()
            .and_then(|c| c.get_text().ok())
            .is_some();

        let menu = Menu::new();
        let _ = menu.append(&MenuItem::with_id("copy", "Copy\tCtrl+C", true, None));
        let _ = menu.append(&MenuItem::with_id(
            "paste",
            "Paste\tCtrl+V",
            can_paste,
            None,
        ));

        #[cfg(target_os = "windows")]
        unsafe {
            menu.show_context_menu_for_hwnd(hwnd, None);
        }
        #[cfg(target_os = "macos")]
        unsafe {
            menu.show_context_menu_for_nsview(hwnd as *const std::ffi::c_void, None);
        }
    }

    pub(crate) fn handle_context_menu_action(&mut self, id: &str) {
        match id {
            "ctx_new_file" => {
                let folder = self.context_menu_folder();
                let path = folder.join("new-file.md");
                self.create_new_file_at(&path);
            }
            "ctx_new_folder" => {
                let folder = self.context_menu_folder();
                self.create_new_folder_at(&folder);
            }
            "ctx_copy_path" => {
                if self.selected_item_idx < self.flat_items.len() {
                    let path_str = self.flat_items[self.selected_item_idx]
                        .path
                        .to_string_lossy()
                        .to_string();
                    if let Some(clip) = &mut self.clipboard {
                        let _ = clip.set_text(&path_str);
                    }
                }
            }
            "ctx_rename" => {
                // Enter rename mode — prompt via console for now
                if self.selected_item_idx < self.flat_items.len()
                    && !self.flat_items[self.selected_item_idx].is_folder
                {
                    let name = &self.flat_items[self.selected_item_idx].name;
                    self.console.lines.push(ConsoleLine {
                        text: format!("Use: rename {} <new-name>", name),
                        is_command: false,
                    });
                    self.console_dirty = true;
                    self.focus = FocusPanel::Console;
                    self.console.input = format!(
                        "rename {} ",
                        self.flat_items[self.selected_item_idx]
                            .path
                            .strip_prefix(&self.root_path)
                            .unwrap_or(&self.flat_items[self.selected_item_idx].path)
                            .display()
                    );
                    self.console.input_cursor = self.console.input.len();
                }
            }
            "ctx_delete" => {
                if self.selected_item_idx < self.flat_items.len()
                    && !self.flat_items[self.selected_item_idx].is_folder
                {
                    let path = self.flat_items[self.selected_item_idx].path.clone();
                    self.delete_path(&path);
                }
            }
            _ => {}
        }
    }

    pub(crate) fn context_menu_folder(&self) -> PathBuf {
        if self.selected_item_idx < self.flat_items.len() {
            let item = &self.flat_items[self.selected_item_idx];
            if item.is_folder {
                return item.path.clone();
            }
            if let Some(parent) = item.path.parent() {
                return parent.to_path_buf();
            }
        }
        self.root_path.clone()
    }
}
