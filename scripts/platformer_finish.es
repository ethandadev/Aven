# The finish: touching it wins the game.

def on_trigger(other):
    if other.tag == "player":
        broadcast("win")
