# Spins around the vertical axis.

speed = 90  # degrees per second

def on_update(dt):
    self.rotation_y += speed * dt
