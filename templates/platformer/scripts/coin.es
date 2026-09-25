# A coin. When the player touches it, add 1 to the score and disappear.

def on_trigger(other):
    if other.tag == "player":
        game.coins += 1
        play_sound("sounds/coin.wav")
        other.emit(12)  # sparkles from the player's particle emitter
        self.destroy()
