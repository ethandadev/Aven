# The way out. It appears when every gem has been collected.

def on_start():
    self.hide()

def on_update(dt):
    self.angle += 90 * dt

def on_message(name, data):
    if name == "all_gems":
        self.show()
        self.get_component("ParticleEmitter").emitting = True

def on_trigger(other):
    if other.tag == "player" and self.visible:
        play_sound("sounds/win.wav")
        broadcast("win")
        other.hide()
