# Runs when this enemy is destroyed: one more point.

def on_destroy():
    game.score = get_game("score", 0) + 1
