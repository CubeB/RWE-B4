"""Checker registry.

Each module in this package exposes ``check(run_dir, context) -> list[Finding]``
and is registered in ``CHECKERS`` under its name, so ``run.py`` iterates the
registry without knowing which checkers exist. Adding a checker is one import
and one entry; nothing in the runner changes.
"""

from .economy import Finding
from .economy import check as _economy_check
from .production import check as _production_check
from .unitdeath import check as _unitdeath_check
from .invariant import check as _invariant_check
from .pathfind import check as _pathfind_check
from .aiperf import check as _aiperf_check
from .desync import check as _desync_check

CHECKERS = {
    "economy": _economy_check,
    "production": _production_check,
    "unitdeath": _unitdeath_check,
    "invariant": _invariant_check,
    "pathfind": _pathfind_check,
    "aiperf": _aiperf_check,
    "desync": _desync_check,
}

__all__ = ["CHECKERS", "Finding"]
