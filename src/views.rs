//! View modes and format helpers. Defines `ViewMode` (EditText, MarkdownPreview, Hex, Csv)
//! and provides hex-dump formatting and CSV-to-table conversion for the document panel.

// ─── View mode ──────────────────────────────────────────────────────────────

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ViewMode {
    EditText,
    MarkdownPreview,
    Hex,
    Csv,
}

// ─── Hex view helpers ───────────────────────────────────────────────────────

pub fn format_hex_view(data: &[u8]) -> Vec<String> {
    let mut lines = Vec::new();
    for (offset, chunk) in data.chunks(16).enumerate() {
        let mut line = format!("{:08X}  ", offset * 16);
        for (i, &byte) in chunk.iter().enumerate() {
            if i == 8 {
                line.push(' ');
            }
            line.push_str(&format!("{:02X} ", byte));
        }
        // Pad remaining
        for i in chunk.len()..16 {
            if i == 8 {
                line.push(' ');
            }
            line.push_str("   ");
        }
        line.push_str(" |");
        for &byte in chunk {
            if byte.is_ascii_graphic() || byte == b' ' {
                line.push(byte as char);
            } else {
                line.push('.');
            }
        }
        line.push('|');
        lines.push(line);
    }
    lines
}

// ─── CSV helpers ────────────────────────────────────────────────────────────

pub fn parse_csv_line(line: &str) -> Vec<String> {
    let mut fields = Vec::new();
    let mut current = String::new();
    let mut in_quotes = false;
    let mut chars = line.chars().peekable();

    while let Some(c) = chars.next() {
        if in_quotes {
            if c == '"' {
                if chars.peek() == Some(&'"') {
                    current.push('"');
                    chars.next();
                } else {
                    in_quotes = false;
                }
            } else {
                current.push(c);
            }
        } else {
            match c {
                ',' => {
                    fields.push(current.trim().to_string());
                    current = String::new();
                }
                '"' => in_quotes = true,
                _ => current.push(c),
            }
        }
    }
    fields.push(current.trim().to_string());
    fields
}

pub fn format_csv_table(text: &str) -> Vec<String> {
    let rows: Vec<Vec<String>> = text.lines().map(parse_csv_line).collect();
    if rows.is_empty() {
        return vec![];
    }

    let col_count = rows.iter().map(|r| r.len()).max().unwrap_or(0);
    let mut col_widths = vec![0usize; col_count];
    for row in &rows {
        for (i, cell) in row.iter().enumerate() {
            col_widths[i] = col_widths[i].max(cell.len());
        }
    }
    // Cap column widths
    for w in &mut col_widths {
        *w = (*w).min(50);
    }

    let mut lines = Vec::new();
    for (row_idx, row) in rows.iter().enumerate() {
        let mut line = String::from("| ");
        for (i, cell) in row.iter().enumerate() {
            let width = col_widths.get(i).copied().unwrap_or(10);
            let is_number =
                cell.parse::<f64>().is_ok() || cell.ends_with('%') || cell.starts_with('$');
            if is_number {
                line.push_str(&format!("{:>width$}", cell, width = width));
            } else {
                line.push_str(&format!("{:<width$}", cell, width = width));
            }
            line.push_str(" | ");
        }
        lines.push(line);

        // Separator after header
        if row_idx == 0 {
            let mut sep = String::from("|-");
            for (i, _) in row.iter().enumerate() {
                let width = col_widths.get(i).copied().unwrap_or(10);
                sep.push_str(&"-".repeat(width));
                sep.push_str("-|-");
            }
            lines.push(sep);
        }
    }
    lines
}
