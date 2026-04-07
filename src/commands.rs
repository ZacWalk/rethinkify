//! Console command parsing and execution. Manages `ConsoleState` (input, history, output
//! lines) and implements all built-in commands (help, open, save, find, calc, cp, mv, rm,
//! etc.) with output redirection and root-folder sandboxing.

use crate::document::Document;
use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};

// ─── Console ────────────────────────────────────────────────────────────────

#[derive(Debug, Clone)]
pub struct ConsoleLine {
    pub text: String,
    pub is_command: bool,
}

pub struct ConsoleState {
    pub lines: Vec<ConsoleLine>,
    pub input: String,
    pub input_cursor: usize,
    pub history: Vec<String>,
    pub history_idx: Option<usize>,
    pub scroll_offset: usize,
}

impl ConsoleState {
    pub fn new() -> Self {
        Self {
            lines: vec![ConsoleLine {
                text: "Rethinkify loaded. Type 'help' for commands.".into(),
                is_command: false,
            }],
            input: String::new(),
            input_cursor: 0,
            history: Vec::new(),
            history_idx: None,
            scroll_offset: 0,
        }
    }

    pub fn execute(
        &mut self,
        root: &Path,
        _docs: &mut HashMap<PathBuf, Document>,
    ) -> Option<CommandAction> {
        let cmd = self.input.trim().to_string();
        if cmd.is_empty() {
            return None;
        }

        self.history.push(cmd.clone());
        self.history_idx = None;
        self.lines.push(ConsoleLine {
            text: format!("> {}", cmd),
            is_command: true,
        });
        self.input.clear();
        self.input_cursor = 0;

        // Parse output redirection: command args > file or command args >> file
        let (cmd_part, redirect_path, redirect_append) = parse_redirection(&cmd);

        let parts: Vec<&str> = cmd_part.splitn(2, ' ').collect();
        let command = parts[0].to_lowercase();
        let args = parts.get(1).unwrap_or(&"").trim();

        // Helper to possibly redirect output
        let redirect_output =
            |lines: &mut Vec<ConsoleLine>, text: &str, rpath: &Option<String>, append: bool| {
                if let Some(path_str) = rpath {
                    let path = if Path::new(path_str).is_relative() {
                        root.join(path_str)
                    } else {
                        PathBuf::from(path_str)
                    };
                    if !path.starts_with(root) {
                        lines.push(ConsoleLine {
                            text: "Output path must stay within the current root folder.".into(),
                            is_command: false,
                        });
                        return;
                    }
                    let result = if append {
                        use std::io::Write;
                        std::fs::OpenOptions::new()
                            .create(true)
                            .append(true)
                            .open(&path)
                            .and_then(|mut f| f.write_all(text.as_bytes()))
                    } else {
                        fs::write(&path, text)
                    };
                    match result {
                        Ok(()) => {
                            lines.push(ConsoleLine {
                                text: format!(
                                    "Saved {}.",
                                    path.strip_prefix(root).unwrap_or(&path).display()
                                ),
                                is_command: false,
                            });
                        }
                        Err(e) => {
                            lines.push(ConsoleLine {
                                text: format!("Failed to save output: {}", e),
                                is_command: false,
                            });
                        }
                    }
                } else {
                    for line in text.lines() {
                        lines.push(ConsoleLine {
                            text: line.to_string(),
                            is_command: false,
                        });
                    }
                    if text.is_empty() || !text.ends_with('\n') {
                        // Already added lines
                    }
                }
            };

        match command.as_str() {
            "h" | "help" => {
                let text = "Commands: help, new, open, save, saveall, exit, find <text>, \
                           reformat, sort, wordwrap, markdown, refresh, tree, echo, calc, \
                           cp <src> <dst>, mv <src> <dst>, rename <path> <name>, rm <path>"
                    .to_string();
                redirect_output(&mut self.lines, &text, &redirect_path, redirect_append);
                None
            }
            "n" | "new" => Some(CommandAction::NewFile),
            "s" | "save" => Some(CommandAction::Save),
            "sa" | "saveas" => Some(CommandAction::SaveAs),
            "ss" | "saveall" => Some(CommandAction::SaveAll),
            "ex" | "exit" => Some(CommandAction::Exit),
            "u" | "undo" => Some(CommandAction::Undo),
            "y" | "redo" => Some(CommandAction::Redo),
            "f" | "find" => {
                if args.is_empty() {
                    self.lines.push(ConsoleLine {
                        text: "Usage: find <text>".into(),
                        is_command: false,
                    });
                    None
                } else {
                    Some(CommandAction::Search(args.to_string()))
                }
            }
            "rf" | "reformat" => Some(CommandAction::ReformatJson),
            "sd" | "sort" => Some(CommandAction::SortRemoveDuplicates),
            "ww" | "wordwrap" => Some(CommandAction::ToggleWordWrap),
            "md" | "markdown" => Some(CommandAction::ToggleMarkdown),
            "r" | "refresh" => Some(CommandAction::RefreshFolder),
            "ls" | "dir" | "tree" => {
                let output = list_tree(root, root, 0);
                redirect_output(&mut self.lines, &output, &redirect_path, redirect_append);
                None
            }
            "echo" => {
                redirect_output(&mut self.lines, args, &redirect_path, redirect_append);
                None
            }
            "?" | "calc" => {
                let result = simple_calc(args);
                redirect_output(&mut self.lines, &result, &redirect_path, redirect_append);
                None
            }
            "rm" | "del" | "delete" => {
                if args.is_empty() {
                    self.lines.push(ConsoleLine {
                        text: "Usage: rm <path>".into(),
                        is_command: false,
                    });
                    None
                } else {
                    let path = if Path::new(args).is_relative() {
                        root.join(args)
                    } else {
                        PathBuf::from(args)
                    };
                    Some(CommandAction::DeletePath(path))
                }
            }
            "cp" | "copy" => {
                let cp_parts: Vec<&str> = args.splitn(2, ' ').collect();
                if cp_parts.len() != 2 {
                    self.lines.push(ConsoleLine {
                        text: "Usage: cp <source> <dest>".into(),
                        is_command: false,
                    });
                    None
                } else {
                    let src = if Path::new(cp_parts[0]).is_relative() {
                        root.join(cp_parts[0])
                    } else {
                        PathBuf::from(cp_parts[0])
                    };
                    let dst = if Path::new(cp_parts[1]).is_relative() {
                        root.join(cp_parts[1])
                    } else {
                        PathBuf::from(cp_parts[1])
                    };
                    Some(CommandAction::CopyPath(src, dst))
                }
            }
            "mv" | "move" => {
                let mv_parts: Vec<&str> = args.splitn(2, ' ').collect();
                if mv_parts.len() != 2 {
                    self.lines.push(ConsoleLine {
                        text: "Usage: mv <source> <dest>".into(),
                        is_command: false,
                    });
                    None
                } else {
                    let src = if Path::new(mv_parts[0]).is_relative() {
                        root.join(mv_parts[0])
                    } else {
                        PathBuf::from(mv_parts[0])
                    };
                    let dst = if Path::new(mv_parts[1]).is_relative() {
                        root.join(mv_parts[1])
                    } else {
                        PathBuf::from(mv_parts[1])
                    };
                    Some(CommandAction::MovePath(src, dst))
                }
            }
            "rename" | "ren" => {
                let ren_parts: Vec<&str> = args.splitn(2, ' ').collect();
                if ren_parts.len() != 2 {
                    self.lines.push(ConsoleLine {
                        text: "Usage: rename <path> <new-name>".into(),
                        is_command: false,
                    });
                    None
                } else {
                    let path = if Path::new(ren_parts[0]).is_relative() {
                        root.join(ren_parts[0])
                    } else {
                        PathBuf::from(ren_parts[0])
                    };
                    Some(CommandAction::RenamePath(path, ren_parts[1].to_string()))
                }
            }
            _ => {
                self.lines.push(ConsoleLine {
                    text: format!(
                        "Unknown command: '{}'. Type 'help' for available commands.",
                        command
                    ),
                    is_command: false,
                });
                None
            }
        }
    }

    pub fn history_up(&mut self) {
        if self.history.is_empty() {
            return;
        }
        match self.history_idx {
            None => {
                self.history_idx = Some(self.history.len() - 1);
            }
            Some(idx) if idx > 0 => {
                self.history_idx = Some(idx - 1);
            }
            _ => {}
        }
        if let Some(idx) = self.history_idx {
            self.input = self.history[idx].clone();
            self.input_cursor = self.input.len();
        }
    }

    pub fn history_down(&mut self) {
        match self.history_idx {
            Some(idx) if idx + 1 < self.history.len() => {
                self.history_idx = Some(idx + 1);
                self.input = self.history[idx + 1].clone();
                self.input_cursor = self.input.len();
            }
            Some(_) => {
                self.history_idx = None;
                self.input.clear();
                self.input_cursor = 0;
            }
            None => {}
        }
    }
}

// ─── Command Action ─────────────────────────────────────────────────────────

#[derive(Debug)]
pub enum CommandAction {
    NewFile,
    Save,
    SaveAs,
    SaveAll,
    Exit,
    Undo,
    Redo,
    Search(String),
    ReformatJson,
    SortRemoveDuplicates,
    ToggleWordWrap,
    ToggleMarkdown,
    RefreshFolder,
    // File management commands
    CopyPath(PathBuf, PathBuf),
    MovePath(PathBuf, PathBuf),
    RenamePath(PathBuf, String),
    DeletePath(PathBuf),
}

// ─── Helper functions ───────────────────────────────────────────────────────

fn list_tree(_root: &Path, dir: &Path, depth: usize) -> String {
    let mut out = String::new();
    let indent = "  ".repeat(depth);
    if let Ok(entries) = fs::read_dir(dir) {
        let mut entries: Vec<_> = entries.flatten().collect();
        entries.sort_by_key(|e| e.file_name());
        for entry in entries {
            let path = entry.path();
            let name = path.file_name().unwrap_or_default().to_string_lossy();
            if name.starts_with('.') {
                continue;
            }
            if path.is_dir() {
                out.push_str(&format!("{}{}/\n", indent, name));
                out.push_str(&list_tree(_root, &path, depth + 1));
            } else {
                out.push_str(&format!("{}{}\n", indent, name));
            }
        }
    }
    out
}

/// Parse output redirection from a command string.
/// Returns (command_part, optional_output_path, is_append).
fn parse_redirection(cmd: &str) -> (String, Option<String>, bool) {
    if let Some(pos) = cmd.find(">>") {
        let cmd_part = cmd[..pos].trim().to_string();
        let path = cmd[pos + 2..].trim().to_string();
        if path.is_empty() {
            (cmd.to_string(), None, false)
        } else {
            (cmd_part, Some(path), true)
        }
    } else if let Some(pos) = cmd.find('>') {
        // Make sure it's not inside quotes
        let cmd_part = cmd[..pos].trim().to_string();
        let path = cmd[pos + 1..].trim().to_string();
        if path.is_empty() {
            (cmd.to_string(), None, false)
        } else {
            (cmd_part, Some(path), false)
        }
    } else {
        (cmd.to_string(), None, false)
    }
}

fn simple_calc(expr: &str) -> String {
    // Very basic calculator: evaluate simple arithmetic
    let expr = expr.trim();
    if expr.is_empty() {
        return "Usage: calc <expression>".to_string();
    }
    // Simple eval: try to parse as f64 arithmetic
    // For now, do a very basic token-based evaluation
    match eval_expr(expr) {
        Some(val) => {
            if val == val.floor() && val.abs() < 1e15 {
                format!("{}", val as i64)
            } else {
                format!("{}", val)
            }
        }
        None => format!("Error: could not evaluate '{}'", expr),
    }
}

fn eval_expr(s: &str) -> Option<f64> {
    let s = s.trim();
    // Handle addition/subtraction at top level
    let mut depth = 0i32;
    let bytes = s.as_bytes();
    // Scan right-to-left for + or - at depth 0
    let mut i = bytes.len();
    while i > 0 {
        i -= 1;
        match bytes[i] {
            b')' => depth += 1,
            b'(' => depth -= 1,
            b'+' if depth == 0 && i > 0 => {
                let left = eval_expr(&s[..i])?;
                let right = eval_expr(&s[i + 1..])?;
                return Some(left + right);
            }
            b'-' if depth == 0 && i > 0 => {
                // Check it's not a unary minus (preceded by operator or start)
                if i > 0 && !b"+-*/(%^".contains(&bytes[i - 1]) {
                    let left = eval_expr(&s[..i])?;
                    let right = eval_expr(&s[i + 1..])?;
                    return Some(left - right);
                }
            }
            _ => {}
        }
    }
    // Handle multiplication/division
    depth = 0;
    i = bytes.len();
    while i > 0 {
        i -= 1;
        match bytes[i] {
            b')' => depth += 1,
            b'(' => depth -= 1,
            b'*' if depth == 0 => {
                let left = eval_expr(&s[..i])?;
                let right = eval_expr(&s[i + 1..])?;
                return Some(left * right);
            }
            b'/' if depth == 0 => {
                let left = eval_expr(&s[..i])?;
                let right = eval_expr(&s[i + 1..])?;
                if right == 0.0 {
                    return None;
                }
                return Some(left / right);
            }
            b'%' if depth == 0 => {
                let left = eval_expr(&s[..i])?;
                let right = eval_expr(&s[i + 1..])?;
                if right == 0.0 {
                    return None;
                }
                return Some(left % right);
            }
            _ => {}
        }
    }
    // Parentheses
    let s = s.trim();
    if s.starts_with('(') && s.ends_with(')') {
        return eval_expr(&s[1..s.len() - 1]);
    }
    // Unary minus
    if let Some(rest) = s.strip_prefix('-') {
        return eval_expr(rest).map(|v| -v);
    }
    s.parse::<f64>().ok()
}

pub fn copy_dir_recursive(src: &Path, dst: &Path) -> std::io::Result<()> {
    fs::create_dir(dst)?;
    for entry in fs::read_dir(src)?.flatten() {
        let src_path = entry.path();
        let dst_path = dst.join(entry.file_name());
        if src_path.is_dir() {
            copy_dir_recursive(&src_path, &dst_path)?;
        } else {
            fs::copy(&src_path, &dst_path)?;
        }
    }
    Ok(())
}
