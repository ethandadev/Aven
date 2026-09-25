# Hidden until someone broadcasts "win".

def on_start():
    self.hide()

def on_message(name, data):
    if name == "win":
        self.show()
        self.text = f"You win! {game.coins} coins"
