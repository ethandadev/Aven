# A slime that walks back and forth. Jump on top of it to squash it!

speed = 1.5  # how fast it walks
distance = 2  # how far it walks from where it started

_start_x = 0
_direction = 1

def on_start():
    _start_x = self.x

def on_update(dt):
    self.x += speed * _direction * dt
    if self.x > _start_x + distance:
        _direction = -1
    elif self.x < _start_x - distance:
        _direction = 1

def on_trigger(other):
    if other.tag != "player":
        return
    # Landing on top squashes the slime; touching it from the side hurts.
    if other.velocity_y < 0 and other.y > self.y + 0.3:
        other.velocity_y = 11
        play_sound("sounds/stomp.wav")
        game.coins += 2
        self.destroy()
    else:
        broadcast("player_hit")
