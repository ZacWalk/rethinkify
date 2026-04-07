//! UI primitives — dark theme color constants, `fill_rect` pixel drawing, `blend` alpha
//! compositing, and `rgb` helper. All colors and drawing operate on the `u32` pixel buffer.

#![allow(dead_code)]

/// Color constants matching the C++ Rethinkify theme
pub const MAIN_BG: u32 = 0x1E1E1E;
pub const PANEL_BG: u32 = 0x252526;
pub const CONSOLE_BG: u32 = 0x1E1E1E;
pub const CONSOLE_INPUT_BG: u32 = 0x2A2A2A;
pub const SPLITTER_COLOR: u32 = 0x2C2C2C;
pub const SPLITTER_HOVER: u32 = 0x555555;
pub const SPLITTER_DRAG: u32 = 0x1166CC;
pub const TEXT_COLOR: u32 = 0xDEDEDE;
pub const FOLDER_COLOR: u32 = 0xEECC22;
pub const DIM_TEXT: u32 = 0x808080;
pub const MODIFIED_COLOR: u32 = 0xFF4444;
pub const SELECTION_BG: u32 = 0x264F78;
pub const CURRENT_LINE_BG: u32 = 0x282828;
pub const GUTTER_BG: u32 = 0x1A1A1A;
pub const LINE_NUMBER_COLOR: u32 = 0x5A5A5A;
pub const LIST_INACTIVE_BG: u32 = 0x2A2D2E;
pub const SEARCH_MATCH_BG: u32 = 0x613214;
pub const SCROLLBAR_BG: u32 = 0x1E1E1E;
pub const SCROLLBAR_THUMB: u32 = 0x424242;
pub const SCROLLBAR_HOVER: u32 = 0x4F4F4F;
pub const COMMAND_ECHO_COLOR: u32 = 0xDEDE00;
pub const CHEVRON_COLOR: u32 = 0x888888;

/// Pack RGB bytes into u32
pub fn rgb(r: u8, g: u8, b: u8) -> u32 {
    (r as u32) << 16 | (g as u32) << 8 | b as u32
}

/// Alpha-blend foreground over background
pub fn blend(fg: u32, bg: u32, alpha: f32) -> u32 {
    let fr = ((fg >> 16) & 0xFF) as f32;
    let fg_val = ((fg >> 8) & 0xFF) as f32;
    let fb = (fg & 0xFF) as f32;
    let br = ((bg >> 16) & 0xFF) as f32;
    let bg_val = ((bg >> 8) & 0xFF) as f32;
    let bb = (bg & 0xFF) as f32;
    let r = (fr * alpha + br * (1.0 - alpha)) as u32;
    let g = (fg_val * alpha + bg_val * (1.0 - alpha)) as u32;
    let b = (fb * alpha + bb * (1.0 - alpha)) as u32;
    (r << 16) | (g << 8) | b
}

/// Draw a filled rectangle into a pixel buffer
#[allow(clippy::too_many_arguments)]
pub fn fill_rect(
    pixels: &mut [u32],
    buf_width: u32,
    buf_height: u32,
    x: u32,
    y: u32,
    w: u32,
    h: u32,
    color: u32,
) {
    let x_end = (x + w).min(buf_width) as usize;
    let x = x as usize;
    if x >= x_end {
        return;
    }
    for row in y..((y + h).min(buf_height)) {
        let start = row as usize * buf_width as usize + x;
        let end = row as usize * buf_width as usize + x_end;
        pixels[start..end].fill(color);
    }
}
