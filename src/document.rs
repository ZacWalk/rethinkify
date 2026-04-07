//! `Document` model — line storage (`Vec<String>`), cursor/selection, unlimited undo/redo,
//! text editing operations, navigation, indent/untab, JSON reformat, sort & dedup, and
//! file I/O with encoding detection, line-ending handling, and 2 MB truncation.

use std::fs;
use std::path::PathBuf;

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Default)]
pub struct TextLocation {
    pub y: usize,
    pub x: usize,
}

impl TextLocation {
    pub fn new(y: usize, x: usize) -> Self {
        Self { y, x }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct TextSelection {
    pub start: TextLocation,
    pub end: TextLocation,
}

impl TextSelection {
    pub fn is_empty(&self) -> bool {
        self.start == self.end
    }

    pub fn normalized(&self) -> Self {
        if self.start <= self.end {
            *self
        } else {
            Self {
                start: self.end,
                end: self.start,
            }
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UndoAction {
    Insert,
    Delete,
}

#[derive(Debug, Clone)]
pub struct UndoStep {
    pub action: UndoAction,
    pub position: TextLocation,
    pub text: String,
}

#[derive(Debug, Clone)]
pub struct UndoItem {
    pub steps: Vec<UndoStep>,
    pub cursor_before: TextLocation,
    pub anchor_before: TextLocation,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LineEnding {
    Dos,
    Unix,
}

const MAX_DOCUMENT_LOAD_SIZE: usize = 2 * 1024 * 1024;

pub struct Document {
    pub lines: Vec<String>,
    pub path: Option<PathBuf>,
    pub modified: bool,
    pub read_only: bool,
    pub is_truncated: bool,
    pub is_binary: bool,

    pub cursor: TextLocation,
    pub anchor: TextLocation,
    pub ideal_char_pos: usize,

    pub undo_stack: Vec<UndoItem>,
    pub undo_pos: usize,
    pub saved_undo_pos: usize,

    pub tab_size: usize,
    pub line_ending: LineEnding,
    pub scroll_line: usize,

    /// Earliest line modified since last buffer rebuild.
    /// `Some(0)` means full rebuild; `None` means no changes.
    pub dirty_from: Option<usize>,
}

pub fn is_binary(data: &[u8]) -> bool {
    let check_len = data.len().min(8192);
    data[..check_len].contains(&0)
}

fn prev_char_boundary(s: &str, pos: usize) -> usize {
    let mut i = pos.min(s.len());
    while i > 0 {
        i -= 1;
        if s.is_char_boundary(i) {
            return i;
        }
    }
    0
}

fn next_char_boundary(s: &str, pos: usize) -> usize {
    let mut i = pos;
    while i < s.len() {
        i += 1;
        if s.is_char_boundary(i) {
            return i;
        }
    }
    s.len()
}

impl Document {
    pub fn new() -> Self {
        Self {
            lines: vec![String::new()],
            path: None,
            modified: false,
            read_only: false,
            is_truncated: false,
            is_binary: false,
            cursor: TextLocation::default(),
            anchor: TextLocation::default(),
            ideal_char_pos: 0,
            undo_stack: Vec::new(),
            undo_pos: 0,
            saved_undo_pos: 0,
            tab_size: 4,
            line_ending: LineEnding::Dos,
            scroll_line: 0,
            dirty_from: Some(0),
        }
    }

    pub fn from_file(path: &std::path::Path) -> std::io::Result<Self> {
        let data = fs::read(path)?;
        let mut doc = Self::new();
        doc.path = Some(path.to_path_buf());

        if is_binary(&data) {
            // Store raw bytes as hex lines for hex view
            doc.read_only = true;
            doc.is_binary = true;
            doc.lines = vec![String::from_utf8_lossy(&data).into_owned()];
            return Ok(doc);
        }

        let (text, truncated) = if data.len() > MAX_DOCUMENT_LOAD_SIZE {
            let truncated_data = &data[..MAX_DOCUMENT_LOAD_SIZE];
            (String::from_utf8_lossy(truncated_data).into_owned(), true)
        } else {
            (String::from_utf8_lossy(&data).into_owned(), false)
        };

        doc.is_truncated = truncated;
        doc.read_only = truncated;

        if text.contains("\r\n") {
            doc.line_ending = LineEnding::Dos;
        } else {
            doc.line_ending = LineEnding::Unix;
        }

        let normalized = text.replace("\r\n", "\n").replace('\r', "\n");
        doc.lines = normalized.split('\n').map(String::from).collect();
        if doc.lines.is_empty() {
            doc.lines.push(String::new());
        }

        Ok(doc)
    }

    pub fn line_count(&self) -> usize {
        self.lines.len()
    }

    /// Mark that lines from `line` onward may have changed.
    pub fn mark_dirty_from(&mut self, line: usize) {
        self.dirty_from = Some(match self.dirty_from {
            Some(existing) => existing.min(line),
            None => line,
        });
    }

    pub fn line_text(&self, line: usize) -> &str {
        self.lines.get(line).map(|s| s.as_str()).unwrap_or("")
    }

    pub fn text(&self) -> String {
        self.lines.join("\n")
    }

    pub fn selection(&self) -> TextSelection {
        TextSelection {
            start: self.anchor,
            end: self.cursor,
        }
    }

    pub fn has_selection(&self) -> bool {
        self.cursor != self.anchor
    }

    pub fn selected_text(&self) -> String {
        let sel = self.selection().normalized();
        if sel.is_empty() {
            return String::new();
        }

        if sel.start.y == sel.end.y {
            let line = self.line_text(sel.start.y);
            let start = sel.start.x.min(line.len());
            let end = sel.end.x.min(line.len());
            return line[start..end].to_string();
        }

        let mut result = String::new();
        for y in sel.start.y..=sel.end.y {
            let line = self.line_text(y);
            if y == sel.start.y {
                let start = sel.start.x.min(line.len());
                result.push_str(&line[start..]);
            } else if y == sel.end.y {
                result.push('\n');
                let end = sel.end.x.min(line.len());
                result.push_str(&line[..end]);
            } else {
                result.push('\n');
                result.push_str(line);
            }
        }
        result
    }

    fn clamp_cursor(&mut self) {
        self.cursor.y = self.cursor.y.min(self.lines.len().saturating_sub(1));
        self.cursor.x = self.cursor.x.min(self.lines[self.cursor.y].len());
        self.anchor.y = self.anchor.y.min(self.lines.len().saturating_sub(1));
        self.anchor.x = self.anchor.x.min(self.lines[self.anchor.y].len());
    }

    // Low-level text ops (no undo tracking)
    fn raw_insert(&mut self, loc: TextLocation, text: &str) -> TextLocation {
        let y = loc.y.min(self.lines.len().saturating_sub(1));
        let x = loc.x.min(self.lines[y].len());
        self.mark_dirty_from(y);
        let insert_lines: Vec<&str> = text.split('\n').collect();

        if insert_lines.len() == 1 {
            self.lines[y].insert_str(x, insert_lines[0]);
            TextLocation::new(y, x + insert_lines[0].len())
        } else {
            let tail = self.lines[y][x..].to_string();
            self.lines[y].truncate(x);
            self.lines[y].push_str(insert_lines[0]);

            let last_idx = insert_lines.len() - 1;
            for (i, &line_text) in insert_lines[1..].iter().enumerate() {
                let mut new_line = String::from(line_text);
                if i == last_idx - 1 {
                    let end_x = new_line.len();
                    new_line.push_str(&tail);
                    self.lines.insert(y + i + 1, new_line);
                    return TextLocation::new(y + i + 1, end_x);
                } else {
                    self.lines.insert(y + i + 1, new_line);
                }
            }
            loc
        }
    }

    fn raw_delete(&mut self, sel: TextSelection) -> (TextLocation, String) {
        let sel = sel.normalized();
        if sel.is_empty() {
            return (sel.start, String::new());
        }

        self.mark_dirty_from(sel.start.y);

        let start_y = sel.start.y.min(self.lines.len().saturating_sub(1));
        let end_y = sel.end.y.min(self.lines.len().saturating_sub(1));
        let start_x = sel.start.x.min(self.lines[start_y].len());
        let end_x = sel.end.x.min(self.lines[end_y].len());

        // Collect deleted text
        let deleted = if start_y == end_y {
            self.lines[start_y][start_x..end_x].to_string()
        } else {
            let mut d = String::new();
            d.push_str(&self.lines[start_y][start_x..]);
            for y in (start_y + 1)..end_y {
                d.push('\n');
                d.push_str(&self.lines[y]);
            }
            d.push('\n');
            d.push_str(&self.lines[end_y][..end_x]);
            d
        };

        if start_y == end_y {
            self.lines[start_y].drain(start_x..end_x);
        } else {
            let tail = self.lines[end_y][end_x..].to_string();
            self.lines[start_y].truncate(start_x);
            self.lines[start_y].push_str(&tail);
            self.lines.drain(start_y + 1..=end_y);
        }

        (TextLocation::new(start_y, start_x), deleted)
    }

    fn record_undo(&mut self, item: UndoItem) {
        self.undo_stack.truncate(self.undo_pos);
        self.undo_stack.push(item);
        self.undo_pos = self.undo_stack.len();
        self.modified = self.undo_pos != self.saved_undo_pos;
    }

    // High-level editing operations with undo tracking

    pub fn edit_insert(&mut self, text: &str) {
        if self.read_only || text.is_empty() {
            return;
        }

        // Normalize line endings
        let text = &text.replace("\r\n", "\n").replace('\r', "\n");

        let cursor_before = self.cursor;
        let anchor_before = self.anchor;
        let mut steps = Vec::new();

        // Delete selection first
        if self.has_selection() {
            let sel = self.selection().normalized();
            let (new_pos, deleted) = self.raw_delete(sel);
            steps.push(UndoStep {
                action: UndoAction::Delete,
                position: sel.start,
                text: deleted,
            });
            self.cursor = new_pos;
            self.anchor = new_pos;
        }

        // Insert text
        let pos_before = self.cursor;
        let pos_after = self.raw_insert(pos_before, text);
        steps.push(UndoStep {
            action: UndoAction::Insert,
            position: pos_before,
            text: text.to_string(),
        });

        self.record_undo(UndoItem {
            steps,
            cursor_before,
            anchor_before,
        });
        self.cursor = pos_after;
        self.anchor = pos_after;
        self.ideal_char_pos = self.cursor.x;
    }

    pub fn edit_delete_selection(&mut self) {
        if self.read_only || !self.has_selection() {
            return;
        }

        let cursor_before = self.cursor;
        let anchor_before = self.anchor;
        let sel = self.selection().normalized();
        let (new_pos, deleted) = self.raw_delete(sel);

        self.record_undo(UndoItem {
            steps: vec![UndoStep {
                action: UndoAction::Delete,
                position: sel.start,
                text: deleted,
            }],
            cursor_before,
            anchor_before,
        });

        self.cursor = new_pos;
        self.anchor = new_pos;
        self.ideal_char_pos = self.cursor.x;
    }

    pub fn edit_delete_forward(&mut self) {
        if self.read_only {
            return;
        }
        if self.has_selection() {
            self.edit_delete_selection();
            return;
        }

        let line = self.line_text(self.cursor.y);
        let end = if self.cursor.x < line.len() {
            let next = next_char_boundary(line, self.cursor.x);
            TextLocation::new(self.cursor.y, next)
        } else if self.cursor.y < self.lines.len() - 1 {
            TextLocation::new(self.cursor.y + 1, 0)
        } else {
            return;
        };

        let cursor_before = self.cursor;
        let anchor_before = self.anchor;
        let sel = TextSelection {
            start: self.cursor,
            end,
        };
        let (new_pos, deleted) = self.raw_delete(sel);

        self.record_undo(UndoItem {
            steps: vec![UndoStep {
                action: UndoAction::Delete,
                position: self.cursor,
                text: deleted,
            }],
            cursor_before,
            anchor_before,
        });

        self.cursor = new_pos;
        self.anchor = new_pos;
        self.ideal_char_pos = self.cursor.x;
    }

    pub fn edit_backspace(&mut self) {
        if self.read_only {
            return;
        }
        if self.has_selection() {
            self.edit_delete_selection();
            return;
        }

        let start = if self.cursor.x > 0 {
            let line = self.line_text(self.cursor.y);
            let prev = prev_char_boundary(line, self.cursor.x);
            TextLocation::new(self.cursor.y, prev)
        } else if self.cursor.y > 0 {
            let prev_len = self.lines[self.cursor.y - 1].len();
            TextLocation::new(self.cursor.y - 1, prev_len)
        } else {
            return;
        };

        let cursor_before = self.cursor;
        let anchor_before = self.anchor;
        let sel = TextSelection {
            start,
            end: self.cursor,
        };
        let (new_pos, deleted) = self.raw_delete(sel);

        self.record_undo(UndoItem {
            steps: vec![UndoStep {
                action: UndoAction::Delete,
                position: start,
                text: deleted,
            }],
            cursor_before,
            anchor_before,
        });

        self.cursor = new_pos;
        self.anchor = new_pos;
        self.ideal_char_pos = self.cursor.x;
    }

    pub fn can_undo(&self) -> bool {
        self.undo_pos > 0
    }

    pub fn can_redo(&self) -> bool {
        self.undo_pos < self.undo_stack.len()
    }

    pub fn undo(&mut self) {
        if !self.can_undo() {
            return;
        }
        self.undo_pos -= 1;
        let item = self.undo_stack[self.undo_pos].clone();

        // Apply steps in reverse
        for step in item.steps.iter().rev() {
            match step.action {
                UndoAction::Insert => {
                    // Undo insert = delete
                    let end = self.compute_insert_end(step.position, &step.text);
                    let sel = TextSelection {
                        start: step.position,
                        end,
                    };
                    self.raw_delete(sel);
                }
                UndoAction::Delete => {
                    // Undo delete = insert
                    self.raw_insert(step.position, &step.text);
                }
            }
        }

        self.cursor = item.cursor_before;
        self.anchor = item.anchor_before;
        self.clamp_cursor();
        self.ideal_char_pos = self.cursor.x;
        self.modified = self.undo_pos != self.saved_undo_pos;
    }

    pub fn redo(&mut self) {
        if !self.can_redo() {
            return;
        }
        let item = self.undo_stack[self.undo_pos].clone();
        self.undo_pos += 1;

        let mut final_cursor = self.cursor;
        for step in &item.steps {
            match step.action {
                UndoAction::Insert => {
                    let end = self.raw_insert(step.position, &step.text);
                    final_cursor = end;
                }
                UndoAction::Delete => {
                    let end = self.compute_insert_end(step.position, &step.text);
                    let sel = TextSelection {
                        start: step.position,
                        end,
                    };
                    let (pos, _) = self.raw_delete(sel);
                    final_cursor = pos;
                }
            }
        }

        self.cursor = final_cursor;
        self.anchor = final_cursor;
        self.clamp_cursor();
        self.ideal_char_pos = self.cursor.x;
        self.modified = self.undo_pos != self.saved_undo_pos;
    }

    fn compute_insert_end(&self, start: TextLocation, text: &str) -> TextLocation {
        let lines: Vec<&str> = text.split('\n').collect();
        if lines.len() == 1 {
            TextLocation::new(start.y, start.x + lines[0].len())
        } else {
            TextLocation::new(start.y + lines.len() - 1, lines.last().unwrap().len())
        }
    }

    // Navigation

    pub fn move_left(&mut self, selecting: bool) {
        if !selecting && self.has_selection() {
            let sel = self.selection().normalized();
            self.cursor = sel.start;
            self.anchor = self.cursor;
            self.ideal_char_pos = self.cursor.x;
            return;
        }
        if self.cursor.x > 0 {
            let line = self.line_text(self.cursor.y);
            self.cursor.x = prev_char_boundary(line, self.cursor.x);
        } else if self.cursor.y > 0 {
            self.cursor.y -= 1;
            self.cursor.x = self.lines[self.cursor.y].len();
        }
        if !selecting {
            self.anchor = self.cursor;
        }
        self.ideal_char_pos = self.cursor.x;
    }

    pub fn move_right(&mut self, selecting: bool) {
        if !selecting && self.has_selection() {
            let sel = self.selection().normalized();
            self.cursor = sel.end;
            self.anchor = self.cursor;
            self.ideal_char_pos = self.cursor.x;
            return;
        }
        let line_len = self.lines[self.cursor.y].len();
        if self.cursor.x < line_len {
            let line = self.line_text(self.cursor.y);
            self.cursor.x = next_char_boundary(line, self.cursor.x);
        } else if self.cursor.y < self.lines.len() - 1 {
            self.cursor.y += 1;
            self.cursor.x = 0;
        }
        if !selecting {
            self.anchor = self.cursor;
        }
        self.ideal_char_pos = self.cursor.x;
    }

    pub fn move_up(&mut self, selecting: bool) {
        if self.cursor.y > 0 {
            self.cursor.y -= 1;
            self.cursor.x = self.ideal_char_pos.min(self.lines[self.cursor.y].len());
        }
        if !selecting {
            self.anchor = self.cursor;
        }
    }

    pub fn move_down(&mut self, selecting: bool) {
        if self.cursor.y < self.lines.len() - 1 {
            self.cursor.y += 1;
            self.cursor.x = self.ideal_char_pos.min(self.lines[self.cursor.y].len());
        }
        if !selecting {
            self.anchor = self.cursor;
        }
    }

    pub fn move_home(&mut self, selecting: bool) {
        // Smart home: toggle between first non-whitespace and column 0
        let line = self.line_text(self.cursor.y);
        let first_non_ws = line
            .bytes()
            .position(|b| !b.is_ascii_whitespace())
            .unwrap_or(0);
        self.cursor.x = if self.cursor.x == first_non_ws {
            0
        } else {
            first_non_ws
        };
        if !selecting {
            self.anchor = self.cursor;
        }
        self.ideal_char_pos = self.cursor.x;
    }

    pub fn move_end(&mut self, selecting: bool) {
        self.cursor.x = self.lines[self.cursor.y].len();
        if !selecting {
            self.anchor = self.cursor;
        }
        self.ideal_char_pos = self.cursor.x;
    }

    pub fn move_doc_start(&mut self, selecting: bool) {
        self.cursor = TextLocation::default();
        if !selecting {
            self.anchor = self.cursor;
        }
        self.ideal_char_pos = 0;
    }

    pub fn move_doc_end(&mut self, selecting: bool) {
        self.cursor.y = self.lines.len().saturating_sub(1);
        self.cursor.x = self.lines[self.cursor.y].len();
        if !selecting {
            self.anchor = self.cursor;
        }
        self.ideal_char_pos = self.cursor.x;
    }

    pub fn move_word_left(&mut self, selecting: bool) {
        if self.cursor.x == 0 && self.cursor.y > 0 {
            self.cursor.y -= 1;
            self.cursor.x = self.lines[self.cursor.y].len();
        } else {
            let line = &self.lines[self.cursor.y];
            let bytes = line.as_bytes();
            let mut x = self.cursor.x;
            while x > 0 && bytes.get(x - 1).is_some_and(|b| b.is_ascii_whitespace()) {
                x -= 1;
            }
            while x > 0
                && bytes
                    .get(x - 1)
                    .is_some_and(|b| b.is_ascii_alphanumeric() || *b == b'_')
            {
                x -= 1;
            }
            self.cursor.x = x;
        }
        if !selecting {
            self.anchor = self.cursor;
        }
        self.ideal_char_pos = self.cursor.x;
    }

    pub fn move_word_right(&mut self, selecting: bool) {
        let line_len = self.lines[self.cursor.y].len();
        if self.cursor.x >= line_len && self.cursor.y < self.lines.len() - 1 {
            self.cursor.y += 1;
            self.cursor.x = 0;
        } else {
            let line = &self.lines[self.cursor.y];
            let bytes = line.as_bytes();
            let mut x = self.cursor.x;
            while x < line_len
                && bytes
                    .get(x)
                    .is_some_and(|b| b.is_ascii_alphanumeric() || *b == b'_')
            {
                x += 1;
            }
            while x < line_len && bytes.get(x).is_some_and(|b| b.is_ascii_whitespace()) {
                x += 1;
            }
            self.cursor.x = x;
        }
        if !selecting {
            self.anchor = self.cursor;
        }
        self.ideal_char_pos = self.cursor.x;
    }

    pub fn select_all(&mut self) {
        self.anchor = TextLocation::default();
        self.cursor.y = self.lines.len().saturating_sub(1);
        self.cursor.x = self.lines[self.cursor.y].len();
    }

    pub fn page_up(&mut self, page_lines: usize, selecting: bool) {
        self.cursor.y = self.cursor.y.saturating_sub(page_lines);
        self.cursor.x = self.ideal_char_pos.min(self.lines[self.cursor.y].len());
        if !selecting {
            self.anchor = self.cursor;
        }
    }

    pub fn page_down(&mut self, page_lines: usize, selecting: bool) {
        self.cursor.y = (self.cursor.y + page_lines).min(self.lines.len().saturating_sub(1));
        self.cursor.x = self.ideal_char_pos.min(self.lines[self.cursor.y].len());
        if !selecting {
            self.anchor = self.cursor;
        }
    }

    // Indent

    pub fn edit_tab(&mut self) {
        if self.read_only {
            return;
        }
        if self.has_selection() {
            let sel = self.selection().normalized();
            let cursor_before = self.cursor;
            let anchor_before = self.anchor;
            let mut steps = Vec::new();
            for y in sel.start.y..=sel.end.y {
                let pos = TextLocation::new(y, 0);
                self.raw_insert(pos, "\t");
                steps.push(UndoStep {
                    action: UndoAction::Insert,
                    position: pos,
                    text: "\t".to_string(),
                });
            }
            self.record_undo(UndoItem {
                steps,
                cursor_before,
                anchor_before,
            });
            self.anchor.x = self.anchor.x.saturating_add(1);
            self.cursor.x = self.cursor.x.saturating_add(1);
        } else {
            self.edit_insert("\t");
        }
    }

    pub fn edit_untab(&mut self) {
        if self.read_only {
            return;
        }
        let sel = self.selection().normalized();
        let start_y = if self.has_selection() {
            sel.start.y
        } else {
            self.cursor.y
        };
        let end_y = if self.has_selection() {
            sel.end.y
        } else {
            self.cursor.y
        };

        let cursor_before = self.cursor;
        let anchor_before = self.anchor;
        let mut steps = Vec::new();

        for y in start_y..=end_y {
            if self.lines[y].starts_with('\t') {
                let pos = TextLocation::new(y, 0);
                let sel = TextSelection {
                    start: pos,
                    end: TextLocation::new(y, 1),
                };
                let (_, deleted) = self.raw_delete(sel);
                steps.push(UndoStep {
                    action: UndoAction::Delete,
                    position: pos,
                    text: deleted,
                });
            } else {
                let spaces: usize = self.lines[y]
                    .bytes()
                    .take(self.tab_size)
                    .take_while(|b| *b == b' ')
                    .count();
                if spaces > 0 {
                    let pos = TextLocation::new(y, 0);
                    let sel = TextSelection {
                        start: pos,
                        end: TextLocation::new(y, spaces),
                    };
                    let (_, deleted) = self.raw_delete(sel);
                    steps.push(UndoStep {
                        action: UndoAction::Delete,
                        position: pos,
                        text: deleted,
                    });
                }
            }
        }

        if !steps.is_empty() {
            self.record_undo(UndoItem {
                steps,
                cursor_before,
                anchor_before,
            });
        }
        self.clamp_cursor();
        self.ideal_char_pos = self.cursor.x;
    }

    // Duplicate / delete line

    pub fn duplicate_line(&mut self) {
        if self.read_only {
            return;
        }
        let y = self.cursor.y;
        let text = self.lines[y].clone();
        let cursor_before = self.cursor;
        let anchor_before = self.anchor;
        let insert_pos = TextLocation::new(y, text.len());
        let insert_text = format!("\n{}", text);
        self.raw_insert(insert_pos, &insert_text);
        self.record_undo(UndoItem {
            steps: vec![UndoStep {
                action: UndoAction::Insert,
                position: insert_pos,
                text: insert_text,
            }],
            cursor_before,
            anchor_before,
        });
        self.cursor.y += 1;
        self.anchor = self.cursor;
    }

    pub fn delete_line(&mut self) {
        if self.read_only || self.lines.is_empty() {
            return;
        }
        let y = self.cursor.y;
        let cursor_before = self.cursor;
        let anchor_before = self.anchor;
        let (start, end) = if y == 0 && self.lines.len() == 1 {
            // Only line — delete its content
            (
                TextLocation::new(0, 0),
                TextLocation::new(0, self.lines[0].len()),
            )
        } else if y < self.lines.len() - 1 {
            // Delete line + trailing newline
            (TextLocation::new(y, 0), TextLocation::new(y + 1, 0))
        } else {
            // Last line — delete preceding newline + line
            (
                TextLocation::new(y - 1, self.lines[y - 1].len()),
                TextLocation::new(y, self.lines[y].len()),
            )
        };
        let sel = TextSelection { start, end };
        let (_, deleted) = self.raw_delete(sel);
        self.record_undo(UndoItem {
            steps: vec![UndoStep {
                action: UndoAction::Delete,
                position: start,
                text: deleted,
            }],
            cursor_before,
            anchor_before,
        });
        self.clamp_cursor();
        self.anchor = self.cursor;
    }

    // Save

    pub fn save(&mut self) -> std::io::Result<()> {
        if let Some(path) = &self.path {
            let ending = match self.line_ending {
                LineEnding::Dos => "\r\n",
                LineEnding::Unix => "\n",
            };
            let content = self.lines.join(ending);
            fs::write(path, &content)?;
            self.modified = false;
            self.saved_undo_pos = self.undo_pos;
        }
        Ok(())
    }

    pub fn save_as(&mut self, path: PathBuf) -> std::io::Result<()> {
        self.path = Some(path);
        self.save()
    }

    // Reformat JSON

    pub fn reformat_json(&mut self) {
        if self.read_only {
            return;
        }
        let text = self.lines.join("\n");
        if let Ok(value) = serde_json::from_str::<serde_json::Value>(&text)
            && let Ok(formatted) = serde_json::to_string_pretty(&value)
        {
            let cursor_before = self.cursor;
            let anchor_before = self.anchor;
            self.lines = formatted.split('\n').map(String::from).collect();
            self.cursor = TextLocation::default();
            self.anchor = TextLocation::default();
            self.modified = true;
            self.mark_dirty_from(0);
            // Record as single undo
            self.record_undo(UndoItem {
                steps: vec![
                    UndoStep {
                        action: UndoAction::Delete,
                        position: TextLocation::default(),
                        text,
                    },
                    UndoStep {
                        action: UndoAction::Insert,
                        position: TextLocation::default(),
                        text: formatted,
                    },
                ],
                cursor_before,
                anchor_before,
            });
        }
    }

    // Sort & remove duplicates

    pub fn sort_remove_duplicates(&mut self) {
        if self.read_only {
            return;
        }
        let cursor_before = self.cursor;
        let anchor_before = self.anchor;
        let old_text = self.lines.join("\n");
        self.lines.sort();
        self.lines.dedup();
        let new_text = self.lines.join("\n");
        self.cursor = TextLocation::default();
        self.anchor = TextLocation::default();
        self.modified = true;
        self.mark_dirty_from(0);
        self.record_undo(UndoItem {
            steps: vec![
                UndoStep {
                    action: UndoAction::Delete,
                    position: TextLocation::default(),
                    text: old_text,
                },
                UndoStep {
                    action: UndoAction::Insert,
                    position: TextLocation::default(),
                    text: new_text,
                },
            ],
            cursor_before,
            anchor_before,
        });
    }

    // Double-click word selection
    pub fn word_at(&self, loc: TextLocation) -> Option<TextSelection> {
        if loc.y >= self.lines.len() {
            return None;
        }
        let line = &self.lines[loc.y];
        let bytes = line.as_bytes();
        if loc.x >= line.len() {
            return None;
        }

        if !bytes[loc.x].is_ascii_alphanumeric() && bytes[loc.x] != b'_' {
            return None;
        }

        let mut start = loc.x;
        while start > 0 && (bytes[start - 1].is_ascii_alphanumeric() || bytes[start - 1] == b'_') {
            start -= 1;
        }
        let mut end = loc.x;
        while end < line.len() && (bytes[end].is_ascii_alphanumeric() || bytes[end] == b'_') {
            end += 1;
        }

        Some(TextSelection {
            start: TextLocation::new(loc.y, start),
            end: TextLocation::new(loc.y, end),
        })
    }

    pub fn set_cursor(&mut self, loc: TextLocation, selecting: bool) {
        self.cursor.y = loc.y.min(self.lines.len().saturating_sub(1));
        self.cursor.x = loc.x.min(self.lines[self.cursor.y].len());
        if !selecting {
            self.anchor = self.cursor;
        }
        self.ideal_char_pos = self.cursor.x;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Helper: create doc from text, apply edit, check result, then undo and redo.
    fn test_edit_undo_redo(initial: &str, expected: &str, edit: impl FnOnce(&mut Document)) {
        let mut doc = Document::new();
        doc.lines = initial.split('\n').map(String::from).collect();
        edit(&mut doc);
        assert_eq!(doc.text(), expected, "after edit");
        doc.undo();
        assert_eq!(doc.text(), initial, "after undo");
        doc.redo();
        assert_eq!(doc.text(), expected, "after redo");
    }

    #[test]
    fn insert_single_chars() {
        let mut doc = Document::new();
        for ch in "Hello".chars() {
            doc.edit_insert(&ch.to_string());
        }
        assert_eq!(doc.text(), "Hello");
        // Insert newline
        doc.edit_insert("\n");
        doc.edit_insert("World");
        assert_eq!(doc.text(), "Hello\nWorld");
    }

    #[test]
    fn split_line() {
        test_edit_undo_redo("line of text", "line\n of text", |doc| {
            doc.cursor = TextLocation::new(0, 4);
            doc.anchor = doc.cursor;
            doc.edit_insert("\n");
        });
    }

    #[test]
    fn combine_line() {
        test_edit_undo_redo("line \nof text", "line of text", |doc| {
            doc.cursor = TextLocation::new(0, 5);
            doc.anchor = doc.cursor;
            doc.edit_delete_forward();
        });
    }

    #[test]
    fn delete_chars() {
        let mut doc = Document::new();
        doc.lines = "one\ntwo\nthree".split('\n').map(String::from).collect();

        // Delete char at col 2, line 0: backspace-style removes char before col 2 = 'n' → "oe"
        doc.cursor = TextLocation::new(0, 2);
        doc.anchor = doc.cursor;
        doc.edit_backspace();

        doc.cursor = TextLocation::new(1, 2);
        doc.anchor = doc.cursor;
        doc.edit_backspace();

        doc.cursor = TextLocation::new(2, 2);
        doc.anchor = doc.cursor;
        doc.edit_backspace();

        assert_eq!(doc.text(), "oe\nto\ntree");

        // Undo all three
        doc.undo();
        doc.undo();
        doc.undo();
        assert_eq!(doc.text(), "one\ntwo\nthree");

        // Redo all three
        doc.redo();
        doc.redo();
        doc.redo();
        assert_eq!(doc.text(), "oe\nto\ntree");
    }

    #[test]
    fn delete_selection() {
        test_edit_undo_redo("line of text", "lixt", |doc| {
            // Delete from col 2 to col 10 on line 0
            doc.anchor = TextLocation::new(0, 2);
            doc.cursor = TextLocation::new(0, 10);
            doc.edit_delete_selection();
        });
    }

    #[test]
    fn delete_2_line_selection() {
        test_edit_undo_redo("one\ntwo\nthree", "onree", |doc| {
            // C++ text_selection(2, 0, 2, 2) = start(col=2,line=0), end(col=2,line=2)
            doc.anchor = TextLocation::new(0, 2);
            doc.cursor = TextLocation::new(2, 2);
            doc.edit_delete_selection();
        });
    }

    #[test]
    fn insert_selection() {
        test_edit_undo_redo("line of text", "line oone\ntwo\nthreef text", |doc| {
            // Insert at col 6, line 0
            doc.cursor = TextLocation::new(0, 6);
            doc.anchor = doc.cursor;
            doc.edit_insert("one\ntwo\nthree");
        });
    }

    #[test]
    fn insert_crlf_text() {
        test_edit_undo_redo("ab", "aone\ntwo\nthreeb", |doc| {
            doc.cursor = TextLocation::new(0, 1);
            doc.anchor = doc.cursor;
            doc.edit_insert("one\r\ntwo\r\nthree");
        });
    }

    #[test]
    fn return_selection() {
        let mut doc = Document::new();
        doc.lines = "one\ntwo\nthree".split('\n').map(String::from).collect();
        // C++ text_selection(2, 0, 2, 2) = start(col=2,line=0), end(col=2,line=2)
        doc.anchor = TextLocation::new(0, 2);
        doc.cursor = TextLocation::new(2, 2);
        assert_eq!(doc.selected_text(), "e\ntwo\nth");
    }

    #[test]
    fn cut_and_paste() {
        test_edit_undo_redo("one\ntwo\nthree", "one\none\ntwo\nthreetwo\nthree", |doc| {
            let all = doc.text();
            // Insert doc text at line 1 col 0
            doc.cursor = TextLocation::new(1, 0);
            doc.anchor = doc.cursor;
            doc.edit_insert(&all);
        });
    }

    #[test]
    fn doc_sort_remove_duplicates() {
        let mut doc = Document::new();
        doc.lines = "banana\napple\nbanana\ncherry\napple"
            .split('\n')
            .map(String::from)
            .collect();
        doc.sort_remove_duplicates();
        assert_eq!(doc.text(), "apple\nbanana\ncherry");
    }

    #[test]
    fn doc_reformat_json() {
        let mut doc = Document::new();
        doc.lines = vec![r#"{"a":"b"}"#.to_string()];
        doc.reformat_json();
        let result = doc.text();
        assert!(result.contains('{'), "json has open brace");
        assert!(result.contains('}'), "json has close brace");
        // serde_json uses ": " format
        assert!(
            result.contains("\": \"") || result.contains(": "),
            "json has formatted colon"
        );
    }

    #[test]
    fn undo_back_to_clean() {
        let mut doc = Document::new();
        doc.lines = vec!["hello".to_string()];
        assert!(!doc.modified);

        // Make an edit
        doc.cursor = TextLocation::new(0, 5);
        doc.anchor = doc.cursor;
        doc.edit_insert(" world");
        assert!(doc.modified, "modified after edit");

        // Undo → back to saved state
        doc.undo();
        assert!(!doc.modified, "undo to clean");

        // Redo → modified again
        doc.redo();
        assert!(doc.modified, "redo is modified");

        // Undo again
        doc.undo();
        assert!(!doc.modified, "undo again to clean");
    }

    #[test]
    fn undo_multiple_to_clean() {
        let mut doc = Document::new();
        doc.lines = vec!["abc".to_string()];

        // First edit
        doc.cursor = TextLocation::new(0, 3);
        doc.anchor = doc.cursor;
        doc.edit_insert("d");

        // Second edit
        doc.cursor = TextLocation::new(0, 4);
        doc.anchor = doc.cursor;
        doc.edit_insert("e");

        assert!(doc.modified);

        // Undo one → still modified
        doc.undo();
        assert!(doc.modified, "undo one still modified");

        // Undo two → clean
        doc.undo();
        assert!(!doc.modified, "undo two clean");
    }
}
