# Security policy

## Supported versions

The latest release and the current `main` branch receive fixes.

## Reporting a vulnerability

Please do not open a public issue for security problems. Use the private
reporting form:

<https://github.com/Nyree21/ollama_launcher/security/advisories/new>

Include the version, the steps to reproduce and the impact you expect. You will
get a first reply within a few days.

## Scope

The launcher starts a local `ollama serve` process, reads Windows process and
TCP information, and optionally runs a loopback HTTP proxy. Relevant issues are,
for example: the proxy binding to a non-loopback interface, log or configuration
injection through a crafted model or request, and privilege issues when the
launcher is started with elevated rights.

The behaviour of Ollama itself is out of scope — report that to the Ollama
project.
