//! The look: the games' menus (teal holograms over a dark map, an orange-red bar on what is
//! selected), drawn here from shapes. None of the games' art is used.

use eframe::egui::{
    self, epaint::Mesh, Align2, Color32, FontId, Pos2, Rect, Response, Sense, Shape, Stroke,
    StrokeKind, Ui, Vec2,
};

pub const TEAL: Color32 = Color32::from_rgb(78, 214, 200);
pub const TEAL_DIM: Color32 = Color32::from_rgb(40, 120, 115);
pub const SELECT: Color32 = Color32::from_rgb(222, 92, 58);
pub const TEXT: Color32 = Color32::from_rgb(214, 240, 236);
pub const GROUND: Color32 = Color32::from_rgb(8, 22, 24);

fn alpha(c: Color32, a: u8) -> Color32 {
    Color32::from_rgba_unmultiplied(c.r(), c.g(), c.b(), a)
}

/// A rectangle whose colour fades from `left` to `right`.
fn gradient(painter: &egui::Painter, rect: Rect, left: Color32, right: Color32) {
    let mut mesh = Mesh::default();
    mesh.colored_vertex(rect.left_top(), left);
    mesh.colored_vertex(rect.right_top(), right);
    mesh.colored_vertex(rect.right_bottom(), right);
    mesh.colored_vertex(rect.left_bottom(), left);
    mesh.add_triangle(0, 1, 2);
    mesh.add_triangle(0, 2, 3);
    painter.add(Shape::mesh(mesh));
}

/// The background: a dark sea-green ground with a faint map grid, contour lines and the
/// targeting ring in the corner.
pub fn background(painter: &egui::Painter, rect: Rect, time: f64) {
    painter.rect_filled(rect, 0.0, GROUND);
    let mut mesh = Mesh::default();
    let top = Color32::from_rgb(16, 46, 48);
    let bottom = Color32::from_rgb(6, 16, 18);
    mesh.colored_vertex(rect.left_top(), top);
    mesh.colored_vertex(rect.right_top(), Color32::from_rgb(10, 30, 32));
    mesh.colored_vertex(rect.right_bottom(), bottom);
    mesh.colored_vertex(rect.left_bottom(), Color32::from_rgb(9, 26, 28));
    mesh.add_triangle(0, 1, 2);
    mesh.add_triangle(0, 2, 3);
    painter.add(Shape::mesh(mesh));
    let grid = Stroke::new(1.0, alpha(TEAL, 10));
    let mut x = rect.left();
    while x < rect.right() {
        painter.line_segment([Pos2::new(x, rect.top()), Pos2::new(x, rect.bottom())], grid);
        x += 48.0;
    }
    let mut y = rect.top();
    while y < rect.bottom() {
        painter.line_segment([Pos2::new(rect.left(), y), Pos2::new(rect.right(), y)], grid);
        y += 48.0;
    }
    // Contours, like the map under the games' menus.
    for k in 0..7 {
        let base = rect.top() + rect.height() * (0.2 + 0.12 * k as f32);
        let points: Vec<Pos2> = (0..=60)
            .map(|i| {
                let t = i as f32 / 60.0;
                let x = rect.left() + rect.width() * t;
                let w = (t * 9.0 + k as f32 * 1.7).sin() * 14.0 + (t * 23.0 + k as f32).sin() * 5.0;
                Pos2::new(x, base + w)
            })
            .collect();
        painter.add(Shape::line(points, Stroke::new(1.0, alpha(TEAL, 14))));
    }
    // A slow scan line.
    let scan = rect.top() + ((time * 0.08).fract() as f32) * rect.height();
    gradient(
        painter,
        Rect::from_min_max(Pos2::new(rect.left(), scan), Pos2::new(rect.right(), scan + 2.0)),
        alpha(TEAL, 0),
        alpha(TEAL, 22),
    );
}

/// The ring and crosshair before the title, as in the games' headers.
pub fn emblem(painter: &egui::Painter, centre: Pos2, r: f32) {
    let s = Stroke::new(2.0, TEAL);
    painter.circle_stroke(centre, r, s);
    painter.circle_stroke(centre, r * 0.45, Stroke::new(1.5, TEAL));
    let long = r * 1.9;
    painter.line_segment([centre - Vec2::new(long, 0.0), centre + Vec2::new(long, 0.0)], Stroke::new(1.0, alpha(TEAL, 150)));
    painter.line_segment([centre - Vec2::new(0.0, long), centre + Vec2::new(0.0, long)], Stroke::new(1.0, alpha(TEAL, 150)));
}

/// The header band with the page's name.
pub fn header(ui: &mut Ui, title: &str) {
    let (rect, _) = ui.allocate_exact_size(Vec2::new(ui.available_width(), 64.0), Sense::hover());
    let p = ui.painter();
    let band = Rect::from_min_max(Pos2::new(rect.left() + 34.0, rect.top() + 14.0), Pos2::new(rect.right(), rect.bottom() - 10.0));
    gradient(p, band, alpha(TEAL, 70), alpha(TEAL, 18));
    p.line_segment([band.left_top(), band.right_top()], Stroke::new(1.0, alpha(TEAL, 160)));
    p.line_segment([band.left_bottom(), band.right_bottom()], Stroke::new(1.0, alpha(TEAL, 90)));
    emblem(p, Pos2::new(rect.left() + 30.0, rect.center().y + 2.0), 22.0);
    p.text(
        Pos2::new(rect.left() + 64.0, band.center().y),
        Align2::LEFT_CENTER,
        title,
        FontId::proportional(30.0),
        TEXT,
    );
}

/// An item of the menu list: teal glass, or the orange-red bar with a white edge when selected.
pub fn menu_item(ui: &mut Ui, text: &str, selected: bool, width: f32) -> Response {
    let (rect, response) = ui.allocate_exact_size(Vec2::new(width, 40.0), Sense::click());
    let p = ui.painter();
    let hover = response.hovered();
    if selected {
        let bar = rect.shrink2(Vec2::new(0.0, 2.0)).translate(Vec2::new(14.0, 0.0));
        gradient(p, bar, alpha(SELECT, 235), alpha(SELECT, 0));
        p.rect_filled(Rect::from_min_size(bar.min, Vec2::new(7.0, bar.height())), 0.0, Color32::WHITE);
        p.text(bar.left_center() + Vec2::new(18.0, 0.0), Align2::LEFT_CENTER, text, FontId::proportional(19.0), Color32::WHITE);
    } else {
        let bar = rect.shrink2(Vec2::new(0.0, 3.0));
        gradient(p, bar, alpha(TEAL, if hover { 70 } else { 38 }), alpha(TEAL, 0));
        p.rect_filled(Rect::from_min_size(bar.min, Vec2::new(3.0, bar.height())), 0.0, alpha(TEAL, 200));
        p.line_segment([bar.left_bottom(), bar.right_bottom() - Vec2::new(bar.width() * 0.35, 0.0)], Stroke::new(1.0, alpha(TEAL, 90)));
        p.text(bar.left_center() + Vec2::new(14.0, 0.0), Align2::LEFT_CENTER, text, FontId::proportional(18.0), alpha(TEXT, 225));
    }
    response.on_hover_cursor(egui::CursorIcon::PointingHand)
}

/// A panel with cut corners and a thin teal edge.
pub fn panel(ui: &mut Ui, add: impl FnOnce(&mut Ui)) {
    let where_ = ui.painter().add(Shape::Noop);
    let inner = egui::Frame::new().inner_margin(egui::Margin::symmetric(20, 16)).show(ui, |ui| {
        ui.set_width(ui.available_width());
        add(ui);
    });
    let r = inner.response.rect;
    let c = 12.0;
    let points = vec![
        r.left_top() + Vec2::new(c, 0.0),
        r.right_top(),
        r.right_bottom() - Vec2::new(0.0, c),
        r.right_bottom() - Vec2::new(c, 0.0),
        r.left_bottom(),
        r.left_top() + Vec2::new(0.0, c),
    ];
    ui.painter().set(
        where_,
        Shape::convex_polygon(points, Color32::from_rgba_unmultiplied(6, 28, 30, 205), Stroke::new(1.0, alpha(TEAL, 120))),
    );
    // Brackets on two corners.
    let s = Stroke::new(2.0, TEAL);
    ui.painter().line_segment([r.right_top(), r.right_top() + Vec2::new(-22.0, 0.0)], s);
    ui.painter().line_segment([r.right_top(), r.right_top() + Vec2::new(0.0, 22.0)], s);
    ui.painter().line_segment([r.left_bottom(), r.left_bottom() + Vec2::new(22.0, 0.0)], s);
    ui.painter().line_segment([r.left_bottom(), r.left_bottom() + Vec2::new(0.0, -22.0)], s);
    ui.add_space(12.0);
}

/// The big action, like the games' selected item but standing on its own.
pub fn action(ui: &mut Ui, text: &str, enabled: bool) -> Response {
    let (rect, response) = ui.allocate_exact_size(Vec2::new(170.0, 48.0), if enabled { Sense::click() } else { Sense::hover() });
    let p = ui.painter();
    let hot = enabled && response.hovered();
    let fill = if !enabled { alpha(TEAL_DIM, 90) } else if hot { Color32::from_rgb(240, 110, 70) } else { SELECT };
    let c = 10.0;
    let points = vec![
        rect.left_top() + Vec2::new(c, 0.0),
        rect.right_top(),
        rect.right_bottom() - Vec2::new(c, 0.0),
        rect.left_bottom(),
    ];
    p.add(Shape::convex_polygon(points, fill, Stroke::new(1.0, alpha(Color32::WHITE, if enabled { 160 } else { 40 }))));
    p.text(rect.center(), Align2::CENTER_CENTER, text, FontId::proportional(21.0), if enabled { Color32::WHITE } else { alpha(TEXT, 120) });
    if enabled { response.on_hover_cursor(egui::CursorIcon::PointingHand) } else { response }
}

/// The bottom band with what the keys do.
pub fn footer(ui: &mut Ui, hints: &[(&str, &str)]) {
    let (rect, _) = ui.allocate_exact_size(Vec2::new(ui.available_width(), 40.0), Sense::hover());
    let p = ui.painter();
    gradient(p, rect, alpha(TEAL, 50), alpha(TEAL, 20));
    p.line_segment([rect.left_top(), rect.right_top()], Stroke::new(1.0, alpha(TEAL, 140)));
    let mut x = rect.left() + 24.0;
    for (key, what) in hints {
        let cap = Rect::from_center_size(Pos2::new(x + 13.0, rect.center().y), Vec2::new(26.0, 22.0));
        p.rect_stroke(cap, 5.0, Stroke::new(1.5, TEXT), StrokeKind::Inside);
        p.text(cap.center(), Align2::CENTER_CENTER, *key, FontId::proportional(12.0), TEXT);
        let g = p.text(Pos2::new(cap.right() + 8.0, rect.center().y), Align2::LEFT_CENTER, *what, FontId::proportional(16.0), TEXT);
        x = g.right() + 34.0;
    }
}

pub fn style(ctx: &egui::Context) {
    ctx.set_theme(egui::Theme::Dark);
    ctx.all_styles_mut(|s| {
        let v = &mut s.visuals;
        v.override_text_color = Some(TEXT);
        v.panel_fill = Color32::TRANSPARENT;
        v.window_fill = Color32::from_rgb(10, 32, 34);
        v.selection.bg_fill = alpha(SELECT, 200);
        v.selection.stroke = Stroke::new(1.0, Color32::WHITE);
        v.hyperlink_color = TEAL;
        for w in [&mut v.widgets.inactive, &mut v.widgets.hovered, &mut v.widgets.active, &mut v.widgets.open] {
            w.corner_radius = egui::CornerRadius::same(2);
        }
        v.widgets.inactive.weak_bg_fill = alpha(TEAL, 30);
        v.widgets.inactive.bg_fill = alpha(TEAL, 45);
        v.widgets.inactive.bg_stroke = Stroke::new(1.0, alpha(TEAL, 90));
        v.widgets.hovered.weak_bg_fill = alpha(TEAL, 70);
        v.widgets.hovered.bg_fill = alpha(TEAL, 80);
        v.widgets.hovered.bg_stroke = Stroke::new(1.0, TEAL);
        v.widgets.active.weak_bg_fill = alpha(SELECT, 200);
        v.widgets.active.bg_fill = alpha(SELECT, 200);
        v.widgets.noninteractive.bg_stroke = Stroke::new(1.0, alpha(TEAL, 60));
        v.slider_trailing_fill = true;
        s.spacing.button_padding = Vec2::new(12.0, 6.0);
        s.spacing.item_spacing = Vec2::new(10.0, 8.0);
        for (_, font) in s.text_styles.iter_mut() {
            font.size *= 1.1;
        }
    });
}
