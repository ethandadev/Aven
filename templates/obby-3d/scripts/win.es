def on_start():
    self.hide()

def on_message(name, data):
    if name == "win":
        self.text = f"You win! {game.time:.1f} seconds"
        self.show()
