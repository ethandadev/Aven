# Walking and jumping come from the CharacterController component, so this
# script only handles falling, lava and checkpoints.

fall_limit = -12  # respawn when falling below this height

_checkpoint = vec(0, 1, 0)

def on_start():
    _checkpoint = self.position
    game.checkpoint = 0
    game.finished = False
    game.start_time = time()

def on_update(dt):
    if self.y < fall_limit:
        respawn()
    if key_pressed("space"):
        play_sound("sounds/jump.wav", 0.4)

def respawn():
    self.position = _checkpoint
    self.velocity = vec(0, 0, 0)
    play_sound("sounds/hit.wav")
    camera_shake(0.3, 0.3)

def on_trigger(other):
    if other.tag == "lava":
        respawn()

# Checkpoints call this with other.send("set_checkpoint", position).
def set_checkpoint(position):
    _checkpoint = position
