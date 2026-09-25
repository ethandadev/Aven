# A gem that bobs up and down. Collect them all to open the portal!

_start_y = 0

def on_start():
    _start_y = self.y

def on_update(dt):
    self.y = _start_y + sin(time() * 180) * 0.1

def on_trigger(other):
    if other.tag == "player":
        game.gems += 1
        play_sound("sounds/collect.wav")
        if game.gems == game.total_gems:
            broadcast("all_gems")
        self.destroy()
