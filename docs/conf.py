# Sphinx configuration for xproc documentation.
# https://www.sphinx-doc.org/en/master/usage/configuration.html

project = "xproc"
copyright = "xproc contributors"
author = "xproc contributors"
release = "0.2.0"
version = "0.2.0"

extensions = []

templates_path = ["_templates"]
exclude_patterns = ["_build", "Thumbs.db", ".DS_Store"]

html_theme = "alabaster"
html_static_path = ["_static"]

pygments_style = "sphinx"
