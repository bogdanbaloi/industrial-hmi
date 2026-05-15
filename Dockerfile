# =============================================================================
# Industrial HMI -- multi-stage Docker image, three deploy variants.
#
# Stage 1 (`builder`) installs the full C++20 toolchain + dev headers,
# fetches third-party deps via CMake FetchContent, and compiles BOTH
# binaries (`industrial-hmi-console` + `industrial-hmi`). The compile
# work is shared between targets, so producing both adds only ~15s.
#
# Stage 2 (`gtk`)     starts from a fresh Ubuntu base and ships the GTK
#                     binary alongside Xvfb + x11vnc + noVNC so a
#                     browser at http://localhost:6080 sees the
#                     dashboard. No host-side X server install needed.
#
# Stage 3 (`console`) (LAST = default) ships only the headless
#                     `industrial-hmi-console` binary plus its runtime
#                     libs. ~200 MB final image, no GUI stack.
#
# Build (headless, default):
#     docker build -t industrial-hmi:console .
#
# Build (GUI variant):
#     docker build --target gtk -t industrial-hmi:gtk .
#
# Run (headless):
#     docker run --rm -it industrial-hmi:console
#
# Run (GUI -> browser):
#     docker run --rm -p 6080:6080 industrial-hmi:gtk
#     # then open http://localhost:6080/vnc.html in any browser
#
# Override build type:
#     docker build --build-arg BUILD_TYPE=Debug -t industrial-hmi:debug .
# =============================================================================


# -----------------------------------------------------------------------------
# Stage 1: builder
# -----------------------------------------------------------------------------
#
# `AS builder` names the stage so the final stage can `COPY --from=builder ...`.
# Ubuntu 24.04 matches the project's CI runner + the dev WSL environment, so
# library versions (GTK4 4.10, SQLite 3.45, Boost 1.83) line up exactly with
# what `BUILD.md` documents.
FROM ubuntu:24.04 AS builder

# Build-args let CI / operators tune the image without editing this file.
# Defaults to Release for production images; Debug build is one flag away
# for an investigation image.
ARG BUILD_TYPE=Release

# Disable apt's interactive prompts -- `tzdata` etc. would otherwise hang
# waiting for keyboard input that never arrives in a non-tty container.
ENV DEBIAN_FRONTEND=noninteractive

# Install the toolchain + every -dev package the build needs.
#
# Why a single `RUN` (instead of one per package): each `RUN` creates a new
# image layer, and apt's index download is the slow part. Coalescing into
# one layer keeps the build cache effective and the layer count low.
#
# `--no-install-recommends` skips Ubuntu's "suggested" extras that bloat
# the layer without ever being used (man pages, doc packages).
#
# `rm -rf /var/lib/apt/lists/*` at the end discards apt's package index --
# we only needed it for THIS layer's install; saving it would carry ~40 MB
# of useless metadata into the layer.
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    libgtkmm-4.0-dev \
    libsqlite3-dev \
    libsodium-dev \
    libboost-dev \
    libgtest-dev \
    libgmock-dev \
    gettext \
    git \
    ca-certificates \
 && rm -rf /var/lib/apt/lists/*

# Why ca-certificates here: the project's CMakeLists fetches stb_image
# from github.com via FetchContent + git clone. A bare Ubuntu install
# with --no-install-recommends doesn't include any CA trust store, so
# the clone fails with "server certificate verification failed".
# Pulling ca-certificates explicitly fixes the chain.

# `WORKDIR` is the in-container cwd for subsequent commands. Creating it
# implicitly + `cd`-ing in one go.
WORKDIR /src

# Copy the project sources into the build stage. The `.dockerignore` at
# the repo root keeps `build/`, `.git/`, `.venv/` etc. OUT of the copy --
# without it, every `docker build` would ship the entire build tree to
# the daemon as context.
COPY . .

# Configure + build the console binary.
#
# - BUILD_OPCUA_BACKEND=OFF: open62541 builds from source via FetchContent
#   and adds ~3 minutes to a cold build. The console image ships TCP +
#   MQTT + Modbus only; OPC-UA opt-in via a separate image variant (or
#   `--build-arg BUILD_OPCUA=ON` once the Dockerfile grows that knob).
# - BUILD_TESTS=OFF: container build is for shipping, not CI. Tests run
#   on the host / CI runner; baking them into a deploy image wastes
#   compile time and final binary size.
# - `cmake --build` runs the configured generator (Ninja) using all
#   available CPU cores by default.
RUN cmake -S . -B build \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
        -DBUILD_OPCUA_BACKEND=OFF \
        -DBUILD_TESTS=OFF \
 && cmake --build build \
        --target industrial-hmi-console industrial-hmi
# Two targets in one cmake invocation builds both the headless
# `industrial-hmi-console` and the GTK `industrial-hmi`. Most of
# the compile work (presenter / model / integration object libs)
# is shared between targets, so linking the extra GUI binary adds
# only ~15s to the overall build.


# -----------------------------------------------------------------------------
# Stage 2: gtk -- GUI variant with noVNC web access
# -----------------------------------------------------------------------------
#
# Ships the GTK4 binary plus a virtual X server (Xvfb), an X-to-VNC
# bridge (x11vnc), and a VNC-to-WebSocket gateway (noVNC + websockify).
# The container exposes a single port: 6080. Anyone with a browser can
# point to `http://localhost:6080/vnc.html` and see the dashboard
# rendered as if it were running on a real desktop -- zero install on
# the host beyond Docker.
#
# Stack inside the container, top to bottom:
#
#   industrial-hmi (GTK4)
#         │  draws via the X11 protocol
#         ▼
#   Xvfb :0                 -- in-memory X server (no monitor needed)
#         │  shares its framebuffer with
#         ▼
#   x11vnc                  -- exposes the framebuffer over the VNC protocol on :5900
#         │  websockify wraps in WebSocket frames
#         ▼
#   noVNC web client at :6080
#         │
#         ▼
#   Operator's browser
#
# Build:
#     docker build --target gtk -t industrial-hmi:gtk .
# Run:
#     docker run --rm -p 6080:6080 industrial-hmi:gtk
#     # then open http://localhost:6080/vnc.html (no password)
FROM ubuntu:24.04 AS gtk

ENV DEBIAN_FRONTEND=noninteractive

# Runtime libs + the GUI bridge stack.
# - libgtkmm-4.0-0 + libsqlite3-0: same as console.
# - xvfb: headless X server.
# - x11vnc: tap into Xvfb's framebuffer and serve it via VNC.
# - novnc + websockify: HTML5 / WebSocket frontend so a plain browser
#   reaches the VNC stream without a native VNC client.
# - dbus-x11 + libxkbcommon: gtkmm-4 wants D-Bus + xkbcommon at runtime
#   even when there's no real display manager; missing them surfaces as
#   silent crashes inside gtk_init().
# - ca-certificates: same TLS rationale as the console stage.
RUN apt-get update && apt-get install -y --no-install-recommends \
    libgtkmm-4.0-0 \
    libsqlite3-0 \
    libsodium23 \
    xvfb \
    x11vnc \
    novnc \
    websockify \
    dbus-x11 \
    libxkbcommon0 \
    fluxbox \
    ca-certificates \
    tzdata \
 && rm -rf /var/lib/apt/lists/*

# Why fluxbox: Xvfb is just a framebuffer -- it has no concept of
# windows, titles, maximise / minimise. The HMI's main window opens
# at whatever size it asks for and stays there with no decorations.
# fluxbox is a ~5 MB window manager that provides title bars, drag,
# resize, and (critically) sends the GTK window the "maximise"
# request the project's startup code handles. With this in place the
# dashboard fills the noVNC viewport instead of floating off-center.

# Non-root user just like the console stage.
RUN useradd --create-home --shell /bin/bash hmi
USER hmi
WORKDIR /home/hmi

COPY --from=builder --chown=hmi:hmi /src/build/industrial-hmi /usr/local/bin/
COPY --from=builder --chown=hmi:hmi /src/config /home/hmi/config
COPY --from=builder --chown=hmi:hmi /src/assets /home/hmi/assets

# Startup script orchestrates Xvfb + x11vnc + websockify + the GTK
# binary. Bash is fine here -- the supervisor responsibilities are
# trivial enough that pulling in s6-overlay / supervisord would be
# over-engineering for a demo image.
COPY --chown=hmi:hmi <<'EOF' /home/hmi/start-gtk.sh
#!/bin/bash
# Start an in-memory X server on display :0.
# Resolution 1920x1080 matches `window.default_{width,height}` in
# app-config.json so the dashboard does not overflow the viewport.
# 24-bit colour depth keeps gtkmm happy (8-bit deprecates several
# rendering paths and produces visual glitches).
Xvfb :0 -screen 0 1920x1080x24 -ac +extension GLX +render -noreset &
sleep 1

export DISPLAY=:0

# Start a minimal window manager. Xvfb on its own is just a frame-
# buffer -- no title bars, no concept of maximise. fluxbox provides
# the WM surface the GTK app's "default_mode: fullscreen" hint
# needs to actually fill the viewport.
#
# stderr -> /dev/null: fluxbox probes ~40 session.* settings on
# every cold start and logs "Failed to read: session.X / Setting
# default value" for each one whose value isn't already on disk.
# All defaults are fine for the container -- the noise is purely
# cosmetic and clutters `docker compose up` output. The startup
# script forwards everything else (Xvfb / x11vnc / websockify /
# HMI logs) so we don't lose anything actionable.
fluxbox -display :0 2>/dev/null &
sleep 1

# Bridge X11 -> VNC.
#   -display :0      attach to the framebuffer Xvfb created
#   -nopw            skip VNC auth (the surface is bound to localhost
#                    in the docker-compose case; for public deploys add
#                    `-passwd $SOME_PASSWORD` here)
#   -forever         keep accepting new VNC clients after disconnect
#   -shared          allow multiple concurrent viewers
#   -bg              daemonise so the script keeps moving
#   -threads         multi-thread the screen-poll loop. Single-threaded
#                    x11vnc maxes one CPU on a busy framebuffer and feels
#                    laggy in noVNC; with -threads each client handler
#                    runs on its own thread and the encode pipeline
#                    parallelises across cores.
#   -nowf            skip the "wireframe drag" trick; on a small
#                    container display it actively hurts more than it
#                    helps.
#   -noxdamage       skip the X DAMAGE extension. Counter-intuitive
#                    because DAMAGE is meant as an optimisation, but on
#                    Xvfb it reports the full screen as damaged after
#                    GTK redraws -- x11vnc then re-encodes everything
#                    anyway, paying the DAMAGE syscall cost on top.
#   -wait 30         polling interval ms. Default 20 -> 30 saves CPU
#                    without an operator-visible difference (the
#                    dashboard updates at ~1 Hz, not 50 Hz).
#
# NOTE: do NOT add `-ncache N` here. x11vnc's client-side cache
# allocates extra framebuffer height (N * screen height) below the
# real screen; noVNC's `resize=scale` then scales the WHOLE thing,
# shrinking the dashboard into the top tile and leaving the cache
# region as a giant black bar. The flag works fine with native VNC
# viewers but is incompatible with the browser-canvas client.
x11vnc -display :0 -nopw -forever -shared -bg -rfbport 5900 \
       -listen 0.0.0.0 -quiet \
       -threads -nowf -noxdamage -wait 30

# Serve noVNC's HTML5 client + bridge WebSocket -> VNC.
# `--web` points websockify at the noVNC static assets; clients hit
# `http://host:6080/vnc.html` and the page connects back to ws://host:6080.
websockify --web=/usr/share/novnc/ 6080 localhost:5900 &
sleep 1

# Finally start the HMI. `exec` replaces the script's PID so a docker
# stop / SIGTERM reaches the GTK app, not the wrapper.
exec industrial-hmi
EOF

RUN chmod +x /home/hmi/start-gtk.sh

# Pre-seed a minimal fluxbox config so the WM does not spam stderr
# with `Failed to read: session.*` lines on every cold start. Without
# this, fluxbox probes ~40 settings, fails each lookup, logs a warning
# per failed key, and falls back to the same defaults we want anyway.
# An empty init file short-circuits the probes -- fluxbox sees a
# config exists, treats every absent key as "use default", silently.
RUN mkdir -p /home/hmi/.fluxbox && \
    touch /home/hmi/.fluxbox/init && \
    touch /home/hmi/.fluxbox/keys && \
    touch /home/hmi/.fluxbox/menu

# Historian SQLite directory. The bundled config points at
# `data/historian.sqlite` (relative to the binary's cwd = /home/hmi);
# without this the open fails and the History tab is dropped silently.
RUN mkdir -p /home/hmi/data

# Default landing page: redirect root + /vnc.html to a pre-parametrised
# URL that (1) scales the remote desktop to fit the browser viewport
# and (2) auto-connects without the "Connect" button click. Operator
# opens http://localhost:6080 and the dashboard appears.
#
# noVNC's `/usr/share/novnc/index.html` is a thin redirector by default;
# we replace it with one that carries the query string we want. The
# original vnc.html stays in place untouched.
USER root
RUN cat > /usr/share/novnc/index.html <<'HTML'
<!doctype html>
<meta http-equiv="refresh" content="0; url=vnc.html?resize=scale&autoconnect=true">
<title>Industrial HMI</title>
HTML
USER hmi

# Document the port; not strictly required (docker run -p still binds
# correctly without it) but `docker image inspect` surfaces this in
# orchestration UIs.
EXPOSE 6080

CMD ["/home/hmi/start-gtk.sh"]


# -----------------------------------------------------------------------------
# Stage 3: console -- headless deploy (DEFAULT target)
# -----------------------------------------------------------------------------
#
# Fresh FROM -- nothing from the builder is preserved automatically. Only
# explicit `COPY --from=builder` brings artefacts forward. That's exactly
# how the image stays small: the compiler + -dev headers + git + the
# whole /src tree never make it into the published image.
#
# This stage stays LAST so `docker build .` without `--target` produces
# the console image. Add `--target gtk` for the GUI variant above.
FROM ubuntu:24.04 AS console

ENV DEBIAN_FRONTEND=noninteractive

# Runtime libraries only -- no `-dev` packages. The headless console
# never opens a window, but it still links against gtkmm symbols
# through transitive headers (DatabaseManager / Application sit in the
# same object-library tree). Loading those shared libs is cheap if
# they're never touched at runtime.
#
# Package names on Ubuntu 24.04 (noble), verified via
# `apt-cache search '^libgtkmm-4'`:
# - `libgtkmm-4.0-0` -- runtime shared libraries for gtkmm-4. The
#   project's ABI does not surface time_t in its API so this package
#   did NOT pick up the `t64` suffix that other libraries got in
#   early 2024.
# - `libsqlite3-0` -- unchanged.
# - Boost: the project links only header-only Boost
#   (`Boost::signals2` / `Boost::headers`), so no runtime Boost
#   shared library is needed. Confirmed by `ldd` on the built
#   binary -- no libboost_*.so dependencies.
#
# `ca-certificates` keeps TLS handshakes valid if the operator
# points the MQTT client at a remote broker with a real cert (current
# code uses plain TCP; ~200 KB cost so we include it pre-emptively).
RUN apt-get update && apt-get install -y --no-install-recommends \
    libgtkmm-4.0-0 \
    libsqlite3-0 \
    libsodium23 \
    ca-certificates \
 && rm -rf /var/lib/apt/lists/*

# Non-root user. Containers running as root can do surprising things if
# someone bind-mounts a host volume (writes appear as root on the host).
# The HMI does not need privileged operations -- a plain user is fine.
RUN useradd --create-home --shell /bin/bash hmi
USER hmi
WORKDIR /home/hmi

# Bring the binary across. Single file; the image needs nothing else from
# the build stage.
COPY --from=builder --chown=hmi:hmi /src/build/industrial-hmi-console /usr/local/bin/

# Ship the default config + assets so the container starts without an
# external volume mount. Operators that need custom config bind-mount
# their own `app-config.json` over `/home/hmi/config/`.
COPY --from=builder --chown=hmi:hmi /src/config /home/hmi/config
COPY --from=builder --chown=hmi:hmi /src/assets /home/hmi/assets

# Console binary reads `config/app-config.json` relative to its cwd by
# default (Bootstrap path resolution). Setting WORKDIR + the COPY above
# means the binary finds its config without --config flag.
#
# `CMD` (not `ENTRYPOINT`) so `docker run image quit` overrides the
# default command; useful for one-shot probes.
CMD ["industrial-hmi-console"]
