//! Syntax highlighting with cookie-based state propagation. Provides C/C++/Rust/JS and
//! Markdown parsers that classify tokens (keywords, comments, strings, numbers, etc.) and
//! build cosmic-text `AttrsList` spans for styled rendering.

use cosmic_text::{Attrs, AttrsList, Color, Family, Style, Weight};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SyntaxStyle {
    Normal,
    Keyword,
    Comment,
    String,
    Number,
    Preprocessor,
    // Markdown
    MdHeading1,
    MdHeading2,
    MdHeading3,
    MdBold,
    MdItalic,
    MdLink,
    MdMarker,
    MdBullet,
}

impl SyntaxStyle {
    pub fn color(&self) -> Color {
        match self {
            SyntaxStyle::Normal => Color::rgb(0xDE, 0xDE, 0xDE),
            SyntaxStyle::Keyword => Color::rgb(0x56, 0x9C, 0xD6),
            SyntaxStyle::Comment => Color::rgb(0x6A, 0x99, 0x55),
            SyntaxStyle::String => Color::rgb(0xCE, 0x91, 0x78),
            SyntaxStyle::Number => Color::rgb(0xB5, 0xCE, 0xA8),
            SyntaxStyle::Preprocessor => Color::rgb(0x9B, 0x9B, 0x9B),
            SyntaxStyle::MdHeading1 => Color::rgb(0x64, 0xC8, 0xFF),
            SyntaxStyle::MdHeading2 => Color::rgb(0x8C, 0xB4, 0xFF),
            SyntaxStyle::MdHeading3 => Color::rgb(0xB4, 0xA0, 0xFF),
            SyntaxStyle::MdBold => Color::rgb(0xFF, 0xFF, 0xFF),
            SyntaxStyle::MdItalic => Color::rgb(0xB4, 0xDC, 0xB4),
            SyntaxStyle::MdLink => Color::rgb(0x64, 0xB4, 0xFF),
            SyntaxStyle::MdMarker => Color::rgb(0x80, 0x80, 0x80),
            SyntaxStyle::MdBullet => Color::rgb(0x80, 0x80, 0x80),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SyntaxType {
    Plain,
    Cpp,
    Rust,
    Python,
    Markdown,
}

pub fn detect_syntax(ext: Option<&str>) -> SyntaxType {
    match ext {
        Some("rs") => SyntaxType::Rust,
        Some("py" | "pyw" | "pyi") => SyntaxType::Python,
        Some(
            "c" | "cpp" | "cxx" | "cc" | "h" | "hpp" | "hxx" | "java" | "cs" | "js" | "ts" | "json",
        ) => SyntaxType::Cpp,
        Some("md" | "markdown") => SyntaxType::Markdown,
        _ => SyntaxType::Plain,
    }
}

// Cookie flags for C/C++ parser state
const COOKIE_COMMENT: u32 = 0x0001;
const COOKIE_STRING: u32 = 0x0008;

// Sorted for binary search
const CPP_KEYWORDS: &[&str] = &[
    "alignas",
    "alignof",
    "and",
    "and_eq",
    "asm",
    "auto",
    "bitand",
    "bitor",
    "bool",
    "break",
    "case",
    "catch",
    "char",
    "char16_t",
    "char32_t",
    "char8_t",
    "class",
    "co_await",
    "co_return",
    "co_yield",
    "compl",
    "concept",
    "const",
    "const_cast",
    "consteval",
    "constexpr",
    "constinit",
    "continue",
    "decltype",
    "default",
    "delete",
    "do",
    "double",
    "dynamic_cast",
    "else",
    "enum",
    "explicit",
    "export",
    "extern",
    "false",
    "float",
    "for",
    "friend",
    "goto",
    "if",
    "inline",
    "int",
    "int16_t",
    "int32_t",
    "int64_t",
    "int8_t",
    "long",
    "map",
    "mutable",
    "namespace",
    "new",
    "noexcept",
    "not",
    "not_eq",
    "nullptr",
    "operator",
    "or",
    "or_eq",
    "private",
    "protected",
    "public",
    "register",
    "reinterpret_cast",
    "requires",
    "return",
    "set",
    "short",
    "signed",
    "size_t",
    "sizeof",
    "static",
    "static_assert",
    "static_cast",
    "string",
    "struct",
    "switch",
    "template",
    "this",
    "thread_local",
    "throw",
    "true",
    "try",
    "typedef",
    "typeid",
    "typename",
    "uint16_t",
    "uint32_t",
    "uint64_t",
    "uint8_t",
    "union",
    "unsigned",
    "using",
    "vector",
    "virtual",
    "void",
    "volatile",
    "wchar_t",
    "while",
    "xor",
    "xor_eq",
];

// Sorted for binary search
const RUST_KEYWORDS: &[&str] = &[
    "Arc", "BTreeMap", "BTreeSet", "Box", "Cell", "Err", "HashMap", "HashSet", "None", "Ok",
    "Option", "Rc", "RefCell", "Result", "Self", "Some", "String", "Vec", "as", "async", "await",
    "bool", "break", "char", "const", "continue", "crate", "dyn", "else", "enum", "extern", "f32",
    "f64", "false", "fn", "for", "i128", "i16", "i32", "i64", "i8", "if", "impl", "in", "isize",
    "let", "loop", "match", "mod", "move", "mut", "pub", "ref", "return", "self", "static", "str",
    "struct", "super", "trait", "true", "type", "u128", "u16", "u32", "u64", "u8", "unsafe", "use",
    "usize", "where", "while", "yield",
];

// Sorted for binary search
const PYTHON_KEYWORDS: &[&str] = &[
    "False",
    "None",
    "True",
    "and",
    "as",
    "assert",
    "async",
    "await",
    "bool",
    "break",
    "class",
    "classmethod",
    "cls",
    "continue",
    "def",
    "del",
    "dict",
    "elif",
    "else",
    "enumerate",
    "except",
    "filter",
    "finally",
    "float",
    "for",
    "from",
    "global",
    "if",
    "import",
    "in",
    "int",
    "is",
    "isinstance",
    "lambda",
    "len",
    "list",
    "map",
    "nonlocal",
    "not",
    "or",
    "pass",
    "print",
    "property",
    "raise",
    "range",
    "return",
    "self",
    "set",
    "staticmethod",
    "str",
    "super",
    "try",
    "tuple",
    "type",
    "while",
    "with",
    "yield",
    "zip",
];

fn is_cpp_keyword(word: &str) -> bool {
    CPP_KEYWORDS.binary_search(&word).is_ok()
}

fn is_rust_keyword(word: &str) -> bool {
    RUST_KEYWORDS.binary_search(&word).is_ok()
}

fn is_python_keyword(word: &str) -> bool {
    PYTHON_KEYWORDS.binary_search(&word).is_ok()
}

#[derive(Debug, Clone)]
pub struct TextBlock {
    pub char_pos: usize,
    pub style: SyntaxStyle,
}

pub fn highlight_cpp(cookie: u32, line: &str) -> (Vec<TextBlock>, u32) {
    let mut blocks = Vec::new();
    let mut new_cookie = cookie;
    let bytes = line.as_bytes();
    let len = bytes.len();
    let mut i = 0;

    // Continue multi-line comment from previous line
    if cookie & COOKIE_COMMENT != 0 {
        blocks.push(TextBlock {
            char_pos: 0,
            style: SyntaxStyle::Comment,
        });
        while i + 1 < len {
            if bytes[i] == b'*' && bytes[i + 1] == b'/' {
                i += 2;
                new_cookie &= !COOKIE_COMMENT;
                if i < len {
                    blocks.push(TextBlock {
                        char_pos: i,
                        style: SyntaxStyle::Normal,
                    });
                }
                break;
            }
            i += 1;
        }
        if new_cookie & COOKIE_COMMENT != 0 {
            return (blocks, new_cookie);
        }
    }

    // Continue string from previous line
    if cookie & COOKIE_STRING != 0 {
        blocks.push(TextBlock {
            char_pos: 0,
            style: SyntaxStyle::String,
        });
        while i < len {
            if bytes[i] == b'\\' && i + 1 < len {
                i += 2;
                continue;
            }
            if bytes[i] == b'"' {
                i += 1;
                new_cookie &= !COOKIE_STRING;
                if i < len {
                    blocks.push(TextBlock {
                        char_pos: i,
                        style: SyntaxStyle::Normal,
                    });
                }
                break;
            }
            i += 1;
        }
        if new_cookie & COOKIE_STRING != 0 {
            return (blocks, new_cookie);
        }
    }

    while i < len {
        // Skip whitespace
        if bytes[i].is_ascii_whitespace() {
            i += 1;
            continue;
        }

        // Preprocessor directive
        if bytes[i] == b'#' && blocks.is_empty() {
            blocks.push(TextBlock {
                char_pos: i,
                style: SyntaxStyle::Preprocessor,
            });
            return (blocks, new_cookie);
        }

        // Single-line comment
        if i + 1 < len && bytes[i] == b'/' && bytes[i + 1] == b'/' {
            blocks.push(TextBlock {
                char_pos: i,
                style: SyntaxStyle::Comment,
            });
            return (blocks, new_cookie);
        }

        // Multi-line comment start
        if i + 1 < len && bytes[i] == b'/' && bytes[i + 1] == b'*' {
            blocks.push(TextBlock {
                char_pos: i,
                style: SyntaxStyle::Comment,
            });
            i += 2;
            while i + 1 < len {
                if bytes[i] == b'*' && bytes[i + 1] == b'/' {
                    i += 2;
                    new_cookie &= !COOKIE_COMMENT;
                    if i < len {
                        blocks.push(TextBlock {
                            char_pos: i,
                            style: SyntaxStyle::Normal,
                        });
                    }
                    break;
                }
                i += 1;
            }
            if i + 1 >= len && !(i > 1 && bytes[i - 2] == b'*' && bytes[i - 1] == b'/') {
                new_cookie |= COOKIE_COMMENT;
                return (blocks, new_cookie);
            }
            continue;
        }

        // String literal
        if bytes[i] == b'"' {
            blocks.push(TextBlock {
                char_pos: i,
                style: SyntaxStyle::String,
            });
            i += 1;
            while i < len {
                if bytes[i] == b'\\' && i + 1 < len {
                    i += 2;
                    continue;
                }
                if bytes[i] == b'"' {
                    i += 1;
                    if i < len {
                        blocks.push(TextBlock {
                            char_pos: i,
                            style: SyntaxStyle::Normal,
                        });
                    }
                    break;
                }
                i += 1;
            }
            if i >= len && !line.ends_with('"') {
                new_cookie |= COOKIE_STRING;
            }
            continue;
        }

        // Character literal
        if bytes[i] == b'\'' {
            blocks.push(TextBlock {
                char_pos: i,
                style: SyntaxStyle::String,
            });
            i += 1;
            while i < len {
                if bytes[i] == b'\\' && i + 1 < len {
                    i += 2;
                    continue;
                }
                if bytes[i] == b'\'' {
                    i += 1;
                    if i < len {
                        blocks.push(TextBlock {
                            char_pos: i,
                            style: SyntaxStyle::Normal,
                        });
                    }
                    break;
                }
                i += 1;
            }
            continue;
        }

        // Number
        if bytes[i].is_ascii_digit()
            || (bytes[i] == b'.' && i + 1 < len && bytes[i + 1].is_ascii_digit())
        {
            blocks.push(TextBlock {
                char_pos: i,
                style: SyntaxStyle::Number,
            });
            if bytes[i] == b'0' && i + 1 < len && (bytes[i + 1] == b'x' || bytes[i + 1] == b'X') {
                i += 2;
                while i < len && bytes[i].is_ascii_hexdigit() {
                    i += 1;
                }
            } else {
                while i < len
                    && (bytes[i].is_ascii_digit()
                        || bytes[i] == b'.'
                        || bytes[i] == b'e'
                        || bytes[i] == b'E')
                {
                    i += 1;
                }
            }
            // Skip suffix (u, l, f, etc.)
            while i < len && bytes[i].is_ascii_alphabetic() {
                i += 1;
            }
            if i < len {
                blocks.push(TextBlock {
                    char_pos: i,
                    style: SyntaxStyle::Normal,
                });
            }
            continue;
        }

        // Identifier/keyword
        if bytes[i].is_ascii_alphabetic() || bytes[i] == b'_' {
            let start = i;
            while i < len && (bytes[i].is_ascii_alphanumeric() || bytes[i] == b'_') {
                i += 1;
            }
            let word = &line[start..i];
            if is_cpp_keyword(word) {
                blocks.push(TextBlock {
                    char_pos: start,
                    style: SyntaxStyle::Keyword,
                });
                if i < len {
                    blocks.push(TextBlock {
                        char_pos: i,
                        style: SyntaxStyle::Normal,
                    });
                }
            }
            continue;
        }

        // Operators
        if b"+-*/%=<>!&|^~?:;,.()[]{}".contains(&bytes[i]) {
            // Just skip for now, they stay normal colored
            i += 1;
            continue;
        }

        i += 1;
    }

    (blocks, new_cookie)
}

pub fn highlight_rust(cookie: u32, line: &str) -> (Vec<TextBlock>, u32) {
    let mut blocks = Vec::new();
    let mut new_cookie = cookie;
    let bytes = line.as_bytes();
    let len = bytes.len();
    let mut i = 0;

    // Continue multi-line comment from previous line
    if cookie & COOKIE_COMMENT != 0 {
        blocks.push(TextBlock {
            char_pos: 0,
            style: SyntaxStyle::Comment,
        });
        while i + 1 < len {
            if bytes[i] == b'*' && bytes[i + 1] == b'/' {
                i += 2;
                new_cookie &= !COOKIE_COMMENT;
                if i < len {
                    blocks.push(TextBlock {
                        char_pos: i,
                        style: SyntaxStyle::Normal,
                    });
                }
                break;
            }
            i += 1;
        }
        if new_cookie & COOKIE_COMMENT != 0 {
            return (blocks, new_cookie);
        }
    }

    while i < len {
        if bytes[i].is_ascii_whitespace() {
            i += 1;
            continue;
        }

        // Attributes #[...] and #![...]
        if bytes[i] == b'#'
            && i + 1 < len
            && (bytes[i + 1] == b'['
                || (bytes[i + 1] == b'!' && i + 2 < len && bytes[i + 2] == b'['))
        {
            blocks.push(TextBlock {
                char_pos: i,
                style: SyntaxStyle::Preprocessor,
            });
            let mut depth = 0;
            while i < len {
                if bytes[i] == b'[' {
                    depth += 1;
                } else if bytes[i] == b']' {
                    depth -= 1;
                    if depth == 0 {
                        i += 1;
                        break;
                    }
                }
                i += 1;
            }
            if i < len {
                blocks.push(TextBlock {
                    char_pos: i,
                    style: SyntaxStyle::Normal,
                });
            }
            continue;
        }

        // Single-line comment (// and /// doc comments)
        if i + 1 < len && bytes[i] == b'/' && bytes[i + 1] == b'/' {
            blocks.push(TextBlock {
                char_pos: i,
                style: SyntaxStyle::Comment,
            });
            return (blocks, new_cookie);
        }

        // Multi-line comment start
        if i + 1 < len && bytes[i] == b'/' && bytes[i + 1] == b'*' {
            blocks.push(TextBlock {
                char_pos: i,
                style: SyntaxStyle::Comment,
            });
            i += 2;
            while i + 1 < len {
                if bytes[i] == b'*' && bytes[i + 1] == b'/' {
                    i += 2;
                    new_cookie &= !COOKIE_COMMENT;
                    if i < len {
                        blocks.push(TextBlock {
                            char_pos: i,
                            style: SyntaxStyle::Normal,
                        });
                    }
                    break;
                }
                i += 1;
            }
            if i + 1 >= len && !(i > 1 && bytes[i - 2] == b'*' && bytes[i - 1] == b'/') {
                new_cookie |= COOKIE_COMMENT;
                return (blocks, new_cookie);
            }
            continue;
        }

        // Raw string r"..." or r#"..."#
        if bytes[i] == b'r' && i + 1 < len && (bytes[i + 1] == b'"' || bytes[i + 1] == b'#') {
            blocks.push(TextBlock {
                char_pos: i,
                style: SyntaxStyle::String,
            });
            i += 1;
            let mut hashes = 0;
            while i < len && bytes[i] == b'#' {
                hashes += 1;
                i += 1;
            }
            if i < len && bytes[i] == b'"' {
                i += 1;
                'raw_outer: while i < len {
                    if bytes[i] == b'"' {
                        let mut closing_hashes = 0;
                        let save = i;
                        i += 1;
                        while i < len && bytes[i] == b'#' && closing_hashes < hashes {
                            closing_hashes += 1;
                            i += 1;
                        }
                        if closing_hashes == hashes {
                            break 'raw_outer;
                        }
                        i = save + 1;
                        continue;
                    }
                    i += 1;
                }
            }
            if i < len {
                blocks.push(TextBlock {
                    char_pos: i,
                    style: SyntaxStyle::Normal,
                });
            }
            continue;
        }

        // String literal
        if bytes[i] == b'"' {
            blocks.push(TextBlock {
                char_pos: i,
                style: SyntaxStyle::String,
            });
            i += 1;
            while i < len {
                if bytes[i] == b'\\' && i + 1 < len {
                    i += 2;
                    continue;
                }
                if bytes[i] == b'"' {
                    i += 1;
                    if i < len {
                        blocks.push(TextBlock {
                            char_pos: i,
                            style: SyntaxStyle::Normal,
                        });
                    }
                    break;
                }
                i += 1;
            }
            continue;
        }

        // Character literal / lifetime
        if bytes[i] == b'\'' {
            // Lifetime like 'a — just skip as normal
            if i + 1 < len
                && bytes[i + 1].is_ascii_alphabetic()
                && (i + 2 >= len || !bytes[i + 2].is_ascii_alphanumeric() || bytes[i + 1] == b'_')
            {
                i += 1;
                while i < len && (bytes[i].is_ascii_alphanumeric() || bytes[i] == b'_') {
                    i += 1;
                }
                continue;
            }
            // Character literal
            blocks.push(TextBlock {
                char_pos: i,
                style: SyntaxStyle::String,
            });
            i += 1;
            while i < len {
                if bytes[i] == b'\\' && i + 1 < len {
                    i += 2;
                    continue;
                }
                if bytes[i] == b'\'' {
                    i += 1;
                    if i < len {
                        blocks.push(TextBlock {
                            char_pos: i,
                            style: SyntaxStyle::Normal,
                        });
                    }
                    break;
                }
                i += 1;
            }
            continue;
        }

        // Number
        if bytes[i].is_ascii_digit()
            || (bytes[i] == b'.' && i + 1 < len && bytes[i + 1].is_ascii_digit())
        {
            blocks.push(TextBlock {
                char_pos: i,
                style: SyntaxStyle::Number,
            });
            if bytes[i] == b'0' && i + 1 < len {
                match bytes[i + 1] {
                    b'x' | b'X' => {
                        i += 2;
                        while i < len && (bytes[i].is_ascii_hexdigit() || bytes[i] == b'_') {
                            i += 1;
                        }
                    }
                    b'b' | b'B' => {
                        i += 2;
                        while i < len && (bytes[i] == b'0' || bytes[i] == b'1' || bytes[i] == b'_')
                        {
                            i += 1;
                        }
                    }
                    b'o' | b'O' => {
                        i += 2;
                        while i < len
                            && ((bytes[i] >= b'0' && bytes[i] <= b'7') || bytes[i] == b'_')
                        {
                            i += 1;
                        }
                    }
                    _ => {
                        while i < len
                            && (bytes[i].is_ascii_digit()
                                || bytes[i] == b'.'
                                || bytes[i] == b'_'
                                || bytes[i] == b'e'
                                || bytes[i] == b'E')
                        {
                            i += 1;
                        }
                    }
                }
            } else {
                while i < len
                    && (bytes[i].is_ascii_digit()
                        || bytes[i] == b'.'
                        || bytes[i] == b'_'
                        || bytes[i] == b'e'
                        || bytes[i] == b'E')
                {
                    i += 1;
                }
            }
            // Type suffix (u8, i32, f64, etc.)
            while i < len && (bytes[i].is_ascii_alphabetic() || bytes[i] == b'_') {
                i += 1;
            }
            if i < len {
                blocks.push(TextBlock {
                    char_pos: i,
                    style: SyntaxStyle::Normal,
                });
            }
            continue;
        }

        // Identifier/keyword + macro name!
        if bytes[i].is_ascii_alphabetic() || bytes[i] == b'_' {
            let start = i;
            while i < len && (bytes[i].is_ascii_alphanumeric() || bytes[i] == b'_') {
                i += 1;
            }
            let word = &line[start..i];
            // Macro invocations like println!
            if i < len && bytes[i] == b'!' && is_rust_keyword(word) {
                // keyword followed by ! — treat as keyword
            }
            if is_rust_keyword(word) {
                blocks.push(TextBlock {
                    char_pos: start,
                    style: SyntaxStyle::Keyword,
                });
                if i < len {
                    blocks.push(TextBlock {
                        char_pos: i,
                        style: SyntaxStyle::Normal,
                    });
                }
            }
            continue;
        }

        i += 1;
    }

    (blocks, new_cookie)
}

// Cookie flags for Python parser state (triple-quoted strings)
const COOKIE_TRIPLE_DOUBLE: u32 = 0x0010;
const COOKIE_TRIPLE_SINGLE: u32 = 0x0020;

pub fn highlight_python(cookie: u32, line: &str) -> (Vec<TextBlock>, u32) {
    let mut blocks = Vec::new();
    let mut new_cookie = cookie;
    let bytes = line.as_bytes();
    let len = bytes.len();
    let mut i = 0;

    // Continue triple-quoted string from previous line
    if cookie & (COOKIE_TRIPLE_DOUBLE | COOKIE_TRIPLE_SINGLE) != 0 {
        let closer = if cookie & COOKIE_TRIPLE_DOUBLE != 0 {
            b'"'
        } else {
            b'\''
        };
        blocks.push(TextBlock {
            char_pos: 0,
            style: SyntaxStyle::String,
        });
        while i + 2 < len {
            if bytes[i] == closer && bytes[i + 1] == closer && bytes[i + 2] == closer {
                i += 3;
                new_cookie &= !(COOKIE_TRIPLE_DOUBLE | COOKIE_TRIPLE_SINGLE);
                if i < len {
                    blocks.push(TextBlock {
                        char_pos: i,
                        style: SyntaxStyle::Normal,
                    });
                }
                break;
            }
            i += 1;
        }
        if new_cookie & (COOKIE_TRIPLE_DOUBLE | COOKIE_TRIPLE_SINGLE) != 0 {
            return (blocks, new_cookie);
        }
    }

    while i < len {
        if bytes[i].is_ascii_whitespace() {
            i += 1;
            continue;
        }

        // Comment
        if bytes[i] == b'#' {
            blocks.push(TextBlock {
                char_pos: i,
                style: SyntaxStyle::Comment,
            });
            return (blocks, new_cookie);
        }

        // Decorator
        if bytes[i] == b'@' && (i == 0 || line[..i].trim().is_empty()) {
            blocks.push(TextBlock {
                char_pos: i,
                style: SyntaxStyle::Preprocessor,
            });
            i += 1;
            while i < len
                && (bytes[i].is_ascii_alphanumeric() || bytes[i] == b'_' || bytes[i] == b'.')
            {
                i += 1;
            }
            if i < len {
                blocks.push(TextBlock {
                    char_pos: i,
                    style: SyntaxStyle::Normal,
                });
            }
            continue;
        }

        // String prefix (f, r, b, u, or combinations) followed by quote
        let string_start = i;
        if (bytes[i] == b'f'
            || bytes[i] == b'r'
            || bytes[i] == b'b'
            || bytes[i] == b'u'
            || bytes[i] == b'F'
            || bytes[i] == b'R'
            || bytes[i] == b'B'
            || bytes[i] == b'U')
            && i + 1 < len
        {
            let mut prefix_end = i + 1;
            // Allow two-char prefixes like fr, rb, etc.
            if prefix_end < len
                && (bytes[prefix_end] == b'f'
                    || bytes[prefix_end] == b'r'
                    || bytes[prefix_end] == b'b'
                    || bytes[prefix_end] == b'F'
                    || bytes[prefix_end] == b'R'
                    || bytes[prefix_end] == b'B')
                && prefix_end + 1 < len
                && (bytes[prefix_end + 1] == b'"' || bytes[prefix_end + 1] == b'\'')
            {
                prefix_end += 1;
            }
            if prefix_end < len && (bytes[prefix_end] == b'"' || bytes[prefix_end] == b'\'') {
                i = prefix_end;
                // Fall through to string handling below
            }
        }

        // Triple-quoted string
        if i + 2 < len
            && ((bytes[i] == b'"' && bytes[i + 1] == b'"' && bytes[i + 2] == b'"')
                || (bytes[i] == b'\'' && bytes[i + 1] == b'\'' && bytes[i + 2] == b'\''))
        {
            let quote = bytes[i];
            blocks.push(TextBlock {
                char_pos: string_start,
                style: SyntaxStyle::String,
            });
            i += 3;
            let mut found_close = false;
            while i + 2 < len {
                if bytes[i] == b'\\' && i + 1 < len {
                    i += 2;
                    continue;
                }
                if bytes[i] == quote && bytes[i + 1] == quote && bytes[i + 2] == quote {
                    i += 3;
                    found_close = true;
                    break;
                }
                i += 1;
            }
            if !found_close {
                new_cookie |= if quote == b'"' {
                    COOKIE_TRIPLE_DOUBLE
                } else {
                    COOKIE_TRIPLE_SINGLE
                };
                return (blocks, new_cookie);
            }
            if i < len {
                blocks.push(TextBlock {
                    char_pos: i,
                    style: SyntaxStyle::Normal,
                });
            }
            continue;
        }

        // Regular string
        if bytes[i] == b'"' || bytes[i] == b'\'' {
            let quote = bytes[i];
            blocks.push(TextBlock {
                char_pos: string_start,
                style: SyntaxStyle::String,
            });
            i += 1;
            while i < len {
                if bytes[i] == b'\\' && i + 1 < len {
                    i += 2;
                    continue;
                }
                if bytes[i] == quote {
                    i += 1;
                    if i < len {
                        blocks.push(TextBlock {
                            char_pos: i,
                            style: SyntaxStyle::Normal,
                        });
                    }
                    break;
                }
                i += 1;
            }
            continue;
        }

        // Reset i if we advanced for a prefix but didn't find a string
        if i != string_start {
            i = string_start;
        }

        // Number
        if bytes[i].is_ascii_digit()
            || (bytes[i] == b'.' && i + 1 < len && bytes[i + 1].is_ascii_digit())
        {
            blocks.push(TextBlock {
                char_pos: i,
                style: SyntaxStyle::Number,
            });
            if bytes[i] == b'0' && i + 1 < len {
                match bytes[i + 1] {
                    b'x' | b'X' => {
                        i += 2;
                        while i < len && (bytes[i].is_ascii_hexdigit() || bytes[i] == b'_') {
                            i += 1;
                        }
                    }
                    b'b' | b'B' => {
                        i += 2;
                        while i < len && (bytes[i] == b'0' || bytes[i] == b'1' || bytes[i] == b'_')
                        {
                            i += 1;
                        }
                    }
                    b'o' | b'O' => {
                        i += 2;
                        while i < len
                            && ((bytes[i] >= b'0' && bytes[i] <= b'7') || bytes[i] == b'_')
                        {
                            i += 1;
                        }
                    }
                    _ => {
                        while i < len
                            && (bytes[i].is_ascii_digit()
                                || bytes[i] == b'.'
                                || bytes[i] == b'_'
                                || bytes[i] == b'e'
                                || bytes[i] == b'E')
                        {
                            i += 1;
                        }
                    }
                }
            } else {
                while i < len
                    && (bytes[i].is_ascii_digit()
                        || bytes[i] == b'.'
                        || bytes[i] == b'_'
                        || bytes[i] == b'e'
                        || bytes[i] == b'E')
                {
                    i += 1;
                }
            }
            // Skip j suffix for complex numbers
            if i < len && (bytes[i] == b'j' || bytes[i] == b'J') {
                i += 1;
            }
            if i < len {
                blocks.push(TextBlock {
                    char_pos: i,
                    style: SyntaxStyle::Normal,
                });
            }
            continue;
        }

        // Identifier/keyword
        if bytes[i].is_ascii_alphabetic() || bytes[i] == b'_' {
            let start = i;
            while i < len && (bytes[i].is_ascii_alphanumeric() || bytes[i] == b'_') {
                i += 1;
            }
            let word = &line[start..i];
            if is_python_keyword(word) {
                blocks.push(TextBlock {
                    char_pos: start,
                    style: SyntaxStyle::Keyword,
                });
                if i < len {
                    blocks.push(TextBlock {
                        char_pos: i,
                        style: SyntaxStyle::Normal,
                    });
                }
            }
            continue;
        }

        i += 1;
    }

    (blocks, new_cookie)
}

pub fn highlight_markdown(_cookie: u32, line: &str) -> (Vec<TextBlock>, u32) {
    let mut blocks = Vec::new();
    let trimmed = line.trim_start();

    if trimmed.starts_with("### ") {
        blocks.push(TextBlock {
            char_pos: 0,
            style: SyntaxStyle::MdHeading3,
        });
    } else if trimmed.starts_with("## ") {
        blocks.push(TextBlock {
            char_pos: 0,
            style: SyntaxStyle::MdHeading2,
        });
    } else if trimmed.starts_with("# ") {
        blocks.push(TextBlock {
            char_pos: 0,
            style: SyntaxStyle::MdHeading1,
        });
    } else if trimmed.starts_with("- ") || trimmed.starts_with("* ") || trimmed.starts_with("+ ") {
        let offset = line.len() - trimmed.len();
        blocks.push(TextBlock {
            char_pos: offset,
            style: SyntaxStyle::MdBullet,
        });
        blocks.push(TextBlock {
            char_pos: offset + 2,
            style: SyntaxStyle::Normal,
        });
        // Inline formatting within list items
        highlight_md_inline(line, offset + 2, &mut blocks);
    } else if trimmed.starts_with(|c: char| c.is_ascii_digit()) && trimmed.contains(". ") {
        // Ordered list
        let offset = line.len() - trimmed.len();
        if let Some(dot_pos) = trimmed.find(". ") {
            blocks.push(TextBlock {
                char_pos: offset,
                style: SyntaxStyle::MdBullet,
            });
            blocks.push(TextBlock {
                char_pos: offset + dot_pos + 2,
                style: SyntaxStyle::Normal,
            });
            highlight_md_inline(line, offset + dot_pos + 2, &mut blocks);
        }
    } else if trimmed.starts_with('|') {
        // Table row
        blocks.push(TextBlock {
            char_pos: 0,
            style: SyntaxStyle::Normal,
        });
    } else if trimmed.starts_with('>') {
        blocks.push(TextBlock {
            char_pos: 0,
            style: SyntaxStyle::MdMarker,
        });
    } else {
        highlight_md_inline(line, 0, &mut blocks);
    }

    (blocks, 0)
}

fn highlight_md_inline(line: &str, start: usize, blocks: &mut Vec<TextBlock>) {
    let bytes = line.as_bytes();
    let len = bytes.len();
    let mut i = start;

    while i < len {
        // Bold **text**
        if i + 1 < len && bytes[i] == b'*' && bytes[i + 1] == b'*' {
            blocks.push(TextBlock {
                char_pos: i,
                style: SyntaxStyle::MdBold,
            });
            i += 2;
            while i + 1 < len {
                if bytes[i] == b'*' && bytes[i + 1] == b'*' {
                    i += 2;
                    break;
                }
                i += 1;
            }
            if i < len {
                blocks.push(TextBlock {
                    char_pos: i,
                    style: SyntaxStyle::Normal,
                });
            }
            continue;
        }

        // Italic *text*
        if bytes[i] == b'*' && (i + 1 < len && bytes[i + 1] != b'*') {
            blocks.push(TextBlock {
                char_pos: i,
                style: SyntaxStyle::MdItalic,
            });
            i += 1;
            while i < len {
                if bytes[i] == b'*' {
                    i += 1;
                    break;
                }
                i += 1;
            }
            if i < len {
                blocks.push(TextBlock {
                    char_pos: i,
                    style: SyntaxStyle::Normal,
                });
            }
            continue;
        }

        // Link [text](url)
        if bytes[i] == b'[' {
            blocks.push(TextBlock {
                char_pos: i,
                style: SyntaxStyle::MdLink,
            });
            i += 1;
            while i < len && bytes[i] != b']' {
                i += 1;
            }
            if i < len {
                i += 1; // ]
            }
            if i < len && bytes[i] == b'(' {
                while i < len && bytes[i] != b')' {
                    i += 1;
                }
                if i < len {
                    i += 1; // )
                }
            }
            if i < len {
                blocks.push(TextBlock {
                    char_pos: i,
                    style: SyntaxStyle::Normal,
                });
            }
            continue;
        }

        // Inline code `text`
        if bytes[i] == b'`' {
            blocks.push(TextBlock {
                char_pos: i,
                style: SyntaxStyle::String,
            });
            i += 1;
            while i < len && bytes[i] != b'`' {
                i += 1;
            }
            if i < len {
                i += 1;
            }
            if i < len {
                blocks.push(TextBlock {
                    char_pos: i,
                    style: SyntaxStyle::Normal,
                });
            }
            continue;
        }

        i += 1;
    }
}

pub fn highlight_line(syntax: SyntaxType, cookie: u32, line: &str) -> (Vec<TextBlock>, u32) {
    match syntax {
        SyntaxType::Cpp => highlight_cpp(cookie, line),
        SyntaxType::Rust => highlight_rust(cookie, line),
        SyntaxType::Python => highlight_python(cookie, line),
        SyntaxType::Markdown => highlight_markdown(cookie, line),
        SyntaxType::Plain => (vec![], 0),
    }
}

pub fn build_attrs_list(
    line: &str,
    blocks: &[TextBlock],
    default_attrs: &Attrs,
    font_family: Family,
) -> AttrsList {
    let mut attrs_list = AttrsList::new(default_attrs);

    for (i, block) in blocks.iter().enumerate() {
        let end = if i + 1 < blocks.len() {
            blocks[i + 1].char_pos
        } else {
            line.len()
        };

        if block.char_pos >= line.len() {
            continue;
        }
        let actual_end = end.min(line.len());
        let base = default_attrs.clone();

        let attrs = match block.style {
            SyntaxStyle::Normal => base.color(block.style.color()),
            SyntaxStyle::Keyword => base.family(font_family).color(block.style.color()),
            SyntaxStyle::Comment => base
                .family(font_family)
                .color(block.style.color())
                .style(Style::Italic),
            SyntaxStyle::String => base.family(font_family).color(block.style.color()),
            SyntaxStyle::Number => base.family(font_family).color(block.style.color()),
            SyntaxStyle::Preprocessor => base.family(font_family).color(block.style.color()),
            SyntaxStyle::MdHeading1 => base
                .family(font_family)
                .weight(Weight::BOLD)
                .color(block.style.color()),
            SyntaxStyle::MdHeading2 => base
                .family(font_family)
                .weight(Weight::BOLD)
                .color(block.style.color()),
            SyntaxStyle::MdHeading3 => base
                .family(font_family)
                .weight(Weight::BOLD)
                .color(block.style.color()),
            SyntaxStyle::MdBold => base
                .family(font_family)
                .weight(Weight::BOLD)
                .color(block.style.color()),
            SyntaxStyle::MdItalic => base
                .family(font_family)
                .style(Style::Italic)
                .color(block.style.color()),
            SyntaxStyle::MdLink => base.family(font_family).color(block.style.color()),
            SyntaxStyle::MdMarker | SyntaxStyle::MdBullet => {
                base.family(font_family).color(block.style.color())
            }
        };

        attrs_list.add_span(block.char_pos..actual_end, &attrs);
    }

    attrs_list
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn detect_syntax_extensions() {
        assert_eq!(detect_syntax(Some("rs")), SyntaxType::Rust);
        assert_eq!(detect_syntax(Some("py")), SyntaxType::Python);
        assert_eq!(detect_syntax(Some("pyw")), SyntaxType::Python);
        assert_eq!(detect_syntax(Some("cpp")), SyntaxType::Cpp);
        assert_eq!(detect_syntax(Some("js")), SyntaxType::Cpp);
        assert_eq!(detect_syntax(Some("md")), SyntaxType::Markdown);
        assert_eq!(detect_syntax(Some("markdown")), SyntaxType::Markdown);
        assert_eq!(detect_syntax(Some("txt")), SyntaxType::Plain);
        assert_eq!(detect_syntax(None), SyntaxType::Plain);
    }

    #[test]
    fn md_highlight_heading() {
        let (blocks, _) = highlight_markdown(0, "# Hello");
        assert!(!blocks.is_empty(), "h1 should have blocks");
        assert_eq!(blocks[0].char_pos, 0, "h1 pos");
        assert_eq!(blocks[0].style, SyntaxStyle::MdHeading1);

        let (blocks, _) = highlight_markdown(0, "## World");
        assert!(!blocks.is_empty(), "h2 should have blocks");
        assert_eq!(blocks[0].char_pos, 0, "h2 pos");
        assert_eq!(blocks[0].style, SyntaxStyle::MdHeading2);
    }

    #[test]
    fn md_highlight_bold() {
        let (blocks, _) = highlight_markdown(0, "some **bold** text");
        assert!(blocks.len() >= 2, "bold block count: {}", blocks.len());
        let has_bold = blocks.iter().any(|b| b.style == SyntaxStyle::MdBold);
        assert!(has_bold, "should have bold block");
    }

    #[test]
    fn md_highlight_italic() {
        let (blocks, _) = highlight_markdown(0, "some *italic* text");
        assert!(blocks.len() >= 2, "italic block count: {}", blocks.len());
        let has_italic = blocks.iter().any(|b| b.style == SyntaxStyle::MdItalic);
        assert!(has_italic, "should have italic block");
    }

    #[test]
    fn md_highlight_link() {
        let (blocks, _) = highlight_markdown(0, "click [here](http://example.com) now");
        assert!(blocks.len() >= 2, "link block count: {}", blocks.len());
        let has_link = blocks.iter().any(|b| b.style == SyntaxStyle::MdLink);
        assert!(has_link, "should have link block");
    }

    #[test]
    fn md_highlight_list() {
        let (blocks, _) = highlight_markdown(0, "- list item");
        assert!(blocks.len() >= 2, "list block count: {}", blocks.len());
        assert_eq!(blocks[0].char_pos, 0, "list bullet pos");
    }

    #[test]
    fn cpp_highlight_keyword() {
        let (blocks, _) = highlight_cpp(0, "int main() {");
        assert!(blocks.len() >= 2, "cpp keyword blocks: {}", blocks.len());
        assert_eq!(blocks[0].style, SyntaxStyle::Keyword);
    }

    #[test]
    fn cpp_highlight_string() {
        let (blocks, _) = highlight_cpp(0, r#"char* s = "hello";"#);
        let has_string = blocks.iter().any(|b| b.style == SyntaxStyle::String);
        assert!(has_string, "should have string block");
    }

    #[test]
    fn cpp_highlight_comment() {
        let (blocks, _) = highlight_cpp(0, "// comment");
        assert!(!blocks.is_empty());
        assert_eq!(blocks[0].style, SyntaxStyle::Comment);
    }

    #[test]
    fn cpp_multiline_comment_cookie() {
        let (_, cookie) = highlight_cpp(0, "/* start");
        assert_ne!(cookie, 0, "cookie should indicate open comment");
        let (blocks, cookie2) = highlight_cpp(cookie, "still comment */");
        assert_eq!(blocks[0].style, SyntaxStyle::Comment);
        assert_eq!(cookie2, 0, "cookie should be cleared after close");
    }

    #[test]
    fn highlight_line_dispatch() {
        let (blocks, _) = highlight_line(SyntaxType::Markdown, 0, "# Title");
        assert!(!blocks.is_empty());
        assert_eq!(blocks[0].style, SyntaxStyle::MdHeading1);

        let (blocks, _) = highlight_line(SyntaxType::Cpp, 0, "int test() {}");
        assert!(!blocks.is_empty());

        let (blocks, _) = highlight_line(SyntaxType::Rust, 0, "fn test() {}");
        assert!(!blocks.is_empty());

        let (blocks, _) = highlight_line(SyntaxType::Python, 0, "def test():");
        assert!(!blocks.is_empty());

        let (blocks, _) = highlight_line(SyntaxType::Plain, 0, "hello");
        assert!(blocks.is_empty());
    }

    #[test]
    fn rust_highlight_keyword() {
        let (blocks, _) = highlight_rust(0, "fn main() {");
        assert!(blocks.len() >= 2, "rust keyword blocks: {}", blocks.len());
        assert_eq!(blocks[0].style, SyntaxStyle::Keyword);
    }

    #[test]
    fn rust_highlight_attribute() {
        let (blocks, _) = highlight_rust(0, "#[derive(Debug)]");
        assert!(!blocks.is_empty());
        assert_eq!(blocks[0].style, SyntaxStyle::Preprocessor);
    }

    #[test]
    fn rust_highlight_raw_string() {
        let (blocks, _) = highlight_rust(0, r##"let s = r#"hello"#;"##);
        let has_string = blocks.iter().any(|b| b.style == SyntaxStyle::String);
        assert!(has_string, "should have string block for raw string");
    }

    #[test]
    fn rust_highlight_comment() {
        let (blocks, _) = highlight_rust(0, "/// doc comment");
        assert!(!blocks.is_empty());
        assert_eq!(blocks[0].style, SyntaxStyle::Comment);
    }

    #[test]
    fn rust_multiline_comment() {
        let (_, cookie) = highlight_rust(0, "/* start");
        assert_ne!(cookie, 0);
        let (blocks, cookie2) = highlight_rust(cookie, "end */");
        assert_eq!(blocks[0].style, SyntaxStyle::Comment);
        assert_eq!(cookie2, 0);
    }

    #[test]
    fn python_highlight_keyword() {
        let (blocks, _) = highlight_python(0, "def hello():");
        assert!(!blocks.is_empty());
        assert_eq!(blocks[0].style, SyntaxStyle::Keyword);
    }

    #[test]
    fn python_highlight_comment() {
        let (blocks, _) = highlight_python(0, "# comment");
        assert!(!blocks.is_empty());
        assert_eq!(blocks[0].style, SyntaxStyle::Comment);
    }

    #[test]
    fn python_highlight_decorator() {
        let (blocks, _) = highlight_python(0, "@staticmethod");
        assert!(!blocks.is_empty());
        assert_eq!(blocks[0].style, SyntaxStyle::Preprocessor);
    }

    #[test]
    fn python_highlight_fstring() {
        let (blocks, _) = highlight_python(0, r#"x = f"hello {name}""#);
        let has_string = blocks.iter().any(|b| b.style == SyntaxStyle::String);
        assert!(has_string, "should have string block for f-string");
    }

    #[test]
    fn python_triple_quoted_string() {
        let (_, cookie) = highlight_python(0, r#"s = """hello"#);
        assert_ne!(cookie, 0, "cookie should indicate open triple string");
        let (blocks, cookie2) = highlight_python(cookie, r#"world""""#);
        assert_eq!(blocks[0].style, SyntaxStyle::String);
        assert_eq!(cookie2, 0, "cookie should be cleared after close");
    }

    #[test]
    fn python_highlight_number() {
        let (blocks, _) = highlight_python(0, "x = 0xFF + 3.14 + 0b1010");
        let num_count = blocks
            .iter()
            .filter(|b| b.style == SyntaxStyle::Number)
            .count();
        assert!(
            num_count >= 3,
            "should have at least 3 number blocks, got {num_count}"
        );
    }
}
