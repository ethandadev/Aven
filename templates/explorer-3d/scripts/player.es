# First-person controls come from the CharacterController (first_person is on).
# This script handles the mouse, the flashlight and falling out of the world.

flashlight_power = 6  # brightness when the flashlight is on

def on_start():
    lock_mouse(True)
    game.crystals = 0
    game.total_crystals = count("crystal")

def on_update(dt):
    if key_pressed("escape"):
        lock_mouse(False)
    if mouse_pressed("left"):
        lock_mouse(True)
    if key_pressed("f"):
        light = find("Flashlight").get_component("Light")
        if light.intensity > 0:
            light.intensity = 0
        else:
            light.intensity = flashlight_power
        play_sound("sounds/click.wav", 0.5)
    if self.y < -10:
        self.position = vec(0, 1, 8)
