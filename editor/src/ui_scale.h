#pragma once

namespace rynax::editor::ui {

// A size laid out for 16 px text, scaled to the real text size (display scaling, UI size and the
// Text size preference): so buttons and fields fit their words on every screen.
float px(float size);
void setPixelScale(float scale);

} // namespace rynax::editor::ui
