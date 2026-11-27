//! The keys held now, by the names of input/keys.h. Asked of Windows, since the window library does
//! not tell the left and right modifier keys apart.

// F8, F9 and F10 are left out: the games use them (renderer switch, haze variants, playtest report).
const NAMED: &[(&str, i32)] = &[
    ("MOUSE1", 0x01), ("MOUSE2", 0x02), ("MOUSE3", 0x04), ("MOUSE4", 0x05), ("MOUSE5", 0x06),
    ("UP", 0x26), ("DOWN", 0x28), ("LEFT", 0x25), ("RIGHT", 0x27), ("SPACE", 0x20),
    ("ENTER", 0x0D), ("ESC", 0x1B), ("TAB", 0x09), ("BACKSPACE", 0x08), ("LSHIFT", 0xA0),
    ("RSHIFT", 0xA1), ("LCTRL", 0xA2), ("RCTRL", 0xA3), ("LALT", 0xA4), ("RALT", 0xA5),
    ("CAPS", 0x14), ("INSERT", 0x2D), ("DELETE", 0x2E), ("HOME", 0x24), ("END", 0x23),
    ("PAGEUP", 0x21), ("PAGEDOWN", 0x22), ("F1", 0x70), ("F2", 0x71), ("F3", 0x72),
    ("F4", 0x73), ("F5", 0x74), ("F6", 0x75), ("F7", 0x76), ("F11", 0x7A),
    ("F12", 0x7B), ("0", 0x30), ("1", 0x31), ("2", 0x32), ("3", 0x33), ("4", 0x34), ("5", 0x35),
    ("6", 0x36), ("7", 0x37), ("8", 0x38), ("9", 0x39), ("A", 0x41), ("B", 0x42), ("C", 0x43),
    ("D", 0x44), ("E", 0x45), ("F", 0x46), ("G", 0x47), ("H", 0x48), ("I", 0x49), ("J", 0x4A),
    ("K", 0x4B), ("L", 0x4C), ("M", 0x4D), ("N", 0x4E), ("O", 0x4F), ("P", 0x50), ("Q", 0x51),
    ("R", 0x52), ("S", 0x53), ("T", 0x54), ("U", 0x55), ("V", 0x56), ("W", 0x57), ("X", 0x58),
    ("Y", 0x59), ("Z", 0x5A),
];

#[cfg(windows)]
pub fn down() -> Vec<&'static str> {
    #[link(name = "user32")]
    extern "system" {
        fn GetAsyncKeyState(key: i32) -> i16;
    }
    // SAFETY: takes a number and returns one.
    NAMED.iter().filter(|(_, k)| unsafe { GetAsyncKeyState(*k) } < 0).map(|(n, _)| *n).collect()
}

#[cfg(not(windows))]
pub fn down() -> Vec<&'static str> {
    let _ = NAMED;
    Vec::new()
}
