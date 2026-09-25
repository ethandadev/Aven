# The gate lights up once every crystal has been found.

_open = False

def on_message(name, data):
    if name == "gate_open":
        _open = True
        self.get_component("MeshRenderer").emission_strength = 4
        find("Portal Light").get_component("Light").intensity = 6
        broadcast("message", "The gate is open! Find it at the end of the path.")

def on_trigger(other):
    if other.tag == "player" and _open:
        play_sound("sounds/win.wav")
        broadcast("message", "You found the way home. Thanks for playing!")
        _open = False
