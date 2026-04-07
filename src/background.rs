//! Background worker thread. Processes file I/O, directory scanning, and search
//! off the UI thread, posting results back via `EventLoopProxy<BgEvent>`.

use crate::document::Document;
use crate::file_tree::FileTreeNode;
use crate::search::SearchGroup;
use std::path::{Path, PathBuf};
use std::sync::mpsc;
use winit::event_loop::EventLoopProxy;

// ─── Work items (UI → background) ──────────────────────────────────────────

pub enum BgWork {
    /// Scan a directory tree for the file panel.
    ScanTree(PathBuf),
    /// Load a file into a Document.
    LoadFile(PathBuf),
    /// Multi-file search.
    Search {
        root: PathBuf,
        query: String,
        /// Monotonic ID so stale results can be discarded.
        seq: u64,
    },
}

// ─── Results (background → UI) ─────────────────────────────────────────────

pub enum BgEvent {
    /// Directory tree scan completed.
    TreeReady(FileTreeNode),
    /// File loaded (path, result).
    FileLoaded(PathBuf, Result<Document, String>),
    /// Search completed.
    SearchDone {
        query: String,
        groups: Vec<SearchGroup>,
        seq: u64,
    },
}

// ─── Worker handle ─────────────────────────────────────────────────────────

pub struct BgWorker {
    sender: mpsc::Sender<BgWork>,
}

impl BgWorker {
    /// Spawn the background thread. Returns a handle for submitting work.
    pub fn spawn(proxy: EventLoopProxy<BgEvent>) -> Self {
        let (tx, rx) = mpsc::channel::<BgWork>();

        std::thread::Builder::new()
            .name("bg-worker".into())
            .spawn(move || worker_loop(rx, proxy))
            .expect("failed to spawn background worker thread");

        BgWorker { sender: tx }
    }

    /// Submit work to the background thread.
    pub fn send(&self, work: BgWork) {
        let _ = self.sender.send(work);
    }
}

// ─── Worker loop ────────────────────────────────────────────────────────────

fn worker_loop(rx: mpsc::Receiver<BgWork>, proxy: EventLoopProxy<BgEvent>) {
    while let Ok(work) = rx.recv() {
        match work {
            BgWork::ScanTree(root) => {
                let tree = FileTreeNode::from_dir(&root);
                let _ = proxy.send_event(BgEvent::TreeReady(tree));
            }
            BgWork::LoadFile(path) => {
                let result = Document::from_file(&path).map_err(|e| e.to_string());
                let _ = proxy.send_event(BgEvent::FileLoaded(path, result));
            }
            BgWork::Search { root, query, seq } => {
                // Drain any pending search requests — only run the latest
                let mut latest_query = query;
                let mut latest_seq = seq;
                while let Ok(next) = rx.try_recv() {
                    match next {
                        BgWork::Search {
                            query,
                            seq,
                            root: _,
                        } => {
                            latest_query = query;
                            latest_seq = seq;
                        }
                        other => {
                            // Non-search work: process it first
                            process_non_search(&other, &proxy);
                        }
                    }
                }
                let groups = run_search(&root, &latest_query);
                let _ = proxy.send_event(BgEvent::SearchDone {
                    query: latest_query,
                    groups,
                    seq: latest_seq,
                });
            }
        }
    }
}

fn process_non_search(work: &BgWork, proxy: &EventLoopProxy<BgEvent>) {
    match work {
        BgWork::ScanTree(root) => {
            let tree = FileTreeNode::from_dir(root);
            let _ = proxy.send_event(BgEvent::TreeReady(tree));
        }
        BgWork::LoadFile(path) => {
            let result = Document::from_file(path).map_err(|e| e.to_string());
            let _ = proxy.send_event(BgEvent::FileLoaded(path.clone(), result));
        }
        BgWork::Search { .. } => {} // handled by caller
    }
}

// ─── Standalone search ─────────────────────────────────────────────────────

fn run_search(root: &Path, query: &str) -> Vec<SearchGroup> {
    if query.is_empty() {
        return Vec::new();
    }
    let query_lower = query.to_lowercase();
    let mut groups = Vec::new();
    let mut total = 0usize;
    search_in_dir(root, root, &query_lower, &mut groups, &mut total);
    groups
}

const MAX_RESULTS: usize = 5000;
const MAX_FILE_SIZE: u64 = 10 * 1024 * 1024;

fn search_in_dir(
    root: &Path,
    dir: &Path,
    query: &str,
    groups: &mut Vec<SearchGroup>,
    total: &mut usize,
) {
    use crate::search::SearchResult;

    if *total >= MAX_RESULTS {
        return;
    }
    let Ok(entries) = std::fs::read_dir(dir) else {
        return;
    };
    let mut entries: Vec<_> = entries.flatten().collect();
    entries.sort_by_key(|e| e.file_name());

    for entry in entries {
        if *total >= MAX_RESULTS {
            break;
        }
        let path = entry.path();
        let name = path.file_name().unwrap_or_default().to_string_lossy();
        if name.starts_with('.') {
            continue;
        }

        if path.is_dir() {
            search_in_dir(root, &path, query, groups, total);
        } else {
            if let Ok(meta) = path.metadata()
                && meta.len() > MAX_FILE_SIZE
            {
                continue;
            }
            if let Ok(content) = std::fs::read_to_string(&path) {
                let mut results = Vec::new();
                for (line_num, line) in content.lines().enumerate() {
                    if *total >= MAX_RESULTS {
                        break;
                    }
                    let line_lower = line.to_lowercase();
                    if let Some(pos) = line_lower.find(query) {
                        results.push(SearchResult {
                            path: path.clone(),
                            line_number: line_num + 1,
                            line_text: line.trim().to_string(),
                            match_start: pos,
                            match_len: query.len(),
                        });
                        *total += 1;
                    }
                }
                if !results.is_empty() {
                    let relative = path.strip_prefix(root).unwrap_or(&path);
                    groups.push(SearchGroup {
                        relative_path: relative.to_string_lossy().into_owned(),
                        results,
                        expanded: true,
                    });
                }
            }
        }
    }
}
