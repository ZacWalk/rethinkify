//! `AppConfig` — persistent user settings (splitter ratios, recent files/folders, word wrap,
//! font sizes, window dimensions). Serialized as JSON to the OS config directory.

use directories::ProjectDirs;
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::PathBuf;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AppConfig {
    pub panel_ratio: f32,
    pub console_ratio: f32,
    pub root_folder: PathBuf,
    pub recent_file: Option<PathBuf>,
    pub recent_root_folders: Vec<PathBuf>,
    pub window_width: u32,
    pub window_height: u32,
    pub word_wrap: bool,
    pub font_size: f32,
    pub list_font_size: f32,
    pub console_font_size: f32,
}

impl Default for AppConfig {
    fn default() -> Self {
        Self {
            panel_ratio: 0.20,
            console_ratio: 0.80,
            root_folder: std::env::current_dir().unwrap_or_else(|_| PathBuf::from(".")),
            recent_file: None,
            recent_root_folders: Vec::new(),
            window_width: 1280,
            window_height: 800,
            word_wrap: true,
            font_size: 14.0,
            list_font_size: 13.0,
            console_font_size: 13.0,
        }
    }
}

impl AppConfig {
    fn path() -> Option<PathBuf> {
        ProjectDirs::from("com", "walker", "Rethinkify")
            .map(|dirs| dirs.config_dir().join("config.json"))
    }

    pub fn load() -> Self {
        if let Some(path) = Self::path()
            && path.exists()
            && let Ok(data) = fs::read_to_string(&path)
            && let Ok(config) = serde_json::from_str(&data)
        {
            return config;
        }
        Self::default()
    }

    pub fn save(&self) {
        if let Some(path) = Self::path()
            && let Some(parent) = path.parent()
        {
            let _ = fs::create_dir_all(parent);
            if let Ok(data) = serde_json::to_string_pretty(self) {
                let _ = fs::write(path, data);
            }
        }
    }

    pub fn remember_root_folder(&mut self, path: &PathBuf) {
        self.recent_root_folders.retain(|p| p != path);
        self.recent_root_folders.insert(0, path.clone());
        if self.recent_root_folders.len() > 8 {
            self.recent_root_folders.truncate(8);
        }
    }
}
