# Shows the controls for a few seconds, then fades away.

def on_start():
    wait(6)
    self.tween("alpha", 0, 1.5)
