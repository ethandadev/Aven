# The message that appears when you win. Press R to play again.

def on_start():
    self.visible = False

def on_message(message, data):
    if message == "win":
        self.visible = True

def on_update(dt):
    if not self.visible and count("star") == 0:
        self.visible = True
    if self.visible and key_pressed("r"):
        restart_scene()
