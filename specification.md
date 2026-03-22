# Rethinkify Specification

Rethinkify is a lightweight text editor with multi-file search and file navigation, written in C++.

## Application Layout

The window is split into two panes separated by a draggable vertical splitter:

- **Left panel** — either the folder browser or the search panel (toggled with `Ctrl+Shift+F`)
- **Right panel** — subdivided vertically by a draggable horizontal splitter:
  - **Text/markdown/hex view** — the active document (top)
  - **Console view** — command input and output (bottom)
- **Splitters** — draggable dividers (5px); highlight on hover, change color while tracking. Both splitters share a common `splitter` type defined in `ui.h`.

## Folder View

### Click on a file
- The item highlights in the folder panel
- The file loads (or is retrieved from cache) and displays in the text view
- If the file changed on disk while also modified locally, a prompt asks to reload
- Focus stays in the folder panel

### Click on a folder
- Toggles expand/collapse
- Scroll position is preserved
- The currently selected file stays selected

### Arrow keys
- Moves the highlight up/down and previews the file in the text view (focus stays in the folder panel)
- Press **Enter** to open the highlighted file and move focus to the text view
- Press **Enter** on a folder to toggle expand/collapse

### Visual indicators
- Modified (unsaved) files are shown in red
- Folders show a expand/collapse chevron icon
- Long file names are ellipsized with `...`

## Search View

Opened with `Ctrl+Shift+F`. Press `Escape` to return to the folder view.

### Edit box
- Typing triggers a live search across all files in the current folder
- Placeholder text "Search..." shown when empty
- Supports text selection and standard edit keys (Ctrl+A, Ctrl+C, etc.)
- Total result count shown below the edit box

### Results display
- Results are grouped by file with a collapsible header per file
- File headers show the relative path and the number of matches, e.g. `src/app.cpp (5)`
- Match lines show the line number, the trimmed line text, and the match highlighted in orange

### Click on a match result
- The result highlights in the search panel
- The corresponding file opens in the text view
- The match text is selected and scrolled into view
- Focus stays in the search panel

### Click on a file header
- Toggles the visibility of that file's match results (collapse/expand)
- The header stays visible; only the child matches hide/show

### Arrow keys (Up/Down)
- Navigate between all items including file headers and match results
- On a match result, immediately opens the file and selects the match (live preview)
- On a file header, pressing **Enter** toggles collapse/expand of that file's matches
- On a match result, pressing **Enter** moves focus to the text view

### Other keys
- **F8** — jump to the next match result (skips headers)
- **Shift+F8** — jump to the previous match result (skips headers)
- **F5** — re-run the current search
- **Escape** — close search panel, return to folder view

### Limits
- Maximum 5,000 results
- Files larger than 10 MB are skipped
- Binary files are skipped

## Text Editor View

### Editing
- Full character input with undo/redo history (unlimited, per-document)
- **Tab** / **Shift+Tab** — indent / unindent selected lines
- **Ctrl+R** — reformat JSON
- **Sort & Remove Duplicates** — available from the Edit menu
- Double-click selects a word; drag to extend selection
- Context menu with spell suggestions (when spell check is enabled)

### Navigation
| Shortcut | Action |
|----------|--------|
| Arrow keys | Move cursor |
| Shift+Arrow | Extend selection |
| Ctrl+Left/Right | Word left/right |
| Ctrl+Shift+Left/Right | Select word left/right |
| Home / End | Line start / end |
| Ctrl+Home / Ctrl+End | Document start / end |
| Page Up / Page Down | Page navigation |
| Ctrl+Up / Ctrl+Down | Scroll without moving cursor |

### Clipboard
| Shortcut | Action |
|----------|--------|
| Ctrl+C / Ctrl+Insert | Copy |
| Ctrl+X / Shift+Delete | Cut |
| Ctrl+V / Shift+Insert | Paste |
| Ctrl+A | Select all |

### Undo/Redo
| Shortcut | Action |
|----------|--------|
| Ctrl+Z / Alt+Backspace | Undo |
| Ctrl+Y | Redo |

## Markdown Preview

- **Ctrl+M** toggles markdown preview for the current document
- `.md` / `.markdown` files auto-open in preview mode
- Renders headings (H1–H3 with size scaling), **bold**, *italic*, links, ordered/unordered lists
- Read-only; press **Escape** to return to the text editor

## Hex View

- Automatically shown for binary files
- Displays offset (8 hex digits) | hex bytes (16 per row) | ASCII representation
- Read-only with keyboard scrolling (arrows, Page Up/Down, Ctrl+Home/End)

## Console View

The console is shown below the text view, separated by a draggable horizontal splitter.

### Edit box
- Single-line input field at the bottom of the console
- Prompt symbol `>` displayed before the input text
- Border color changes when focused (blue) vs unfocused (gray)
- Blinking caret (530ms interval) when focused
- Supports text selection, copy/paste, and standard edit keys

### Commands

Commands are parsed using a simple tokenizer that splits on spaces, with support for quoted arguments (e.g. `find "hello world"`). Each command has a short and long alias. Unknown commands display an error message with a help hint.

#### File

| Short | Long | Description |
|-------|------|-------------|
| `n` | `new` | Create a new document |
| `o` | `open` | Open a file |
| `s` | `save` | Save the current file |
| `sa` | `saveas` | Save the current file as... |
| `ss` | `saveall` | Save all modified files |
| `q` | `exit` | Exit the application |

#### Edit

| Short | Long | Description |
|-------|------|-------------|
| `u` | `undo` | Undo the last edit |
| `y` | `redo` | Redo the last undone edit |
| `x` | `cut` | Cut selection to clipboard |
| `c` | `copy` | Copy selection to clipboard |
| `v` | `paste` | Paste from clipboard |
| `d` | `delete` | Delete selection |
| `a` | `selectall` | Select all text |
| `f` | `find` | Search in files: `find <text>` |
| `rf` | `reformat` | Reformat JSON document |
| `sd` | `sort` | Sort lines and remove duplicates |
| `sp` | `spellcheck` | Toggle spell check |

#### View

| Short | Long | Description |
|-------|------|-------------|
| `ww` | `wordwrap` | Toggle word wrap |
| `md` | `markdown` | Toggle markdown preview |
| `r` | `refresh` | Refresh folder index |
| `fn` | `nextresult` | Navigate to next search result |
| `fp` | `prevresult` | Navigate to previous search result |

#### Help & Tools

| Short | Long | Description |
|-------|------|-------------|
| `t` | `test` | Run all tests |
| `ab` | `about` | Show about / help overlay |
| `?`, `h` | `help` | List all available commands |
| `ls`, `dir` | `tree` | List folder contents as a tree |

### Output area
- Scrollable history of commands and output above the edit box
- Commands are echoed with `> ` prefix in yellow
- Output text shown in lighter gray
- Custom vertical scrollbar appears when content overflows
- Mouse wheel scrolls by 2 lines per tick
- Auto-scrolls to bottom after each command

### Keyboard

| Key | Action |
|-----|--------|
| Enter | Execute command and clear input |
| Up / Down | Navigate command history |
| Escape | Return focus to text view |
| Home / End | Move cursor to start / end of input |
| Standard edit keys | Select, copy, paste, delete |

### Focus
- Click to focus the console
- Escape returns focus to the text view

## Overlay Documents

- **F1** — About / keyboard shortcut reference (read-only overlay)
- **Ctrl+T** — Run tests and show results (read-only overlay)
- Press **Escape** to close and return to the previous document
- While an overlay is showing, the left panel is hidden

## Document Management

- Multiple documents open simultaneously; switching files does not prompt to save
- Modified documents remain in memory with their full undo history
- Changing the root folder or exiting prompts to save modified files
- Auto-detects on-disk changes and silently reloads (or prompts if locally modified)

## File Menu
| Shortcut | Action |
|----------|--------|
| Ctrl+N | New document (creates `new-N.md`) |
| Ctrl+O | Open file |
| Ctrl+S | Save (or Save As if new) |
| Ctrl+Shift+S | Save all modified files |

## View Menu
| Shortcut | Action |
|----------|--------|
| Alt+Z | Toggle word wrap |
| Ctrl+M | Toggle markdown preview |
| F5 | Refresh folder tree from disk |
| F8 | Next result (search) or next file (folder) |
| Shift+F8 | Previous result (search) or previous file (folder) |

## Other
| Shortcut | Action |
|----------|--------|
| Ctrl+Shift+F | Toggle search-in-files panel |
| Ctrl+Shift+P | Toggle spell check |
| Ctrl+Plus / Ctrl+Minus | Zoom in / out |
| Ctrl+0 | Reset zoom |
| Escape | Close overlay / exit preview / close search panel |

## Configuration Persistence

Saved on exit and restored on startup:
- Window position, size, and maximized state
- Text editor and list panel font sizes
- Splitter positions (stored as ratios, not pixel values)
- Last open folder and document

## Syntax Highlighting

Auto-detected by file extension. The syntax highlighter is owned by the view, not the document — the document stores text content only, and the view selects the appropriate highlighter when a document is assigned.

- **C/C++** — keywords, comments, strings, numbers, preprocessor directives, operators
- **Markdown** — headings, bold, italic, links, list markers
- **Plain text** — no highlighting
- **Hex** — custom offset/hex/ASCII column formatting

Spell checking is integrated into highlighting: when enabled on a document, the view marks misspelled words during rendering. Spell check state (`enabled`/`disabled`) is stored per-document; the spell checker itself is a shared platform service accessed via free functions.

## Command-Line

```
rethinkify64d.exe /test    — Run all tests and print results to stdout
rethinkify64d.exe <file>   — Open a specific file
```