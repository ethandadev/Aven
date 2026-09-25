# Touch it to respawn here instead of at the start.

number = 1  # which checkpoint this is

_reached = False

def on_trigger(other):
    if other.tag == "player" and not _reached:
        _reached = True
        other.send("set_checkpoint", self.position + vec(0, 1.2, 0))
        game.checkpoint = number
        play_sound("sounds/checkpoint.wav")
        self.color = "#22c55e"
