# Shows messages sent with broadcast("message", "some text").

_hide_at = 0

def on_message(name, data):
    if name == "message":
        self.text = data
        self.alpha = 1
        _hide_at = time() + 4

def on_update(dt):
    if _hide_at > 0 and time() > _hide_at:
        self.tween("alpha", 0, 1)
        _hide_at = 0
