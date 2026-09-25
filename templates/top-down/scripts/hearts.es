# Shows one heart for each point of health.

def on_update(dt):
    for i in range(1, 4):
        heart = self.find_child(f"Heart{i}")
        heart.visible = game.health >= i
