# The end of the course.

def on_trigger(other):
    if other.tag == "player" and not game.finished:
        game.finished = True
        game.time = time() - game.start_time
        play_sound("sounds/win.wav")
        self.emit(120)
        broadcast("win")
