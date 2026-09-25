# Spins the object around. Try changing the speed in the Inspector!

speed = 45  # degrees per second

def on_update(dt):
    self.rotation_y += speed * dt
