//! Hierarchical file tree model. `FileTreeNode` represents files and folders with lazy
//! expansion. Provides directory scanning, sorting, expand/collapse toggling, and
//! flattening into `FlatItem`s for list-panel rendering.

use crate::document::Document;
use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};

// ─── File tree ──────────────────────────────────────────────────────────────

#[derive(Debug, Clone)]
pub struct FileTreeNode {
    pub path: PathBuf,
    pub name: String,
    pub is_folder: bool,
    pub expanded: bool,
    pub children: Vec<FileTreeNode>,
}

impl FileTreeNode {
    /// Create an empty tree root (no children scanned yet).
    pub fn empty(dir: &Path) -> Self {
        FileTreeNode {
            path: dir.to_path_buf(),
            name: dir
                .file_name()
                .unwrap_or_default()
                .to_string_lossy()
                .into_owned(),
            is_folder: true,
            expanded: true,
            children: Vec::new(),
        }
    }

    pub fn from_dir(dir: &Path) -> Self {
        let mut node = FileTreeNode {
            path: dir.to_path_buf(),
            name: dir
                .file_name()
                .unwrap_or_default()
                .to_string_lossy()
                .into_owned(),
            is_folder: true,
            expanded: true,
            children: Vec::new(),
        };
        node.populate();
        node
    }

    fn populate(&mut self) {
        self.children.clear();
        if let Ok(entries) = fs::read_dir(&self.path) {
            let mut folders = Vec::new();
            let mut files = Vec::new();
            for entry in entries.flatten() {
                let path = entry.path();
                let name = path
                    .file_name()
                    .unwrap_or_default()
                    .to_string_lossy()
                    .into_owned();
                if name.starts_with('.') {
                    continue;
                }
                if path.is_dir() {
                    folders.push(FileTreeNode {
                        path: path.clone(),
                        name,
                        is_folder: true,
                        expanded: false,
                        children: Vec::new(),
                    });
                } else {
                    files.push(FileTreeNode {
                        path,
                        name,
                        is_folder: false,
                        expanded: false,
                        children: Vec::new(),
                    });
                }
            }
            folders.sort_by(|a, b| a.name.to_lowercase().cmp(&b.name.to_lowercase()));
            files.sort_by(|a, b| a.name.to_lowercase().cmp(&b.name.to_lowercase()));
            self.children.extend(folders);
            self.children.extend(files);
        }
    }

    pub fn toggle_expand(&mut self) {
        if self.is_folder {
            self.expanded = !self.expanded;
            if self.expanded && self.children.is_empty() {
                self.populate();
            }
        }
    }
}

#[derive(Debug, Clone)]
pub struct FlatItem {
    pub path: PathBuf,
    pub name: String,
    pub is_folder: bool,
    pub expanded: bool,
    pub depth: usize,
    pub modified: bool,
}

pub fn flatten_tree(
    node: &FileTreeNode,
    depth: usize,
    out: &mut Vec<FlatItem>,
    open_docs: &HashMap<PathBuf, Document>,
) {
    for child in &node.children {
        let modified = open_docs.get(&child.path).is_some_and(|d| d.modified);
        out.push(FlatItem {
            path: child.path.clone(),
            name: child.name.clone(),
            is_folder: child.is_folder,
            expanded: child.expanded,
            depth,
            modified,
        });
        if child.is_folder && child.expanded {
            flatten_tree(child, depth + 1, out, open_docs);
        }
    }
}
