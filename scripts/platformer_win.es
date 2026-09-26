# The message that appears when you win. Press R to play again.

def on_start():
    self.visible = False

def on_message(message, data):
    if message == "win":
        self.visible = True

def on_update(dt):
    if self.visible and key_pressed("r"):
        restart_scene()
