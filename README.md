# Rethinkify

Rethinkify is a lightweight text editor with multi-file search, markdown preview, CSV table view, hex view, and a built-in console, written in Rust.

Still a work in progress.

![Rethinkify screenshot](screenshot.png)

## Features

- **Text editing** — syntax highlighting for C/C++, Rust, JavaScript, and Markdown; unlimited undo/redo; word wrap toggle
- **Multi-file search** — recursive search across the root folder (Ctrl+Shift+F) with F8/Shift+F8 result navigation
- **Markdown preview** — rendered inline with heading, bold, italic, link, list, and code block support
- **CSV table view** — column-aligned table display for CSV files
- **Hex view** — hex dump for binary inspection
- **Built-in console** — command input with file management (`cp`, `mv`, `rename`, `rm`), `calc` expression evaluator, output redirection, and root-folder sandboxing
- **File tree** — hierarchical browser with lazy expansion, context menus, rename (F2), and delete
- **Native menus** — context menus for file tree, editor, and console panels

## Architecture

The app uses [winit](https://github.com/rust-windowing/winit) for windowing, [softbuffer](https://github.com/rust-windowing/softbuffer) for pixel output, and [cosmic-text](https://github.com/pop-os/cosmic-text) for text shaping and glyph rendering. There is no GPU dependency — everything is software-rendered into a `u32` pixel buffer. Menus use [muda](https://github.com/nicholasrice/muda); file dialogs use [rfd](https://github.com/PolyMeilex/rfd); clipboard uses [arboard](https://github.com/1Password/arboard).

The window is split into three panels separated by draggable splitters:

- **Left panel** — hierarchical file tree (or search results)
- **Top-right panel** — document view (text editor, markdown preview, hex view, or CSV table)
- **Bottom-right panel** — console with command input and scrollable output

## Building

```
cargo build            # debug build
cargo run              # run debug
cargo build --release  # release build
```

Rust edition: 2024. Runs on Windows and macOS; Linux builds but has no native menu bar or context menus yet (requires GTK integration).