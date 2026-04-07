Rethinkify is a lightweight text editor with multi-file search, markdown preview, CSV table view, hex view, and a built-in console, written in Rust. 

Place temp files in tmp/

After changes, run:
cargo clippy --fix --allow-dirty && cargo fmt --all && cargo test

## Architecture

The app uses winit for windowing, softbuffer for pixel output, and cosmic-text for text shaping and glyph rendering. There is no GPU dependency — everything is software-rendered into a `u32` pixel buffer. Menus use the muda crate (including context menus via `ContextMenu` trait); file dialogs use rfd; clipboard uses arboard.

The window is split into three panels separated by draggable splitters:
- **Left panel** — hierarchical file tree (or search results, toggled with Ctrl+Shift+F)
- **Top-right panel** — document view (text editor, markdown preview, hex view, or CSV table)
- **Bottom-right panel** — console with command input and scrollable output

## Key Files

| File | Purpose |
|------|---------|
| `src/main.rs` | Entry point; declares modules and calls `app::run()` |
| `src/app.rs` | Main `App` struct implementing winit `ApplicationHandler`; window/surface setup, layout, state management, and event loop |
| `src/commands.rs` | Console command parsing/execution, `ConsoleState` management, output redirection, root-folder sandboxing |
| `src/config.rs` | `AppConfig` — persistent settings (splitter ratios, recent files, recent root folders, word wrap, font sizes); JSON serialization to user config dir |
| `src/document.rs` | `Document` model — line storage (`Vec<String>`), cursor/selection (`TextLocation`, `TextSelection`), unlimited undo/redo, text editing operations, navigation, indent/untab, JSON reformat, sort & dedup, file I/O with encoding detection and 2 MB truncation |
| `src/file_tree.rs` | `FileTreeNode` hierarchical file tree model with lazy expansion, directory scanning, sorting, and flattening into `FlatItem`s for rendering |
| `src/input.rs` | Keyboard and mouse input handling; translates events into document edits, list navigation, and console interaction per focus panel |
| `src/menus.rs` | Native context menus via muda for file tree, text editor, and console panels |
| `src/render.rs` | Software rendering into `u32` pixel buffer; glyph drawing via cosmic-text/swash, panel backgrounds, splitters, selection highlights, cursor, line numbers, scrollbars |
| `src/search.rs` | Multi-file search — recursive directory scan (5 000 results max, skips >10 MB and binary files), result grouping, F8/Shift+F8 navigation |
| `src/syntax.rs` | Syntax highlighting with cookie-based state propagation; C/C++/Rust/JS parser (keywords, comments, strings, numbers, preprocessor) and Markdown parser (headings, bold, italic, links, lists, inline code); builds cosmic-text `AttrsList` spans |
| `src/ui.rs` | Dark theme color constants, `fill_rect` pixel drawing, `blend` alpha compositing, `rgb` helper |
| `src/views.rs` | `ViewMode` enum (EditText, MarkdownPreview, Hex, Csv) and view helpers for hex-dump formatting and CSV-to-table conversion |
| `Cargo.toml` | Dependencies: winit, softbuffer, cosmic-text, muda, rfd, arboard, serde, serde_json, directories |

## Document Model

Documents are stored as `Vec<String>` lines with a cursor (`TextLocation { y, x }`), anchor (for selections), and an undo stack of `UndoItem`s containing `UndoStep`s (insert or delete). The document model is independent of rendering. Each open document lives in a `HashMap<PathBuf, Document>` keyed by file path.

Binary detection, line-ending detection (DOS/Unix), and large-file truncation (2 MB, read-only) are handled at load time.

Each open document remembers its view mode (`EditText`, `MarkdownPreview`, `Hex`, or `Csv`) via the `saved_view_modes` map. Switching to another file saves the current mode; switching back restores it instead of re-detecting from the extension.

## Rendering

Each panel has a cosmic-text `Buffer` that is rebuilt when a dirty flag (`doc_dirty`, `list_dirty`, `console_dirty`) is set. Syntax highlighting is applied per-line via `AttrsList` spans on the cosmic-text buffer lines. The render pass draws backgrounds, splitters, selection highlights, the cursor, and scrollbars into the pixel buffer, then blits glyphs from cosmic-text's swash cache. The cursor blinks on a 530 ms timer via periodic redraws in `about_to_wait`.

## Context Menus

Right-click shows a native context menu (via muda's `show_context_menu_for_hwnd`) appropriate to the panel:
- **File tree** — New File, New Folder, Copy Path, Rename (F2), Delete (Del)
- **Text editor** — Undo, Redo, Cut, Copy, Paste, Select All (with enabled/disabled states)
- **Console** — Copy, Paste

File tree also supports F2 (rename) and Delete keys directly; rename pre-fills the console with a `rename` command.

## Console Commands

The console parses input as `command [args]` with short and long aliases. Supported commands include: `help`, `new`, `open`, `save`, `saveas`, `saveall`, `exit`, `undo`, `redo`, `find`, `reformat`, `sort`, `wordwrap`, `markdown`, `refresh`, `tree`, `echo`, `calc`, `cp`, `mv`, `rename`, `rm`/`del`. The `calc` command evaluates arithmetic expressions. Output redirection (`>` replace, `>>` append) is supported for text-producing commands. File management commands are sandboxed to the root folder.

## Search

Multi-file search (Ctrl+Shift+F) scans all files in the root folder (up to 5000 results, skipping files > 10 MB and binary files). Results are grouped by file in the left panel. F8 / Shift+F8 navigate between results, opening the matched file and selecting the matched text.

## Menus

The File menu includes an **Open Recent Root Folder** submenu listing up to 8 previously used root folders (most recent first), persisted in config.

## Building

```
cargo build          # debug build
cargo run            # run debug
cargo build --release
```

Rust edition: 2024. Target: Windows (Win32 menu init via muda).

