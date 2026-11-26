//! The player's settings, kept in settings.toml beside the launcher, and what they are as the games'
//! arguments and key file.

use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

/// Controls as in input/keys.h: name, what the player reads, default keys.
pub const CONTROLS: [(&str, &str, &str); 20] = [
    ("move_forward", "Move forward", "W"),
    ("move_back", "Move back", "S"),
    ("move_left", "Move left", "A"),
    ("move_right", "Move right", "D"),
    ("right_trigger", "Fire", "MOUSE1"),
    ("left_trigger", "Aim", "MOUSE2"),
    ("right_stick_press", "Scope", "MOUSE3 Z"),
    ("left_stick_press", "Stance", "C LCTRL"),
    ("left_bumper", "Engagement mode", "T"),
    ("right_bumper", "Camera side / command view", "F"),
    ("a", "Reload / fire mode / select", "R ENTER"),
    ("b", "Weapons (tap: last) / back", "Q BACKSPACE"),
    ("x", "Vision", "V N"),
    ("y", "Action", "E SPACE"),
    ("dpad_up", "Order up", "3 WHEELUP UP"),
    ("dpad_down", "Order down", "4 WHEELDOWN DOWN"),
    ("dpad_left", "Previous Cross-Com unit", "1 LEFT"),
    ("dpad_right", "Next Cross-Com unit", "2 RIGHT"),
    ("start", "Pause", "ESC"),
    ("back", "Tactical map", "TAB M"),
];

#[derive(Clone, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct Settings {
    pub renderer: String,
    pub scale: u32,
    pub fullscreen: bool,
    pub vsync: bool,
    /// Percent of the game's own (GRAW 2).
    pub field_of_view: u32,
    /// 16:9, 21:9, 32:9 or display (GRAW 2).
    pub aspect_ratio: String,
    pub anisotropy: u32,
    pub ambient_occlusion: u32,
    pub sharpness: f32,
    pub mip_bias: f32,
    pub haze: String,
    pub haze_variant: u32,
    pub keyboard: bool,
    pub prompts: String,
    pub mouse_sensitivity: f32,
    pub mouse_invert: bool,
    /// Folders with a game's files, where the player has them already.
    pub folders: BTreeMap<String, String>,
    pub keys: BTreeMap<String, String>,
}

impl Default for Settings {
    fn default() -> Self {
        Settings {
            renderer: "native".into(),
            scale: 2,
            fullscreen: true,
            vsync: true,
            field_of_view: 100,
            aspect_ratio: "16:9".into(),
            anisotropy: 16,
            ambient_occlusion: 0,
            sharpness: 0.0,
            mip_bias: 0.0,
            haze: "fixed".into(),
            haze_variant: 3,
            keyboard: true,
            prompts: "auto".into(),
            mouse_sensitivity: 1.0,
            mouse_invert: false,
            folders: BTreeMap::new(),
            keys: default_keys(),
        }
    }
}

pub fn default_keys() -> BTreeMap<String, String> {
    CONTROLS.iter().map(|(n, _, k)| (n.to_string(), k.to_string())).collect()
}

fn path() -> std::path::PathBuf {
    crate::game::home().join("settings.toml")
}

impl Settings {
    /// The settings, and a message if the file could not be read (it is then kept as .bad).
    pub fn load() -> (Settings, Option<String>) {
        let Ok(text) = std::fs::read_to_string(path()) else {
            return (Settings::default(), None);
        };
        match Settings::parse(&text) {
            Ok(s) => (s, None),
            Err(e) => {
                let bad = path().with_extension("toml.bad");
                let _ = std::fs::copy(path(), &bad);
                (Settings::default(), Some(format!("settings.toml could not be read ({e}). Defaults are used; the old file is settings.toml.bad.")))
            }
        }
    }

    fn parse(text: &str) -> Result<Settings, String> {
        let mut s: Settings = toml::from_str(text).map_err(|e| e.message().to_string())?;
        for (name, keys) in default_keys() {
            s.keys.entry(name).or_insert(keys);
        }
        // Values the games would refuse to start with.
        let d = Settings::default();
        if !["native", "emulated"].contains(&&s.renderer[..]) {
            s.renderer = d.renderer;
        }
        if !["fixed", "original", "off"].contains(&&s.haze[..]) {
            s.haze = d.haze;
        }
        if !["16:9", "21:9", "32:9", "display"].contains(&&s.aspect_ratio[..]) {
            s.aspect_ratio = d.aspect_ratio.clone();
        }
        if !["auto", "controller", "keyboard"].contains(&&s.prompts[..]) {
            s.prompts = d.prompts;
        }
        s.scale = s.scale.clamp(1, 3);
        s.anisotropy = s.anisotropy.clamp(1, 16);
        s.field_of_view = s.field_of_view.clamp(75, 150);
        s.ambient_occlusion = s.ambient_occlusion.min(100);
        s.haze_variant = s.haze_variant.clamp(1, 6);
        s.sharpness = number(s.sharpness, 0.0, 1.0, 0.0);
        s.mip_bias = number(s.mip_bias, -2.0, 2.0, 0.0);
        s.mouse_sensitivity = number(s.mouse_sensitivity, 0.05, 20.0, 1.0);
        Ok(s)
    }

    pub fn save(&self) -> Result<(), String> {
        let text = toml::to_string_pretty(self).map_err(|e| e.to_string())?;
        let partial = path().with_extension("toml.partial");
        std::fs::write(&partial, text).map_err(|e| e.to_string())?;
        std::fs::rename(&partial, path()).map_err(|e| e.to_string())
    }

    pub fn arguments(&self) -> Vec<String> {
        let mut a = vec![
            "--gpu.driver=d3d12".to_string(),
            "--kernel.headless=true".into(),
            "--ui.video_mode_width=1280".into(),
            "--ui.video_mode_height=720".into(),
            format!("--ui.fullscreen={}", self.fullscreen),
            format!("--native.renderer={}", self.renderer),
            format!("--native.resolution_scale={}", self.scale),
            format!("--gpu.resolution_scale_x={}", self.scale),
            format!("--gpu.resolution_scale_y={}", self.scale),
            format!("--native.vsync={}", self.vsync),
            format!("--native.field_of_view={}", self.field_of_view),
            format!("--native.aspect_ratio={}", self.aspect_ratio),
            format!("--native.anisotropy={}", self.anisotropy),
            format!("--native.ambient_occlusion={}", self.ambient_occlusion),
            format!("--native.sharpness={}", self.sharpness),
            format!("--native.mip_bias={}", self.mip_bias),
            format!("--native.haze={}", self.haze),
            format!("--native.haze_variant={}", self.haze_variant),
            format!("--native.prompts={}", self.prompts),
        ];
        if self.keyboard {
            a.push("--hid.drivers=keyboard,sdl3".into());
            a.push("--hid.shared_pads=true".into());
        } else {
            a.push("--hid.drivers=sdl3,xinput".into());
        }
        a
    }

    /// grawbox-keys.txt as input/keys.h reads it.
    pub fn keys_file(&self) -> String {
        let mut out = format!(
            "# Written by the launcher at each start.\nmouse_sensitivity = {}\nmouse_invert = {}\n",
            self.mouse_sensitivity, self.mouse_invert as u32
        );
        for (name, _, default) in CONTROLS {
            let keys = self.keys.get(name).map_or(default, |k| k.as_str());
            out += &format!("{name} = {keys}\n");
        }
        out
    }
}

fn number(v: f32, lo: f32, hi: f32, default: f32) -> f32 {
    if v.is_finite() { v.clamp(lo, hi) } else { default }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trip() {
        let mut s = Settings::default();
        s.scale = 3;
        s.keys.insert("a".into(), "R".into());
        let text = toml::to_string_pretty(&s).unwrap();
        assert!(Settings::parse(&text).unwrap() == s);
    }

    #[test]
    fn old_and_odd_files() {
        assert!(Settings::parse("").unwrap() == Settings::default());
        let s = Settings::parse("scale = 9\nhaze = 'soft'\nsharpness = nan\nunknown = 1").unwrap();
        assert_eq!((s.scale, &s.haze[..], s.sharpness), (3, "fixed", 0.0));
        assert!(Settings::parse("scale = 'x'").is_err());
    }

    #[test]
    fn controls_match_the_games() {
        let header = include_str!("../../input/keys.h");
        for (name, _, keys) in CONTROLS {
            let found = header.lines().any(|l| {
                l.contains(&format!("{{\"{name}\",")) && l.contains(&format!("\"{keys}\"}}"))
            });
            assert!(found, "{name} = {keys} differs from input/keys.h");
        }
    }
}
