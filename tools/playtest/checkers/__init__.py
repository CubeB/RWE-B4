"""Checker registry.

Each module in this package exposes ``check(run_dir, context) -> list[Finding]``
and is registered in ``CHECKERS`` under its name, so ``run.py`` iterates the
registry without knowing which checkers exist. Adding a checker is one import
and one entry; nothing in the runner changes.
"""

from .economy import Finding
from .economy import check as _economy_check

CHECKERS = {
    "economy": _economy_check,
}

__all__ = ["CHECKERS", "Finding"]
