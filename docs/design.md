# Design notes

Authoritative design documentation is maintained as reStructuredText for Sphinx:

- **[design.rst](design.rst)** — layout, Windows naming, platform sync semantics, ring capacity notes, validation spin limits, cross-process tests.

To build HTML:

```bash
cd docs
pip install -r requirements.txt
sphinx-build -b html . _build/html
```
