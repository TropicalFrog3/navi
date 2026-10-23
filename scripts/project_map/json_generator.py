"""
JSON generator for tooling and automated agent workflows.
"""

from __future__ import annotations

import json
from .models import ProjectMap


def generate_json(project_map: ProjectMap) -> str:
    """
    Serializes the ProjectMap to deterministic pretty-printed JSON.
    """
    data = project_map.to_dict()
    return json.dumps(data, indent=2, sort_keys=True)
