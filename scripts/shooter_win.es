# The message that appears when you win. Press R to play again.

target = 20  # the score that wins

def on_start():
    self.visible = False

def on_message(message, data):
    if message == "win":
        self.visible = True

def on_update(dt):
    if not self.visible and get_game("score", 0) >= target:
        self.visible = True
    if self.visible and key_pressed("r"):
        restart_scene()
