//! Multi-file search types. The actual search is performed on the background
//! thread (see `background.rs`). F8/Shift+F8 navigation lives in `app.rs`.

use std::path::PathBuf;

// ─── Search types ───────────────────────────────────────────────────────────

#[derive(Debug, Clone)]
pub struct SearchResult {
    pub path: PathBuf,
    pub line_number: usize,
    pub line_text: String,
    pub match_start: usize,
    pub match_len: usize,
}

#[derive(Debug, Clone)]
pub struct SearchGroup {
    pub relative_path: String,
    pub results: Vec<SearchResult>,
    pub expanded: bool,
}
