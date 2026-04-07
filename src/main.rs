//! Entry point for Rethinkify. Declares modules and launches the application via `app::run()`.

mod app;
mod background;
mod commands;
mod config;
mod document;
mod file_tree;
mod input;
mod menus;
mod render;
mod search;
mod syntax;
mod ui;
mod views;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    app::run()
}
