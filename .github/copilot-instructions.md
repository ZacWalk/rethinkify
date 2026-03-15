Rethinkify is a lightweight text editor with multi-file search and file navigation, written in C++.

All Windows-specific code should be abstracted into platform_win.cpp and accessed via platform-independent abstractions declared in platform.h.

When updating high level behaviour of the app, specification.md should be updated.

## Key Files

| File | Purpose |
|------|---------|
| `specification.md` | Detailed specification of all features and behaviors |
| `app.h` | Application types: event interfaces, search results, index items, app_events |
| `app.cpp` | Application logic: main window, menus, splitter, file I/O commands |
| `app_frame.h` | Main window frame: child window creation, layout, splitter, menu command routing |
| `app_state.h` | Testable app state: document collection, file operations, search |
| `platform.h` | Platform-independent types, window/draw abstractions, API declarations |
| `platform_win.cpp` | Win32 implementation: windowing, timers, sound, file dialogs, spell check |
| `document.h` / `document.cpp` | Text document model: lines, selections, undo/redo, syntax highlighting, JSON reformat, sort |
| `view_text_base.h` | Base class for text-document views: font, scrolling, message routing |
| `view_text.h` | Text editor view: selection, caret, word wrap, syntax-highlighted rendering |
| `view_text_edit.h` | Editable text view: character input, cut/paste, indent/unindent |
| `view_markdown.h` | Read-only markdown renderer: headings, bold, italic, links, lists |
| `view_hex.h` | Hex view for binary files: offset, hex bytes, ASCII columns |
| `view_list.h` | Base class for panel views: scrolling, selection, keyboard navigation, collapsible items |
| `view_list_files.h` | Folder browser panel: tree navigation, expand/collapse, item rendering |
| `view_list_search.h` | Search panel: text input, result display, navigation |
| `document_syntax.cpp` | Syntax highlighting parsers (C++, plain text, hex, markdown) |
| `util.h` / `util.cpp` | Core utilities: string ops, geometry types, AES-256, SHA-256, base64/hex |
| `ui.h` | UI color constants |
| `test.h` | Lightweight test framework (assertions + runner) |
| `tests.cpp` | Unit tests for document editing, undo/redo, search, crypto, app state |
| `resource.h` | Win32 resource IDs for menus, accelerators, dialogs |
| `pch.h` / `pch.cpp` | Precompiled header: standard library includes |
| `targetver.h` | Windows SDK version targeting |

## Document Management

Multiple documents can be open simultaneously within the current folder. Each document is kept in memory as a `shared_ptr<document>` indexed by file path. Switching between files in the file browser does not prompt to save — modified documents remain in the background with their full undo history intact. Modified files are highlighted in red in the file list. The user is prompted to save modified documents only when changing the root folder or exiting the program.

## Testing

Run all tests from the command line:

```
rethinkify64d.exe /test
```
