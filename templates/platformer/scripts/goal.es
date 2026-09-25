# The flag at the end of the level.

def on_trigger(other):
    if other.tag == "player" and not game.won:
        game.won = True
        play_sound("sounds/win.wav")
        self.emit(60)
        broadcast("win")
