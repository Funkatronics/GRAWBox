//! GRAWBox launcher: imports the player's own files, keeps the settings and starts the games.
#![windows_subsystem = "windows"]

mod extract;
mod game;
mod keyboard;
mod report;
mod saves;
mod settings;
mod sha1;
mod theme;

use eframe::egui::{self, Color32, RichText};
use game::{Import, Ready, GAMES};
use settings::{Settings, CONTROLS};
use std::path::Path;
use std::process::Child;
use std::sync::atomic::Ordering;
use std::sync::mpsc;
use std::time::Duration;

use theme::{panel as card, TEAL as ACCENT};

#[derive(PartialEq, Clone, Copy)]
enum Tab {
    Play,
    Graphics,
    Controls,
    Files,
    Saves,
}

/// A control waiting for a key. Keys held when it began do not count.
struct Capture {
    control: &'static str,
    held: Vec<&'static str>,
}

struct App {
    settings: Settings,
    /// Each game's last save time and when it was read: the Saves tab walks the save folders at most
    /// every 2 s (and after an import or export), not every frame.
    last_saved: Vec<Option<(std::time::Instant, Option<std::time::SystemTime>)>>,
    saved: Settings,
    tab: Tab,
    running: Option<(usize, Child)>,
    checking: Option<(usize, mpsc::Receiver<Result<(), String>>)>,
    import: Option<Import>,
    capture: Option<Capture>,
    message: Option<(bool, String)>,
}

impl App {
    fn new(cc: &eframe::CreationContext<'_>) -> App {
        theme::style(&cc.egui_ctx);
        let (settings, warning) = Settings::load();
        let ready = GAMES.iter().any(|g| g.ready(&settings) == Ready::Yes);
        App {
            saved: settings.clone(),
            settings,
            last_saved: vec![None; GAMES.len()],
            // GRAWBOX_LAUNCHER_TAB (play, graphics, controls, files, saves): the first tab, for screenshots.
            tab: match std::env::var("GRAWBOX_LAUNCHER_TAB").as_deref() {
                Ok("graphics") => Tab::Graphics,
                Ok("controls") => Tab::Controls,
                Ok("files") => Tab::Files,
                Ok("saves") => Tab::Saves,
                Ok("play") => Tab::Play,
                _ if ready => Tab::Play,
                _ => Tab::Files,
            },
            running: None,
            checking: None,
            import: None,
            capture: None,
            message: warning.map(|w| (false, w)),
        }
    }

    fn error(&mut self, text: impl Into<String>) {
        self.message = Some((false, text.into()));
    }

    fn open(&mut self, path: &Path) {
        if self.import.is_some() {
            return self.error("Wait for the running import to finish.");
        }
        if path.is_dir() {
            return self.folder(path);
        }
        match Import::start(path, &self.settings) {
            Ok(i) => {
                self.tab = Tab::Files;
                self.message = None;
                self.import = Some(i);
            }
            Err(e) => self.error(e),
        }
    }

    /// A folder with files already extracted.
    fn folder(&mut self, path: &Path) {
        let game = GAMES.iter().find(|g| path.join(g.entry).is_file());
        match game {
            Some(g) => {
                self.settings.folders.insert(g.id.into(), path.display().to_string());
                self.message = Some((true, format!("{} uses this folder now", g.name)));
            }
            None => self.error("This folder has neither game's campaign files."),
        }
    }

    /// Checks the files on a thread, then starts the game (tick).
    fn play(&mut self, index: usize) {
        let settings = self.settings.clone();
        let (tx, rx) = mpsc::channel();
        std::thread::spawn(move || {
            let _ = tx.send(GAMES[index].verify(&settings));
        });
        self.message = None;
        self.checking = Some((index, rx));
    }

    fn tick(&mut self, ctx: &egui::Context) {
        if let Some((index, rx)) = &self.checking {
            let index = *index;
            match rx.try_recv() {
                Ok(Ok(())) => {
                    self.checking = None;
                    match GAMES[index].start(&self.settings) {
                        Ok(child) => self.running = Some((index, child)),
                        Err(e) => self.error(e),
                    }
                }
                Ok(Err(e)) => {
                    self.checking = None;
                    self.error(e);
                }
                Err(mpsc::TryRecvError::Disconnected) => self.checking = None,
                Err(mpsc::TryRecvError::Empty) => {}
            }
        }
        if let Some((index, child)) = &mut self.running {
            let index = *index;
            if let Ok(Some(status)) = child.try_wait() {
                self.running = None;
                if !status.success() {
                    self.error(format!(
                        "{} ended with an error ({status}). Its log is {}",
                        GAMES[index].name,
                        GAMES[index].log().display()
                    ));
                }
            }
        }
        let finished = self.import.as_ref().and_then(|i| i.result.lock().unwrap().take());
        if let Some(result) = finished {
            let import = self.import.take();
            if let (Ok(_), Some(i)) = (&result, &import) {
                if i.disc {
                    self.settings.folders.remove(i.game.id);
                }
            }
            self.message = Some(match result {
                Ok(t) => (true, t),
                Err(e) => (false, e),
            });
        }
        if let Some(c) = &mut self.capture {
            let down = keyboard::down();
            c.held.retain(|k| down.contains(k));
            if let Some(key) = down.iter().find(|k| !c.held.contains(k)) {
                self.settings.keys.insert(c.control.into(), key.to_string());
                self.capture = None;
            }
        }
        if self.settings != self.saved && !ctx.input(|i| i.pointer.any_down()) {
            self.saved = self.settings.clone();
            if let Err(e) = self.settings.save() {
                self.error(format!("The settings could not be saved: {e}"));
            }
        }
        let busy = self.import.is_some() || self.checking.is_some() || self.capture.is_some();
        if busy {
            ctx.request_repaint_after(Duration::from_millis(30));
        } else if self.running.is_some() {
            ctx.request_repaint_after(Duration::from_millis(500));
        }
    }

    fn play_tab(&mut self, ui: &mut egui::Ui) {
        for (i, g) in GAMES.iter().enumerate() {
            let ready = g.ready(&self.settings);
            card(ui, |ui| {
                ui.horizontal(|ui| {
                    ui.vertical(|ui| {
                        ui.label(RichText::new("TOM CLANCY'S GHOST RECON").size(12.0).color(ACCENT));
                        ui.label(RichText::new(g.name.to_uppercase()).font(theme::heading(30.0)));
                        ui.label(RichText::new(match ready {
                            Ready::Yes => "Ready",
                            Ready::NoProgram => "The port's program is missing from the programs folder",
                            Ready::NoDisc => "Needs your disc image",
                            Ready::NoUpdate => "Needs the title update",
                        }).weak());
                    });
                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                        let checking = self.checking.as_ref().is_some_and(|c| c.0 == i);
                        let running = self.running.as_ref().is_some_and(|r| r.0 == i);
                        let free = self.checking.is_none() && self.running.is_none() && self.import.is_none();
                        match ready {
                            Ready::Yes => {
                                let text = if running { "RUNNING" } else if checking { "CHECKING" } else { "PLAY" };
                                if theme::action(ui, text, free).clicked() {
                                    self.play(i);
                                }
                                // F10 in the game writes a report; this packs it and opens the issue form.
                                if ui.small_button("Report a problem").clicked() {
                                    match report::pack(g, &settings::path()) {
                                        Ok((zip, used)) => {
                                            let opened = report::show(&zip, g);
                                            let holds = match used {
                                                Some(name) => format!("the F10 report from {name}"),
                                                None => "the end of the log (no F10 report in the last 30 minutes)".into(),
                                            };
                                            let next = if opened {
                                                "Drag it into the issue form that just opened."
                                            } else {
                                                "Open a new issue on the GRAWBox GitHub page and attach it."
                                            };
                                            self.message = Some((true, format!("Report saved with {holds}: {}. {next}", zip.display())));
                                        }
                                        Err(e) => self.error(format!("The report could not be made: {e}")),
                                    }
                                }
                            }
                            Ready::NoDisc | Ready::NoUpdate => {
                                if theme::action(ui, "SET UP", true).clicked() {
                                    self.tab = Tab::Files;
                                }
                            }
                            Ready::NoProgram => {}
                        }
                    });
                });
            });
        }
        ui.add_space(6.0);
        ui.label(RichText::new("F9 in a game switches between the native and the emulated renderer.").weak());
        ui.label(RichText::new("Something wrong? Press F10 in the game right when it happens, then Report a problem here.").weak());
    }

    fn graphics_tab(&mut self, ui: &mut egui::Ui) {
        let s = &mut self.settings;
        card(ui, |ui| {
            grid(ui, "picture", |ui| {
                row(ui, "Renderer", "Native draws the games' own calls. Emulated is the older way, for comparison.", |ui| {
                    choice(ui, &mut s.renderer, &[("native", "Native"), ("emulated", "Emulated")]);
                });
                row(ui, "Resolution", "How finely the games draw. It is scaled to your screen.", |ui| {
                    pick(ui, &mut s.scale, &[(1, "720p"), (2, "1440p"), (3, "4K")]);
                });
                row(ui, "Fullscreen", "", |ui| { ui.checkbox(&mut s.fullscreen, ""); });
                row(ui, "VSync", "", |ui| { ui.checkbox(&mut s.vsync, ""); });
                row(ui, "Wait for new shaders", "Native renderer. On: the game waits briefly for shaders it hasn't built yet, so it can stutter the first time a new effect or area appears. Off: smoother, and new things can appear a moment late.", |ui| { ui.checkbox(&mut s.shader_wait, ""); });
                row(ui, "Field of view", "How much of the world you see. 100 % is the game's own.", |ui| {
                    ui.add(egui::Slider::new(&mut s.field_of_view, 75..=150).step_by(5.0).suffix(" %"));
                });
                row(ui, "Aspect ratio", "Wider pictures for wide screens, with the native renderer.", |ui| {
                    choice(ui, &mut s.aspect_ratio, &[("16:9", "16:9"), ("21:9", "21:9"), ("32:9", "32:9"), ("display", "Display")]);
                });
                row(ui, "HUD on wide screens", "Centered keeps menus and HUD 16:9 in the middle; stretched spreads them across.", |ui| {
                    choice(ui, &mut s.hud_layout, &[("centered", "Centered 16:9"), ("stretched", "Stretched")]);
                });
            });
        });
        let native = s.renderer == "native";
        card(ui, |ui| {
            ui.add_enabled_ui(native, |ui| {
                grid(ui, "quality", |ui| {
                    row(ui, "Texture filtering", "Sharper textures at an angle.", |ui| {
                        pick(ui, &mut s.anisotropy, &[(1, "Game's own"), (2, "2x"), (4, "4x"), (8, "8x"), (16, "16x")]);
                    });
                    row(ui, "Texture sharpness", "Above 0 is sharper and may shimmer.", |ui| {
                        let mut sharper = -s.mip_bias;
                        if ui.add(egui::Slider::new(&mut sharper, -2.0..=2.0).step_by(0.25)).changed() {
                            s.mip_bias = -sharper;
                        }
                    });
                    row(ui, "Ambient occlusion", "Shade in corners and creases.", |ui| {
                        ui.add(egui::Slider::new(&mut s.ambient_occlusion, 0..=100).suffix(" %"));
                    });
                    row(ui, "Sharpening", "", |ui| {
                        ui.add(egui::Slider::new(&mut s.sharpness, 0.0..=1.0).step_by(0.05));
                    });
                    row(ui, "Haze", "Fixed is the games' haze without its doubled picture.", |ui| {
                        choice(ui, &mut s.haze, &[("fixed", "Fixed"), ("original", "Original"), ("off", "Off")]);
                    });
                    if s.haze == "fixed" {
                        row(ui, "Haze motion", "", |ui| {
                            pick(ui, &mut s.haze_variant, &[
                                (1, "As the games"), (2, "As the games, slow"), (3, "Drift"), (4, "Drift, slow"),
                                (5, "Drift, gentle"), (6, "Drift, faint"),
                            ]);
                        });
                    }
                });
            });
            if !native {
                ui.label(RichText::new("These need the native renderer.").weak());
            }
        });
    }

    fn controls_tab(&mut self, ui: &mut egui::Ui) {
        let s = &mut self.settings;
        card(ui, |ui| {
            grid(ui, "input", |ui| {
                row(ui, "Keyboard and mouse", "A controller keeps working beside them.", |ui| { ui.checkbox(&mut s.keyboard, ""); });
                row(ui, "Button prompts", "", |ui| {
                    choice(ui, &mut s.prompts, &[("auto", "Automatic"), ("keyboard", "Keys"), ("controller", "Controller")]);
                });
                row(ui, "Mouse speed", "", |ui| {
                    ui.add(egui::Slider::new(&mut s.mouse_sensitivity, 0.1..=5.0).logarithmic(true));
                });
                row(ui, "Invert mouse", "", |ui| { ui.checkbox(&mut s.mouse_invert, ""); });
            });
        });
        let mut begin = None;
        card(ui, |ui| {
            ui.add_enabled_ui(s.keyboard, |ui| {
                ui.horizontal(|ui| {
                    ui.label(RichText::new("Keys").strong());
                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                        if ui.button("Defaults").clicked() {
                            s.keys = settings::default_keys();
                        }
                        if ui.button("Classic").on_hover_text("The first key scheme, the same keys in both games").clicked() {
                            s.keys = settings::classic_keys();
                        }
                    });
                });
                ui.label(RichText::new("Click, then press a key or mouse button.").weak());
                grid(ui, "keys", |ui| {
                    for (name, label, _) in CONTROLS {
                        ui.label(label);
                        let waiting = self.capture.as_ref().is_some_and(|c| c.control == name);
                        let text = if waiting { "Press a key".into() } else { s.keys.get(name).cloned().unwrap_or_default().replace(' ', ", ") };
                        let button = ui.add(egui::Button::new(text).min_size(egui::vec2(220.0, 0.0)));
                        if button.clicked() {
                            begin = Some(name);
                        }
                        ui.end_row();
                    }
                });
            });
        });
        if let Some(control) = begin {
            self.capture = Some(Capture { control, held: keyboard::down() });
        }
    }

    fn saves_tab(&mut self, ui: &mut egui::Ui) {
        card(ui, |ui| {
            ui.label(RichText::new("Your saves").strong());
            ui.label("Each game keeps its saves here. Export copies them to a folder you choose. Import replaces them with an exported copy; the saves it replaces are kept in the game's \"save backups\" folder.");
        });
        for i in 0..GAMES.len() {
            let g = &GAMES[i];
            let running = self.running.as_ref().is_some_and(|r| r.0 == i);
            card(ui, |ui| {
                ui.label(RichText::new(g.name).strong());
                let user = g.user_dir();
                let fresh = self.last_saved[i].filter(|(at, _)| at.elapsed() < Duration::from_secs(2));
                let saved = match fresh {
                    Some((_, t)) => t,
                    None => {
                        let t = saves::last_saved(&user, g.title_id);
                        self.last_saved[i] = Some((std::time::Instant::now(), t));
                        t
                    }
                };
                match saved {
                    Some(t) => ui.label(format!("Saved {} (UTC)", saves::stamp(t))),
                    None => ui.label("No save yet"),
                };
                ui.label(RichText::new(user.display().to_string()).weak().small());
                ui.horizontal_wrapped(|ui| {
                    if ui.button("Open folder").clicked() {
                        let _ = std::fs::create_dir_all(&user);
                        let _ = std::process::Command::new("explorer").arg(&user).spawn();
                    }
                    if ui.button("Export").clicked() {
                        if let Some(into) = rfd::FileDialog::new().set_title("Export to").pick_folder() {
                            self.last_saved[i] = None;
                            self.message = Some(match saves::export(&user, g.title_id, g.name, &into) {
                                Ok(p) => (true, format!("Exported to {}", p.display())),
                                Err(e) => (false, e),
                            });
                        }
                    }
                    if ui.add_enabled(!running, egui::Button::new("Import")).on_disabled_hover_text("Close the game first.").clicked() {
                        if let Some(from) = rfd::FileDialog::new().set_title("An exported save").pick_folder() {
                            self.last_saved[i] = None;
                            self.message = Some(match saves::import(&user, g.title_id, g.name, &from, &g.backup_dir()) {
                                Ok(Some(b)) => (true, format!("Imported. The previous saves are in {}", b.display())),
                                Ok(None) => (true, "Imported.".into()),
                                Err(e) => (false, e),
                            });
                        }
                    }
                });
            });
        }
    }

    fn files_tab(&mut self, ui: &mut egui::Ui) {
        card(ui, |ui| {
            ui.label(RichText::new("Your own game files").strong());
            ui.label("Drop your Xbox 360 disc image (.iso) of each game here, then its title update. A folder with the extracted files works too. Nothing is changed in what you drop.");
            ui.horizontal(|ui| {
                if ui.button("Choose a file").clicked() {
                    if let Some(p) = rfd::FileDialog::new().pick_file() {
                        self.open(&p);
                    }
                }
                if ui.button("Choose a folder").clicked() {
                    if let Some(p) = rfd::FileDialog::new().pick_folder() {
                        self.folder(&p);
                    }
                }
            });
        });
        if let Some(i) = &self.import {
            card(ui, |ui| {
                let done = i.done.load(Ordering::Relaxed);
                ui.label(format!("Copying {}", i.game.name));
                ui.add(egui::ProgressBar::new(done as f32 / i.total.max(1) as f32).show_percentage());
                if ui.button("Stop").clicked() {
                    i.stop.store(true, Ordering::Relaxed);
                }
            });
        }
        for g in &GAMES {
            card(ui, |ui| {
                ui.label(RichText::new(g.name).strong());
                let ready = g.ready(&self.settings);
                let disc = !matches!(ready, Ready::NoDisc | Ready::NoProgram);
                if ready == Ready::NoProgram {
                    ui.label("The port's program is missing from the programs folder.");
                } else {
                    ui.label(if disc { "Disc: found" } else { "Disc: missing" });
                    ui.label(if ready == Ready::Yes { "Title update: found" } else { "Title update: missing" });
                }
                ui.label(RichText::new(g.game_dir(&self.settings).display().to_string()).weak().small());
                if self.settings.folders.contains_key(g.id) && ui.button("Use the launcher's own folder").clicked() {
                    self.settings.folders.remove(g.id);
                }
            });
        }
    }
}

impl eframe::App for App {
    fn ui(&mut self, ui: &mut egui::Ui, _: &mut eframe::Frame) {
        let ctx = ui.ctx().clone();
        self.tick(&ctx);
        let dropped = ctx.input(|i| i.raw.dropped_files.first().map(|f| f.path().to_path_buf()));
        if let Some(p) = dropped {
            self.open(&p);
        }
        let time = ctx.input(|i| i.time);
        theme::background(ui.painter(), ui.max_rect(), time);
        ctx.request_repaint_after(Duration::from_millis(50));
        let titles = [(Tab::Play, "Campaign"), (Tab::Graphics, "Graphics"), (Tab::Controls, "Controls"), (Tab::Files, "Game files"), (Tab::Saves, "Saves")];
        let title = titles.iter().find(|t| t.0 == self.tab).map_or("", |t| t.1);
        egui::Panel::top("head").show_separator_line(false).frame(egui::Frame::new().inner_margin(egui::Margin { left: 18, right: 0, top: 10, bottom: 0 })).show(ui, |ui| {
            theme::header(ui, title);
        });
        egui::Panel::bottom("foot").show_separator_line(false).frame(egui::Frame::new()).show(ui, |ui| {
            if let Some((good, text)) = self.message.clone() {
                egui::Frame::new().inner_margin(egui::Margin::symmetric(24, 8)).show(ui, |ui| {
                    ui.horizontal(|ui| {
                        if ui.button("Close").clicked() {
                            self.message = None;
                        }
                        let color = if good { ACCENT } else { Color32::from_rgb(255, 150, 110) };
                        ui.add(egui::Label::new(RichText::new(text).color(color)).wrap());
                    });
                });
            }
            theme::footer(ui, &[("F9", "Switch renderer in game"), ("ESC", "Pause in game")]);
        });
        egui::Panel::left("menu").show_separator_line(false).resizable(false).exact_size(230.0).frame(egui::Frame::new().inner_margin(egui::Margin { left: 26, right: 10, top: 26, bottom: 0 })).show(ui, |ui| {
            for (tab, name) in titles {
                if theme::menu_item(ui, name, self.tab == tab, 194.0).clicked() {
                    self.tab = tab;
                }
                ui.add_space(4.0);
            }
            ui.add_space(18.0);
            ui.label(RichText::new("GRAWBox").size(13.0).color(theme::TEAL_DIM));
            ui.label(RichText::new(report::BUILD).size(11.0).color(theme::TEAL_DIM));
        });
        egui::CentralPanel::default().frame(egui::Frame::new().inner_margin(egui::Margin { left: 6, right: 26, top: 22, bottom: 10 })).show(ui, |ui| {
            egui::ScrollArea::vertical().auto_shrink(false).show(ui, |ui| {
                if self.tab != Tab::Controls {
                    self.capture = None;
                }
                match self.tab {
                    Tab::Play => self.play_tab(ui),
                    Tab::Graphics => self.graphics_tab(ui),
                    Tab::Controls => self.controls_tab(ui),
                    Tab::Files => self.files_tab(ui),
                    Tab::Saves => self.saves_tab(ui),
                }
            });
        });
    }
}

fn grid(ui: &mut egui::Ui, id: &str, add: impl FnOnce(&mut egui::Ui)) {
    egui::Grid::new(id).num_columns(2).spacing([28.0, 10.0]).min_col_width(170.0).show(ui, add);
}

fn row(ui: &mut egui::Ui, name: &str, hint: &str, add: impl FnOnce(&mut egui::Ui)) {
    let label = ui.label(name);
    if !hint.is_empty() {
        label.on_hover_text(hint);
    }
    ui.horizontal_wrapped(add);
    ui.end_row();
}

fn choice(ui: &mut egui::Ui, value: &mut String, options: &[(&str, &str)]) {
    for (v, name) in options {
        if ui.selectable_label(value == v, *name).clicked() {
            *value = v.to_string();
        }
    }
}

fn pick(ui: &mut egui::Ui, value: &mut u32, options: &[(u32, &str)]) {
    for (v, name) in options {
        ui.selectable_value(value, *v, *name);
    }
}

fn main() -> eframe::Result {
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_title("GRAWBox")
            .with_inner_size([1040.0, 680.0])
            .with_min_inner_size([640.0, 420.0])
            .with_drag_and_drop(true),
        ..Default::default()
    };
    let result = eframe::run_native("GRAWBox", options, Box::new(|cc| Ok(Box::new(App::new(cc)))));
    // Without a console the reason would be lost.
    if let Err(e) = &result {
        let _ = std::fs::write(game::home().join("launcher-error.txt"), format!("{e}
"));
    }
    result
}
