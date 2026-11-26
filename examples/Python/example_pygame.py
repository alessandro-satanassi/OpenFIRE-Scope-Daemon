#!/usr/bin/env python3
"""example_pygame.py - a tiny pygame shooting gallery with an OpenFIRE scope for player 1.

The mouse is the lightgun (an OpenFIRE lightgun moves the mouse too). The scope shows a 4x zoom around the aim point:
the "crop around the aim point" technique, the simplest way to give a 2D game a scope. Click to shoot, mouse wheel
to change the zoom, P or Esc for the pause menu (the scope keeps the last image), Q to quit.

    pip install pygame
    python example_pygame.py
    python example_pygame.py --always     # also without a scope: watch the daemon's Preview tab
    python example_pygame.py --demo 20    # no mouse needed: the aim moves by itself, quits after 20 s

MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE
"""
import argparse
import math
import random

import pygame

import openfire_scope as ofs

SCREEN_W, SCREEN_H = 960, 540
PLAYER = 1   # the scope of player 1 (the mouse); with several lightguns, one aim point and one view per player


def to_bytes(surface):
    """The pixels of a surface as RGB bytes, top row first (pygame 2.1.3+ has tobytes, older versions tostring)."""
    return pygame.image.tobytes(surface, "RGB") if hasattr(pygame.image, "tobytes") \
        else pygame.image.tostring(surface, "RGB")


def draw_world(screen, targets, t):
    screen.fill((110, 170, 235))
    pygame.draw.polygon(screen, (60, 140, 60), [(0, SCREEN_H)] + [
        (x, 330 + 35 * math.sin(x * 0.01 + 1) + 12 * math.sin(x * 0.045)) for x in range(0, SCREEN_W + 20, 20)
    ] + [(SCREEN_W, SCREEN_H)])
    for tg in targets:
        x, y = tg["x"] + 30 * math.sin(t * tg["speed"]), tg["y"]
        for r, c in ((18, (240, 240, 240)), (14, (220, 40, 40)), (9, (240, 240, 240)), (4, (220, 40, 40))):
            pygame.draw.circle(screen, c, (int(x), int(y)), r)
        tg["pos"] = (x, y)


def scope_view(screen, aim, zoom, view):
    """A (240/zoom)-pixel square around the aim point, scaled up to 240x240, plus the reticle."""
    size = max(8, int(round(ofs.WIDTH / zoom)))
    src = pygame.Rect(0, 0, size, size)
    src.center = (int(aim[0]), int(aim[1]))
    view.fill((0, 0, 0))
    part = src.clip(screen.get_rect())   # near the edges of the screen the rest stays black
    if part.width and part.height:
        scaled = pygame.transform.smoothscale(screen.subsurface(part),
                                              (part.width * ofs.WIDTH // size, part.height * ofs.HEIGHT // size))
        view.blit(scaled, ((part.x - src.x) * ofs.WIDTH // size, (part.y - src.y) * ofs.HEIGHT // size))
    # the scope draws nothing over the image: the reticle is ours
    for a, b in (((120, 30), (120, 110)), ((120, 130), (120, 210)), ((30, 120), (110, 120)), ((130, 120), (210, 120))):
        pygame.draw.line(view, (0, 0, 0), a, b, 2)
    pygame.draw.circle(view, (255, 40, 40), (120, 120), 2)
    pygame.draw.circle(view, (0, 0, 0), (120, 120), 112, 2)   # a ring just inside the visible circle


def main():
    ap = argparse.ArgumentParser(description="pygame shooting gallery with an OpenFIRE scope.")
    ap.add_argument("--always", action="store_true", help="send even without a scope (daemon Preview tab)")
    ap.add_argument("--demo", type=float, metavar="SECONDS", help="aim by itself and quit after SECONDS")
    args = ap.parse_args()

    pygame.init()
    screen = pygame.display.set_mode((SCREEN_W, SCREEN_H))
    pygame.display.set_caption("OpenFIRE scope - pygame example")
    world = pygame.Surface((SCREEN_W, SCREEN_H))
    view = pygame.Surface((ofs.WIDTH, ofs.HEIGHT))
    font = pygame.font.Font(None, 36)
    clock = pygame.time.Clock()
    targets = [{"x": random.randint(80, 880), "y": random.randint(150, 450), "speed": random.uniform(0.5, 2)}
               for _ in range(8)]
    zoom, paused, score, t = 4.0, False, 0, 0.0

    scope = ofs.Scope(always_send=args.always)
    running = True
    while running:
        dt = clock.tick(60) / 1000   # the game runs at 60 fps; the scope gets one image per game frame
        for e in pygame.event.get():
            if e.type == pygame.QUIT or (e.type == pygame.KEYDOWN and e.key == pygame.K_q):
                running = False
            elif e.type == pygame.KEYDOWN and e.key in (pygame.K_p, pygame.K_ESCAPE):
                paused = not paused
            elif e.type == pygame.MOUSEWHEEL:
                zoom = min(10.0, max(2.0, zoom + e.y * 0.5))
            elif e.type == pygame.MOUSEBUTTONDOWN and e.button == 1 and not paused:
                for tg in targets:
                    if math.dist(tg["pos"], e.pos) < 18:   # hit: the target jumps somewhere else
                        score += 1
                        tg["x"], tg["y"] = random.randint(80, 880), random.randint(150, 450)

        if not paused:
            t += dt
        if args.demo is not None:
            aim = (480 + 380 * math.sin(t * 0.6), 300 + 140 * math.sin(t * 1.1))
            if t >= args.demo:
                running = False
        else:
            aim = pygame.mouse.get_pos()

        draw_world(world, targets, t)
        screen.blit(world, (0, 0))

        scope.update()   # discovery + pause packets, never blocks
        if paused:
            scope.set_paused(PLAYER)   # pause menu: the scope keeps the last image
        elif scope.has_scope(PLAYER):
            scope_view(world, aim, zoom, view)   # from the world, without the HUD of the screen
            scope.send_frame(PLAYER, to_bytes(view), ofs.RGB888)

        # HUD: a small copy of the scope view in the corner, score, state
        if scope.has_scope(PLAYER):
            screen.blit(pygame.transform.smoothscale(view, (160, 160)), (SCREEN_W - 170, 10))
        state = "scope P%d: %s" % (PLAYER, "port %d" % scope.ports[PLAYER] if scope.ports[PLAYER] else
                                   ("always_send" if args.always else "none"))
        screen.blit(font.render("score %d   zoom %.1fx   %s" % (score, zoom, state), True, (0, 0, 0)), (10, 10))
        if paused:
            screen.blit(font.render("PAUSED - press P", True, (255, 255, 255), (0, 0, 0)), (380, 250))
        pygame.display.flip()

    scope.close()   # the scope shows NO SIGNAL about 0.5 s later
    pygame.quit()


if __name__ == "__main__":
    main()
