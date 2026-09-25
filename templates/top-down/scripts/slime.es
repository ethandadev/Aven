# A slime that wanders around and chases the hero when it gets close.

speed = 2.5  # chase speed
sight = 4  # how close the hero must be before the slime notices

_wander = vec(0, 0)
_stunned = 0

def on_start():
    pick_direction()
    every(2, pick_direction)

def pick_direction():
    _wander = vec(random_range(-1, 1), random_range(-1, 1))

def on_update(dt):
    if _stunned > 0:
        _stunned -= dt
        return
    player = find("Player")
    if player and self.distance_to(player) < sight:
        self.velocity = self.direction_to(player) * speed
    else:
        self.velocity = _wander
    self.flip_x = self.velocity_x < 0

def on_collide(other):
    if other.tag == "player":
        other.send("hurt", 1)
        # Bounce back so the hero can get away.
        self.velocity = self.direction_to(other) * -6
        _stunned = 1
