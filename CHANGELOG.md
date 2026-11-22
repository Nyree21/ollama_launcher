# Changelog

## 1.0.0 - 2026-11-22

First public release.

- Start and stop `ollama serve` from a tray application; the process tree is
  terminated on exit.
- Single-instance guard: a second launch focuses the existing window.
- Live metrics: running and installed models, process memory, model VRAM,
  generated tokens, TTFT, generation speed, client connections, error counters.
- Log viewer with level colours, filters, access-log hiding, multi-select copy,
  export and a follow-latest lock.
- Tabs for connected clients and running models.
- TTFT benchmark using any installed model.
- Optional statistics proxy that accumulates tokens, TTFT and tok/s from the
  streamed NDJSON, with a `GET /__stats` endpoint.
- Settings dialog for every ini option, plus an English/Simplified Chinese
  language switch.
