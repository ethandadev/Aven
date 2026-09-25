# A glowing crystal that floats and spins. Walk into it to collect it.

_start_y = 0

def on_start():
    _start_y = self.y

def on_update(dt):
    self.y = _start_y + sin(time() * 90) * 0.15
    self.rotation_y += 60 * dt

def on_trigger(other):
    if other.tag == "player":
        game.crystals += 1
        play_sound("sounds/collect.wav")
        if game.crystals == game.total_crystals:
            broadcast("gate_open")
        else:
            broadcast("message", f"{game.total_crystals - game.crystals} crystals left")
        self.destroy()
