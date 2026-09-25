# Slides back and forth. Players standing on it ride along.

distance = 2.5  # how far it moves to each side
speed = 1  # higher is faster

_start_x = 0

def on_start():
    _start_x = self.x

def on_update(dt):
    self.x = _start_x + sin(time() * 60 * speed) * distance
