# Big text in the middle of the screen for important moments.

_game_over = False
_hide_at = 3  # time() when the text disappears; 0 keeps it on screen

def on_message(name, data):
    if name == "all_gems":
        show_text("All gems found! Find the portal.", 3)
    elif name == "win":
        show_text("You escaped the meadow!", 0)
    elif name == "game_over":
        show_text("Oh no! Press R to try again", 0)
        _game_over = True

def show_text(text, seconds):
    self.text = text
    self.show()
    if seconds > 0:
        _hide_at = time() + seconds
    else:
        _hide_at = 0

def on_update(dt):
    if _hide_at > 0 and time() > _hide_at:
        self.hide()
        _hide_at = 0
    if _game_over and key_pressed("r"):
        restart_scene()
