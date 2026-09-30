#!/usr/bin/env python3
"""Signs Rynax's release downloads, so the editor's updater installs only what you released.

    python3 tools/release/update_key.py new
        Makes a key pair. The public key goes in the repository's Actions *variable*
        UPDATE_PUBLIC_KEY (built into the editor); the private key goes in the Actions *secret*
        UPDATE_SIGNING_KEY. Keep a copy of the private key somewhere safe and offline.

    UPDATE_SIGNING_KEY=<private key> python3 tools/release/update_key.py sign FILE...
        Writes FILE.sig for each file (the release workflow does this).

    python3 tools/release/update_key.py verify PUBLIC_KEY FILE...
        Checks FILE.sig.

What's signed is the text "aven-update-v1\\n<file name>\\n<SHA-256 of the file>\\n". The file name
has the version in it (rynax-0.4.0-windows-x64.zip), so an older signed download can't be passed off
as a newer release. Signatures are Ed25519 (RFC 8032); this is the RFC's reference algorithm, using
only Python's standard library. editor's side: engine/src/rynax/core/ed25519.cpp.
"""

import hashlib
import os
import sys

# --- Ed25519, as in RFC 8032 section 6

p = 2**255 - 19
L = 2**252 + 27742317777372353535851937790883648493
d = -121665 * pow(121666, p - 2, p) % p
SQRT_M1 = pow(2, (p - 1) // 4, p)


def point_add(P, Q):
    A, B = (P[1] - P[0]) * (Q[1] - Q[0]) % p, (P[1] + P[0]) * (Q[1] + Q[0]) % p
    C, D = 2 * P[3] * Q[3] * d % p, 2 * P[2] * Q[2] % p
    E, F, G, H = B - A, D - C, D + C, B + A
    return (E * F % p, G * H % p, F * G % p, E * H % p)


def point_mul(s, P):
    Q = (0, 1, 1, 0)
    while s > 0:
        if s & 1:
            Q = point_add(Q, P)
        P = point_add(P, P)
        s >>= 1
    return Q


def point_equal(P, Q):
    return (P[0] * Q[2] - Q[0] * P[2]) % p == 0 and (P[1] * Q[2] - Q[1] * P[2]) % p == 0


def recover_x(y, sign):
    if y >= p:
        return None
    x2 = (y * y - 1) * pow(d * y * y + 1, p - 2, p)
    if x2 == 0:
        return None if sign else 0
    x = pow(x2, (p + 3) // 8, p)
    if (x * x - x2) % p != 0:
        x = x * SQRT_M1 % p
    if (x * x - x2) % p != 0:
        return None
    if (x & 1) != sign:
        x = p - x
    return x


g_y = 4 * pow(5, p - 2, p) % p
g_x = recover_x(g_y, 0)
G = (g_x, g_y, 1, g_x * g_y % p)


def compress(P):
    zinv = pow(P[2], p - 2, p)
    x, y = P[0] * zinv % p, P[1] * zinv % p
    return int.to_bytes(y | ((x & 1) << 255), 32, "little")


def decompress(s):
    if len(s) != 32:
        return None
    y = int.from_bytes(s, "little")
    sign = y >> 255
    y &= (1 << 255) - 1
    x = recover_x(y, sign)
    return None if x is None else (x, y, 1, x * y % p)


def sha512_int(data):
    return int.from_bytes(hashlib.sha512(data).digest(), "little")


def secret_expand(seed):
    h = hashlib.sha512(seed).digest()
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    return a, h[32:]


def public_key(seed):
    a, _ = secret_expand(seed)
    return compress(point_mul(a, G))


def sign(seed, msg):
    a, prefix = secret_expand(seed)
    A = compress(point_mul(a, G))
    r = sha512_int(prefix + msg) % L
    Rs = compress(point_mul(r, G))
    h = sha512_int(Rs + A + msg) % L
    s = (r + h * a) % L
    return Rs + int.to_bytes(s, 32, "little")


def verify(public, msg, signature):
    if len(public) != 32 or len(signature) != 64:
        return False
    A = decompress(public)
    R = decompress(signature[:32])
    if A is None or R is None:
        return False
    s = int.from_bytes(signature[32:], "little")
    if s >= L:
        return False
    h = sha512_int(signature[:32] + public + msg) % L
    return point_equal(point_mul(s, G), point_add(R, point_mul(h, A)))


# --- what Rynax signs


def message(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return ("aven-update-v1\n%s\n%s\n" % (os.path.basename(path), digest.hexdigest())).encode()


def main(argv):
    if len(argv) >= 2 and argv[1] == "new":
        seed = os.urandom(32)
        print("Public key  (Actions variable UPDATE_PUBLIC_KEY):  " + public_key(seed).hex())
        print("Private key (Actions secret UPDATE_SIGNING_KEY):   " + seed.hex())
        print("\nKeep a copy of the private key offline. Anyone with it can sign updates for Rynax.")
        return 0
    if len(argv) >= 3 and argv[1] == "sign":
        key = os.environ.get("UPDATE_SIGNING_KEY", "").strip()
        try:
            seed = bytes.fromhex(key)
        except ValueError:
            seed = b""
        if len(seed) != 32:
            print("Set UPDATE_SIGNING_KEY to the private key (64 hex digits).", file=sys.stderr)
            return 1
        for path in argv[2:]:
            with open(path + ".sig", "w") as f:
                f.write(sign(seed, message(path)).hex() + "\n")
            print("Signed " + os.path.basename(path) + " with key " + public_key(seed).hex())
        return 0
    if len(argv) >= 4 and argv[1] == "verify":
        try:
            public = bytes.fromhex(argv[2].strip())
        except ValueError:
            public = b""
        if len(public) != 32:
            print("The public key should be 64 hex digits.", file=sys.stderr)
            return 1
        ok = True
        for path in argv[3:]:
            try:
                with open(path + ".sig") as f:
                    signature = bytes.fromhex(f.read().strip())
                good = verify(public, message(path), signature)
            except (OSError, ValueError) as e:
                print("BAD  %s (%s)" % (path, e))
                ok = False
                continue
            print(("ok   " if good else "BAD  ") + path)
            ok = ok and good
        return 0 if ok else 1
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
