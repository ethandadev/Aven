# Shows the number of coins. `game` holds values every script can share.

def on_start():
    game.coins = 0
    game.won = False

def on_update(dt):
    self.text = f"Coins: {game.coins}"
