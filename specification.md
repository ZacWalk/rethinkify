# Rethinkify Specification

Rethinkify is a lightweight AI-agent-enabled text editor with multi-file search, Markdown, CSV, charting, and other financial features, written in C++. It is intended to support financial research using various text files.

Command-line and other non-interactive startup modes may read persisted configuration, but they must not overwrite it. Configuration is only written during full interactive app sessions.

## Application Layout

The window is split into two panes separated by a draggable vertical splitter:

- **Left panel** — either the folder browser or the search panel (toggled with `Ctrl+Shift+F`)
- **Right panel** — subdivided vertically by a draggable horizontal splitter:
  - **Text/markdown/hex/CSV/chart view** — the active document (top)
  - **Console view** — command input and output (bottom)
- **Splitters** — draggable dividers (5px); highlight on hover, change color while tracking. Both splitters share a common `splitter` type defined in `ui.h`.

Each open document remembers its own active content view while it stays open. Switching to another file and back restores that document's last content view rather than reusing the most recently selected mode from another document.

The File menu includes an Open Recent Root Folder submenu listing up to 8 previously used root folders in most-recent-first order. Selecting one switches the current root folder, prompting to save modified files first when needed, and restores that folder's last open file when it is still present. This list is persisted in config alongside the last open folder and document.

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
- Folders show an expand/collapse chevron icon
- Long file names are ellipsized with `...`

### Context Menu (Right-Click)
- **New File** — creates `new-file.md` in the folder context (the clicked folder, or parent folder of the clicked file), refreshes the file list, and opens the new file for editing. If that name is already in use, appends `-2`, `-3`, etc. until a unique name is found.
- **New Folder** — creates `new-folder` in the folder context and refreshes the file list. If that name is already in use, appends `-2`, `-3`, etc. until a unique name is found.
- **Copy Path** — copies the full path of the clicked item to the clipboard.
- **Delete** — sends the selected file to the Recycle Bin after a confirmation prompt; if the deleted file is the active document, switches to a new document first; refreshes the file list. Disabled for folders.
- **Rename** — opens an inline edit widget over the file name. Enter commits the rename, Escape cancels. The file name (without extension) is pre-selected. Shows an error if the target name already exists. Disabled for folders.

### Keyboard
- **Delete** — deletes the selected file in the folder panel (same action as the context menu Delete item). No effect on folders.
- **Ctrl+C** — copies the full path of the selected item in the folder panel.
- **F2** — begins inline rename of the selected file (same behavior as the Rename context menu item)

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

### Context Menu (Right-Click)
- **Open Result** — opens the clicked match result in the editor and selects the match text.
- **Expand / Collapse** — toggles a file header open or closed when right-clicking a header.
- **Copy Path** — copies the full path of the clicked file header, or `path:line` for a specific match result using a 1-based line number.

### Arrow keys (Up/Down)
- Navigate between all items including file headers and match results
- On a match result, immediately opens the file and selects the match (live preview)
- On a file header, pressing **Enter** toggles collapse/expand of that file's matches
- On a match result, pressing **Enter** moves focus to the text view

### Other keys
- **F8** — jump to the next match result (skips headers)
- **Shift+F8** — jump to the previous match result (skips headers)
- **F5** — re-run the current search
- **Ctrl+C** — copies the selected search result path, appending `:line` for a specific match
- **Escape** — close search panel, return to folder view

### Limits
- Maximum 5,000 results
- Files larger than 10 MB are skipped
- Binary files are skipped

## Text Editor View

### Editing
- Full character input with undo/redo history (unlimited, per-document)
- Word wrap is enabled by default and can be toggled with **Alt+Z**; the current setting is restored on the next app launch
- **Tab** / **Shift+Tab** — indent / unindent selected lines
- **Ctrl+R** — reformat JSON
- **Sort & Remove Duplicates** — available from the Edit menu
- Double-click selects a word; drag to extend selection
- Context menu with spell suggestions (when spell check is enabled)
- When the text editor has focus, the active row is shown with a subtle full-width highlight band to keep the current edit line visually prominent

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
- Word-wrapped continuation lines inside bullet points are indented to align with the content start (hanging indent)
- Renders markdown tables with aligned columns; column widths are capped to fit the available screen width, with cell text word-wrapping within each column; cells containing numbers, percentages, or currency values are right-aligned, others left-aligned
- Read-only; press **Escape** to return to the text editor

## Hex View

- Automatically shown for binary files
- Displays offset (8 hex digits) | hex bytes (16 per row) | ASCII representation
- Read-only with keyboard scrolling (arrows, Page Up/Down, Ctrl+Home/End)

## CSV View

- Automatically shown for `.csv` files
- Parses comma-separated values with support for quoted fields (RFC 4180)
- Renders data as an aligned table with pipe-delimited columns, similar to markdown tables
- The first row is treated as the header and displayed in bold
- A separator line is drawn below the header
- Column widths are computed from the widest cell in each column, capped to fit the available screen width
- Cells containing numbers, percentages, or currency values are right-aligned; others are left-aligned
- Cell text word-wraps within its column when the content exceeds the column width
- Read-only; scrolling with keyboard (arrows, Page Up/Down, Ctrl+Home/End) and mouse wheel
- Press Escape to return to text editing mode

## Chart View

- Available for CSV files containing trade data (columns: price, qty, time)
- Toggled via View > Chart View menu item (Ctrl+K) or the `chart` console command
- Aggregates tick-level trades into OHLC (Open/High/Low/Close) candlestick bars
- Candle interval is auto-selected based on data time span: 10s for <1h, 1m for <1d, 5m otherwise
- Green candles for bullish (close >= open), red for bearish
- Vertical price axis with grid lines on the left
- Horizontal scrolling via mouse wheel, arrow keys, or minimap drag
- Minimap at the bottom shows the full data range as a reduced chart with a viewport indicator
- Dragging the minimap viewport scrolls the main chart
- Press Escape to return to CSV table view

## Console View

The console is shown below the text view, separated by a draggable horizontal splitter.

### Edit box
- Single-line input field rendered as the last row in the scrollable console buffer
- Prompt symbol `>` displayed before the input text
- Input area uses a slightly different background color from console output, extending the full console width underneath the scrollbar
- Blinking caret (530ms interval) when focused
- Supports text selection, copy/paste, and standard edit keys
- Long input wraps onto additional rows within the console buffer
- Console rows use slightly looser line spacing, with a small extra gap below the prompt block
- Prompt text and caret are vertically centered within the highlighted input rows

### Context Menu (Right-Click)
- **Copy** — copies the selected console output text or selected input text to the clipboard
- **Paste** — pastes clipboard text into the console input box

### Commands

Commands are parsed using a simple tokenizer that splits on spaces, with support for quoted arguments (e.g. `find "hello world"`). The tokenizer also recognizes `>` and `>>` outside quotes for output redirection. Each command has a short and long alias. Unknown commands display an error message with a help hint.

Each command also declares where it is available:

- `UI` commands are available to menus, keyboard shortcuts, and the interactive console used by the person running the app
- `Agent` commands are the subset exposed to Gemini through the `run_command` tool
- Commands that are not available in the current context return a `not available here` error instead of running

#### File

| Short | Long | Description |
|-------|------|-------------|
| `n` | `new` | Create a new document |
| `o` | `open` | Open a file |
| `s` | `save` | Save the current file |
| `sa` | `saveas` | Save the current file as... |
| `ss` | `saveall` | Save all modified files |
| `ex` | `exit` | Exit the application |

#### Edit

| Short | Long | Description |
|-------|------|-------------|
| `u` | `undo` | Undo the last edit |
| `y` | `redo` | Redo the last undone edit |
| `x` | `cut` | Cut selection to clipboard |
| `c` | `copy` | Copy selection to clipboard |
| `v` | `paste` | Paste from clipboard |
| `d` | `delete` | Delete selection |
| `selectall` | `selectall` | Select all text |
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
| `h` | `help` | List all available commands |
| `ls`, `dir` | `tree` | List folder contents as a tree |
| `del`, `rm` | `delete` | Delete a file or folder inside the current root folder |
| `cp` | `copy` | Copy a file or folder inside the current root folder |
| `mv` | `move` | Move a file or folder inside the current root folder |
| `rename` | `rename` | Rename a file or folder within its current parent folder |
| `q` | `quote` | Stock quote: `q <ticker>` prints markdown to the console; `q <ticker> > file.md` saves it to a file and opens that file |
| `echo` | `echo` | Echo text to the console, or create/open a file with `echo "text" > file.md` |
| `?`, `calc` | `calc` | Evaluate a simple math expression such as `calc 1 + 2 * (3 + 4)` |
| `a` | `ai` | Ask Gemini to inspect or edit files in the current root: `a "question"` |
| `sum` | `summarize` | Summarize a markdown, text, or PDF document into markdown: `summarize [path] [> file.md]`; if no output file is provided, saves beside the source as `*-summary.md` |

### Sandboxed shell behavior

- Console file commands are sandboxed to the current root folder and its subfolders
- Absolute paths and paths escaping the root with `..` are rejected
- `>` replaces file contents with command output and opens the file in the editor
- `>>` appends command output to the target file and opens the file in the editor
- Redirection is supported by text-producing commands including `help`, `ls`, `q`, `echo`, and `calc`

### Gemini Agent

- The `a` / `ai` / `agent` console command sends the prompt to Gemini using `GEMINI_API_KEY` loaded from `.env`
- Gemini replies directly in the console output area
- Gemini can inspect and edit files only within the current root folder and its subfolders
- Gemini has tool access for listing files, reading files with line numbers, searching files, writing full file contents, and running only commands explicitly flagged for agent use
- The `sum` / `summarize` console command sends a single source document to Gemini and saves the markdown response as a document in the current root folder
- `sum` supports `.md`, `.markdown`, `.txt`, `.text`, `.log`, and `.pdf` source documents
- PDF summarization sends the PDF bytes directly to Gemini as inline document input; PDFs larger than 50 MB are rejected by the command and would require Files API support instead
- Agent-visible commands are filtered from the normal UI command set; clipboard and other UI-only commands are not exposed to Gemini
- Agent commands may create and open files in the current root, including `q <ticker> > file.md` and `echo "text" > file.md`
- Gemini must not change the root folder or access files outside the current root

### Output area
- Scrollable history of commands, output, and the current input line
- Output text word-wraps to the available console width
- Commands are echoed with `> ` prefix in yellow
- Output text shown in lighter gray
- Custom vertical scrollbar appears when content overflows
- Mouse wheel scrolls by 2 lines per tick
- Auto-scrolls to bottom after each command
- Typing or editing the current command scrolls back to the bottom so the full wrapped input stays visible when it fits in the viewport
- Text selection via mouse click and drag with visual highlighting
- Double-click selects a word in the output
- Shift+click extends the selection
- Ctrl+A selects all output text
- Ctrl+C copies selected output text to clipboard (copies all if nothing selected)
- Selection is cleared when a command is executed

### Keyboard

| Key | Action |
|-----|--------|
| Enter | Execute command and clear input |
| Up / Down | Navigate command history |
| Escape | Return focus to text view |
| Home / End | Move cursor to start / end of input |
| Ctrl+Plus / Ctrl+Minus | Zoom in / out (changes console font size independently) |
| Ctrl+Mouse Wheel | Zoom in / out |
| Ctrl+C | Copy selected input text if the input has a selection; otherwise copy selected output text |
| Ctrl+V / Shift+Insert | Paste into the command input when the console has focus |
| Ctrl+A | Select all output text |
| Ctrl+Home / Ctrl+End | Scroll to top / bottom of output |
| Page Up / Page Down | Scroll output by one page |
| Standard edit keys | Select, copy, paste, delete in input |

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
- Opening a file via `Ctrl+O` that is outside the current root folder adds it to the file list and loads it

### Storage Model

Documents are backed by a shared `file_buffer` (`std::vector<uint8_t>`) that holds the raw file bytes. Each `document_line` stores an offset and length into this buffer rather than its own copy of the text. Lines are decoded on demand (lazy materialization) when first accessed. Once a line is edited, it stores the modified text locally and releases its buffer reference. The buffer is reference-counted (`std::shared_ptr`) and freed automatically when no lines reference it.

### Large File Handling

Files larger than 2 MB are truncated at the 2 MB boundary, opened as read-only, and the info bar displays: *"File exceeds 2 MB and has been truncated. Read-only."*

## File Menu
| Shortcut | Action |
|----------|--------|
| Ctrl+N | New document (creates `new.md`, then `new-2.md`, `new-3.md`, etc. as needed) |
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
| Escape | Close overlay / exit preview / close search panel |

## Configuration Persistence

Saved on exit and restored on startup:
- Window position, size, and maximized state
- Text editor and list panel font sizes
- Text editor word wrap setting
- Splitter positions (stored as ratios, not pixel values)
- Last open folder and document

## View Architecture

All views inherit from `view_base`, which owns the scrolling logic: view extent, content extent, and scroll offset — all in pixels. Inheriting types set the content extent; `view_base` controls the offset via clamped pixel-level scroll methods.

`text_view` extends `view_base` with line-based text display, font metrics, and a virtual text-selection interface (`current_selection`, `set_selection`, `has_current_selection`). The default implementation stores a local `text_selection`; `doc_view` overrides these to delegate to the document's selection state, ensuring that text selection and document selection stay coordinated.

```
view_base — pixel scrolling (extents, offset, scrollbars)
├── text_view — line-based text display, text selection, syntax highlighting
│   ├── doc_view — document-backed view, caret, word wrap
│   │   ├── edit_doc_view — editable text (char input, undo/redo)
│   │   ├── hex_doc_view — hex byte display
│   │   ├── csv_doc_view — CSV table display
│   │   ├── chart_doc_view — OHLC candlestick chart for trade CSV
│   │   └── markdown_doc_view — rendered markdown
│   └── console_view — command input + scrollable output
└── list_view — item list (folder browser, search results)
```

## Syntax Highlighting

Auto-detected by file extension. The syntax highlighter is owned by the view, not the document — the document stores text content only, and the view selects the appropriate highlighter when a document is assigned.

- **C/C++** — keywords, comments, strings, numbers, preprocessor directives, operators
- **Markdown** — headings, bold, italic, links, list markers
- **Plain text** — no highlighting
- **Hex** — custom offset/hex/ASCII column formatting

Spell checking is integrated into highlighting: when enabled, the edit view and markdown preview mark misspelled words during rendering. The app persists an app-wide spell-check mode in config (`auto`, `enabled`, or `disabled`). In `auto`, `.md` and `.txt` files default to spell check on when loaded; explicit toggle changes are saved back to config for the next launch. The spell checker itself is a shared platform service accessed via free functions.

## Command-Line

```
rethinkify64d.exe /test              — Run all tests and print results to stdout
rethinkify64d.exe /download:<url>    — Download URL contents and print to stdout
rethinkify64d.exe /quote:<ticker>    — Fetch stock quote and print generated markdown to stdout
rethinkify64d.exe /spell:<word>      — Probe Windows spell checker status and print validity/suggestions for a word
rethinkify64d.exe <file>             — Open a specific file
```

## Stock Quote

The console command `q <ticker>` (or `quote <ticker>`) fetches stock data from Yahoo Finance and generates a markdown summary document. The generated file (e.g. `msft.md`) opens automatically in markdown preview mode.

- Downloads price data from the Yahoo Finance chart API (`query1.finance.yahoo.com/v8/finance/chart/`)
- Fetches a Yahoo Finance API crumb from `query2.finance.yahoo.com/v1/test/getcrumb`
- Uses the crumb to fetch analyst and earnings data from the quoteSummary API (`query1.finance.yahoo.com/v10/finance/quoteSummary/`)
- Uses a Chrome-compatible user agent for all HTTP requests
- Generates a markdown document containing:
  - Company name and exchange
  - Company profile (sector, industry, employee count, website, business summary)
  - Current price with change and percentage
  - Trading data (previous close, day range, 52-week range, volume)
  - Analyst price targets (mean, median, high, low target prices, recommendation, number of analysts)
  - Analyst recommendations table (strong buy, buy, hold, sell, strong sell counts by period)
  - Quarterly earnings table (EPS actual vs estimate with surprise percentage)
  - Earnings estimates table (EPS and revenue estimates with growth by period)

## Web Requests

The platform layer provides HTTP client support via WinInet for fetching content from the web:

- `pf::is_online()` — checks network connectivity
- `pf::connect_to_host(host, secure, port, user_agent)` — opens a connection to an HTTP/HTTPS host, returning a `web_host_ptr` (optional custom user agent)
- `pf::send_request(host, request)` — sends a GET or POST request and returns a `web_response` with status code, body, and content type
- Supports custom headers, query parameters, multipart form-data uploads, and file downloads
- URL percent-encoding via `url_encode()` in util.h