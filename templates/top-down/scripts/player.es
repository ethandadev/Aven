# The hero. Walks with the arrow keys or WASD.

speed = 5  # walking speed
max_health = 3  # hearts at the start

_hurt_time = 0

def on_start():
    game.health = max_health
    game.gems = 0
    game.total_gems = count("gem")

def on_update(dt):
    if game.health <= 0:
        self.velocity = vec(0, 0)
        return
    x = axis("horizontal")
    y = axis("vertical")
    self.velocity = vec(x, y) * speed
    if x < 0:
        self.flip_x = True
    elif x > 0:
        self.flip_x = False

    # Blink for a moment after getting hurt.
    if _hurt_time > 0:
        _hurt_time -= dt
        if int(_hurt_time * 10) % 2 == 0:
            self.alpha = 0.3
        else:
            self.alpha = 1
    else:
        self.alpha = 1

# Slimes call this with other.send("hurt", 1).
def hurt(amount):
    if _hurt_time > 0 or game.health <= 0:
        return
    _hurt_time = 1.2
    game.health -= amount
    play_sound("sounds/hit.wav")
    camera_shake(0.3, 0.3)
    if game.health <= 0:
        play_sound("sounds/lose.wav")
        broadcast("game_over")
