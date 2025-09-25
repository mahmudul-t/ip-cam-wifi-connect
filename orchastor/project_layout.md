# Project: Button-Driven Wi‑Fi/AP Orchestrator (single binary with built-in Tiny Server)
# -----------------------------------------------------------------------------
# Layout
#   Makefile
#   src/
#     config.h
#     log.h
#     gpio_btn.h        gpio_btn.c
#     proc.h            proc.c
#     net.h             net.c
#     tinyserver.h      tinyserver.c   <-- integrated tiny server (from your code)
#     flow.h            flow.c         <-- starts/stops tiny server via API
#     orchestrator_main.c
#
# Build:
#   $ make            # native
#   $ make STATIC=1   # static (BusyBox-friendly)
#
# Runtime:
#   - Binary is a single daemon: `orchestrator`
#   - On long press: kills wifi/app -> starts AP -> **starts embedded tiny server** -> waits
#     for onboard POST -> Wi‑Fi (enable_wifi.sh) -> NFS -> app.