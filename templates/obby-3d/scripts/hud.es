def on_update(dt):
    if not game.finished:
        seconds = time() - game.start_time
        self.text = f"Time: {seconds:.1f}    Checkpoint: {game.checkpoint} / 2"
