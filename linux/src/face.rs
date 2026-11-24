use std::sync::LazyLock;

use ksni::Icon;
use tiny_skia::{LineCap, LineJoin, Paint, PathBuilder, Pixmap, Rect, Stroke, Transform};

#[derive(Clone, Copy)]
pub enum Mood {
    Sad,
    Neutral,
    Happy,
}

impl Mood {
    pub fn icons(self) -> Vec<Icon> {
        static SAD: LazyLock<Vec<Icon>> = LazyLock::new(|| draw_icons(Mood::Sad));
        static NEUTRAL: LazyLock<Vec<Icon>> = LazyLock::new(|| draw_icons(Mood::Neutral));
        static HAPPY: LazyLock<Vec<Icon>> = LazyLock::new(|| draw_icons(Mood::Happy));

        match self {
            Self::Sad => &*SAD,
            Self::Neutral => &*NEUTRAL,
            Self::Happy => &*HAPPY,
        }
        .clone()
    }
}

fn draw_icons(mood: Mood) -> Vec<Icon> {
    [16, 22, 32, 48, 64]
        .map(|size| draw_face(size, mood))
        .into()
}

fn draw_face(size: u32, mood: Mood) -> Icon {
    let mut pixmap = Pixmap::new(size, size).expect("valid icon size");
    let scale = size as f32 / 16.0;
    let transform = Transform::from_scale(scale, scale);
    let mut yellow = Paint::default();
    yellow.set_color_rgba8(255, 220, 0, 255);
    let black = Paint::default();
    let stroke = Stroke {
        line_cap: LineCap::Round,
        line_join: LineJoin::Round,
        ..Default::default()
    };

    // Match the Windows face in its original 16 by 16 coordinate space.
    let face = PathBuilder::from_circle(8.0, 8.0, 6.5).expect("valid face circle");
    pixmap.fill_path(&face, &yellow, Default::default(), transform, None);
    pixmap.stroke_path(&face, &black, &stroke, transform, None);

    for x in [4.0, 10.0] {
        let bounds = Rect::from_xywh(x, 5.0, 2.0, 2.0).expect("valid eye bounds");
        let eye = PathBuilder::from_oval(bounds).expect("valid eye oval");
        pixmap.fill_path(&eye, &black, Default::default(), transform, None);
    }

    let heights = match mood {
        Mood::Sad => [12.0, 10.0, 9.0, 10.0, 12.0],
        Mood::Neutral => [11.0; 5],
        Mood::Happy => [9.0, 10.0, 11.0, 10.0, 9.0],
    };
    let mut mouth = PathBuilder::new();
    mouth.move_to(4.0, heights[0]);
    for (x, y) in [6.0, 8.0, 10.0, 12.0].into_iter().zip(&heights[1..]) {
        mouth.line_to(x, *y);
    }
    let mouth = mouth.finish().expect("nonempty mouth path");
    pixmap.stroke_path(&mouth, &black, &stroke, transform, None);

    let mut data = Vec::with_capacity((size * size * 4) as usize);
    for pixel in pixmap.pixels() {
        let rgba = pixel.demultiply();
        data.extend([rgba.alpha(), rgba.red(), rgba.green(), rgba.blue()]);
    }
    Icon {
        width: size as i32,
        height: size as i32,
        data,
    }
}
