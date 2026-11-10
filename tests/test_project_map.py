"""
@file  test_project_map.py
@brief Verifies workspace floor plan and CONTEXT.md routing tables


Comprehensive test suite for the Project Code Map Generator.
Tests:
  - Nested functions and closures
  - Classes, inheritance, and methods
  - Imports (internal vs external) and exports (__all__)
  - Call graph resolution (direct, intra-class, cross-file, uncertain/dynamic)
  - C/C++ adapter
  - JavaScript / TypeScript adapter
  - CLI commands and options (--include, --exclude, --format json/markdown, --max-depth, --include-bodies, --include-private)
  - Output determinism (byte-for-byte identical across runs)
"""

from __future__ import annotations

import json
from pathlib import Path
import pytest

from scripts.project_map.models import ParseConfig, SymbolKind, Visibility
from scripts.project_map.engine import run_project_map
from scripts.project_map.adapters.python_adapter import PythonAdapter
from scripts.project_map.adapters.cpp_adapter import CppAdapter
from scripts.project_map.adapters.js_ts_adapter import JsTsAdapter
from scripts.project_map.cli import main as cli_main


@pytest.fixture
def sample_project(tmp_path: Path) -> Path:
    """Creates a realistic multi-file, multi-language project for testing."""
    root = tmp_path / "sample_app"
    root.mkdir()

    # File 1: core/base.py
    core_dir = root / "core"
    core_dir.mkdir()
    (core_dir / "__init__.py").write_text('"""Core package."""\n__all__ = ["BaseService"]\n')
    (core_dir / "base.py").write_text('''"""
Base service definitions.
"""
from typing import Optional

class BaseService:
    """Base class for all business services."""
    
    def __init__(self, name: str, timeout: float = 30.0) -> None:
        self.name = name
        self.timeout = timeout

    def ping(self) -> bool:
        """Healthcheck endpoint."""
        return True

    def _internal_reset(self) -> None:
        """Internal cleanup."""
        pass
''')

    # File 2: core/service.py
    (core_dir / "service.py").write_text('''"""
Concrete user service with nested functions, inheritance, and cross-file calls.
"""
from typing import List, Optional
import os
import sys

from core.base import BaseService

__all__ = ["UserService", "compute_stats"]

def compute_stats(items: List[int]) -> int:
    """Compute aggregate stats."""
    return sum(items)

class UserService(BaseService):
    """User service handling business logic."""

    @classmethod
    def create_default(cls) -> "UserService":
        return cls(name="default_user_service")

    def process_users(self, user_ids: List[int], flag: bool = True) -> int:
        """Process user batch with nested closure."""
        self.ping()

        multiplier = 2 if flag else 1

        def transform_id(uid: int) -> int:
            """Nested closure transforming an ID."""
            return uid * multiplier

        transformed = [transform_id(uid) for uid in user_ids]
        return compute_stats(transformed)

    def _private_helper(self) -> None:
        pass
''')

    # File 3: utils/helpers.cpp (C++ file)
    utils_dir = root / "utils"
    utils_dir.mkdir()
    (utils_dir / "math_utils.hpp").write_text('''// Math utility header
#ifndef MATH_UTILS_HPP
#define MATH_UTILS_HPP

#include <vector>
#include <string>
#include "core/base.hpp"

namespace app::utils {

/// Math helper class
class MathHelper : public BaseCalculator {
public:
    MathHelper();
    virtual ~MathHelper();

    int add(int a, int b = 0) const;
    virtual double multiply(double x, double y) override;

private:
    void secret_hash(const std::string& input);
};

int global_calculate(int x);

} // namespace app::utils

#endif
''')

    # File 4: frontend/client.ts (TypeScript file)
    frontend_dir = root / "frontend"
    frontend_dir.mkdir()
    (frontend_dir / "client.ts").write_text('''/**
 * Frontend API client.
 */
import { BaseConfig } from './config';
import axios from 'axios';

export interface UserResponse {
    id: string;
    username: string;
}

export type ResultCallback = (res: UserResponse) => void;

export class ApiClient extends BaseConfig {
    private apiKey: string;

    constructor(endpoint: string, apiKey: string) {
        super(endpoint);
        this.apiKey = apiKey;
    }

    public async fetchUser(userId: string): Promise<UserResponse> {
        return { id: userId, username: "test_user" };
    }
}

export const initClient = async (url: string = "http://localhost"): Promise<ApiClient> => {
    return new ApiClient(url, "secret");
};
''')

    return root


def test_nested_functions_and_containment(sample_project: Path):
    """Verify nested function / closure detection and containment hierarchy."""
    config = ParseConfig(
        root_path=sample_project,
        output_path=sample_project / "map.md",
        include_patterns=["core/service.py"],
        include_private=True,
    )
    project_map, _ = run_project_map(config)

    # Verify UserService exists
    user_service_id = "core/service.py::UserService"
    assert user_service_id in project_map.symbols
    user_service = project_map.symbols[user_service_id]
    assert user_service.kind == SymbolKind.CLASS
    assert "BaseService" in user_service.inherits

    # Verify method process_users
    proc_id = "core/service.py::UserService.process_users"
    assert proc_id in project_map.symbols
    proc_method = project_map.symbols[proc_id]
    assert proc_method.kind == SymbolKind.METHOD
    assert proc_method.parent_id == user_service_id
    assert proc_id in user_service.children_ids

    # Verify nested function transform_id
    nested_id = "core/service.py::UserService.process_users.transform_id"
    assert nested_id in project_map.symbols
    nested_func = project_map.symbols[nested_id]
    assert nested_func.kind == SymbolKind.NESTED_FUNCTION
    assert nested_func.is_nested is True
    assert nested_func.parent_id == proc_id
    assert nested_id in proc_method.children_ids
    assert len(nested_func.parameters) == 1
    assert nested_func.parameters[0].name == "uid"
    assert nested_func.parameters[0].type_annotation == "int"
    assert nested_func.return_type == "int"


def test_classes_and_inheritance(sample_project: Path):
    """Verify class extraction, bases inheritance, and method modifiers."""
    config = ParseConfig(
        root_path=sample_project,
        output_path=sample_project / "map.md",
        include_patterns=["core/**"],
        include_private=True,
    )
    project_map, _ = run_project_map(config)

    base_id = "core/base.py::BaseService"
    assert base_id in project_map.symbols
    base_class = project_map.symbols[base_id]
    assert base_class.kind == SymbolKind.CLASS
    assert base_class.visibility == Visibility.PUBLIC
    # Check UserService is exported (__all__)
    assert project_map.symbols["core/service.py::UserService"].visibility == Visibility.EXPORTED

    # Check BaseService.__init__ parameters
    init_id = "core/base.py::BaseService.__init__"
    assert init_id in project_map.symbols
    init_method = project_map.symbols[init_id]
    param_names = [p.name for p in init_method.parameters]
    assert param_names == ["self", "name", "timeout"]
    assert init_method.parameters[2].default_value == "30.0"
    assert init_method.parameters[2].is_required is False

    # Check @classmethod in UserService
    create_id = "core/service.py::UserService.create_default"
    assert create_id in project_map.symbols
    create_method = project_map.symbols[create_id]
    assert any("@classmethod" in m for m in create_method.modifiers)


def test_imports_and_exports(sample_project: Path):
    """Verify internal vs external imports classification and __all__ exports."""
    config = ParseConfig(
        root_path=sample_project,
        output_path=sample_project / "map.md",
        include_patterns=["core/service.py"],
    )
    project_map, _ = run_project_map(config)

    file_info = project_map.files["core/service.py"]
    assert "UserService" in file_info.exports
    assert "compute_stats" in file_info.exports

    # Internal imports vs external dependencies
    internal_mods = [imp.module for imp in file_info.imports if imp.is_internal]
    external_mods = [imp.module for imp in file_info.imports if not imp.is_internal]
    assert "core.base" in internal_mods
    assert "os" in external_mods or "typing" in external_mods


def test_call_graph_and_cross_references(sample_project: Path):
    """Verify callers, callees, intra-class and cross-file reference resolution."""
    config = ParseConfig(
        root_path=sample_project,
        output_path=sample_project / "map.md",
        include_patterns=["core/**"],
        include_private=True,
    )
    project_map, _ = run_project_map(config)

    proc_id = "core/service.py::UserService.process_users"
    proc_method = project_map.symbols[proc_id]

    # process_users calls self.ping() -> resolved to BaseService.ping
    ping_id = "core/base.py::BaseService.ping"
    assert ping_id in proc_method.resolved_callees
    # Reverse caller check
    assert proc_id in project_map.symbols[ping_id].resolved_callers

    # process_users calls compute_stats() -> resolved to compute_stats in same file
    stats_id = "core/service.py::compute_stats"
    assert stats_id in proc_method.resolved_callees
    assert proc_id in project_map.symbols[stats_id].resolved_callers


def test_cpp_adapter(sample_project: Path):
    """Verify C++ adapter parsing classes, inheritance, namespaces, visibility, and #includes."""
    config = ParseConfig(
        root_path=sample_project,
        output_path=sample_project / "map.md",
        include_patterns=["utils/**"],
        include_private=True,
    )
    project_map, _ = run_project_map(config)

    # Class MathHelper
    math_helper_id = "utils/math_utils.hpp::app::utils::MathHelper"
    assert math_helper_id in project_map.symbols
    cls_sym = project_map.symbols[math_helper_id]
    assert cls_sym.kind == SymbolKind.CLASS
    assert "public BaseCalculator" in cls_sym.inherits

    # Public method: add
    add_id = "utils/math_utils.hpp::app::utils::MathHelper::add"
    assert add_id in project_map.symbols
    add_sym = project_map.symbols[add_id]
    assert add_sym.kind == SymbolKind.METHOD
    assert add_sym.visibility == Visibility.PUBLIC
    assert add_sym.return_type == "int"
    assert "const" in add_sym.modifiers

    # Private method: secret_hash
    secret_id = "utils/math_utils.hpp::app::utils::MathHelper::secret_hash"
    assert secret_id in project_map.symbols
    secret_sym = project_map.symbols[secret_id]
    assert secret_sym.visibility == Visibility.PRIVATE

    # Verify #includes
    fi = project_map.files["utils/math_utils.hpp"]
    external_incs = [i.module for i in fi.imports if not i.is_internal]
    internal_incs = [i.module for i in fi.imports if i.is_internal]
    assert "vector" in external_incs
    assert "core/base.hpp" in internal_incs


def test_js_ts_adapter(sample_project: Path):
    """Verify JS/TS adapter parsing classes, interfaces, types, methods, async arrow functions."""
    config = ParseConfig(
        root_path=sample_project,
        output_path=sample_project / "map.md",
        include_patterns=["frontend/**"],
        include_private=True,
    )
    project_map, _ = run_project_map(config)

    # Interface UserResponse
    iface_id = "frontend/client.ts::UserResponse"
    assert iface_id in project_map.symbols
    assert project_map.symbols[iface_id].kind == SymbolKind.INTERFACE

    # Type alias ResultCallback
    type_id = "frontend/client.ts::ResultCallback"
    assert type_id in project_map.symbols
    assert project_map.symbols[type_id].kind == SymbolKind.TYPE

    # Class ApiClient extending BaseConfig
    cls_id = "frontend/client.ts::ApiClient"
    assert cls_id in project_map.symbols
    assert "BaseConfig" in project_map.symbols[cls_id].inherits

    # Method fetchUser
    fetch_id = "frontend/client.ts::ApiClient.fetchUser"
    assert fetch_id in project_map.symbols
    fetch_method = project_map.symbols[fetch_id]
    assert fetch_method.return_type == "Promise<UserResponse>"
    assert "async" in fetch_method.modifiers

    # Exported arrow function initClient
    init_id = "frontend/client.ts::initClient"
    assert init_id in project_map.symbols
    init_sym = project_map.symbols[init_id]
    assert init_sym.kind == SymbolKind.FUNCTION
    assert init_sym.visibility == Visibility.EXPORTED


def test_cli_generate_json_format(sample_project: Path):
    """Verify CLI --format json output produces valid, parseable JSON schema."""
    json_out = sample_project / "map.json"
    ret = cli_main([
        "generate",
        "--root", str(sample_project),
        "--output", str(json_out),
        "--format", "json",
    ])
    assert ret == 0
    assert json_out.exists()

    data = json.loads(json_out.read_text(encoding="utf-8"))
    assert "project_name" in data
    assert "files" in data
    assert "symbols" in data
    assert "call_graph" in data
    assert "stats" in data
    assert data["stats"]["total_files"] >= 4


def test_cli_exclude_and_max_depth(sample_project: Path):
    """Verify --exclude and --max-depth CLI flags."""
    out_md = sample_project / "filtered.md"
    ret = cli_main([
        "generate",
        "--root", str(sample_project),
        "--output", str(out_md),
        "--exclude", "frontend/**",
        "--exclude", "utils/**",
        "--max-depth", "2",
    ])
    assert ret == 0
    assert out_md.exists()

    content = out_md.read_text(encoding="utf-8")
    assert "frontend/client.ts" not in content
    assert "utils/math_utils.hpp" not in content
    assert "core/service.py" in content


def test_deterministic_output(sample_project: Path):
    """Verify that regenerating the map produces identical byte-for-byte output."""
    out_file = sample_project / "PROJECT_MAP.md"

    ret1 = cli_main(["generate", "--root", str(sample_project), "--output", str(out_file)])
    content1 = out_file.read_bytes()

    ret2 = cli_main(["generate", "--root", str(sample_project), "--output", str(out_file)])
    content2 = out_file.read_bytes()

    assert ret1 == 0
    assert ret2 == 0
    assert content1 == content2


def test_include_bodies_flag(sample_project: Path):
    """Verify --include-bodies includes code previews."""
    out_bodies = sample_project / "bodies.md"
    ret = cli_main([
        "generate",
        "--root", str(sample_project),
        "--output", str(out_bodies),
        "--include", "core/base.py",
        "--include-bodies",
        "--include-private",
    ])
    assert ret == 0
    content = out_bodies.read_text(encoding="utf-8")
    assert "Body Preview:" in content
    assert "return True" in content


def test_uncertain_dynamic_references(tmp_path: Path):
    """Verify that dynamically dispatched or ambiguous calls are marked uncertain, not direct."""
    project = tmp_path / "dynamic_app"
    project.mkdir()
    (project / "models.py").write_text('''
class User:
    def save(self) -> None:
        pass

class Product:
    def save(self) -> None:
        pass
''')
    (project / "service.py").write_text('''
def handle_entity(entity):
    entity.save()
''')

    config = ParseConfig(
        root_path=project,
        output_path=project / "map.md",
        include_private=True,
    )
    project_map, md = run_project_map(config)

    handler_sym = project_map.symbols["service.py::handle_entity"]
    # entity.save() should be marked as uncertain/dynamic call, NOT direct
    assert len(handler_sym.uncertain_callees) >= 1
    assert "service.py::handle_entity" not in project_map.call_graph
    assert "uncertain (dynamic)" in md


def test_route_decorator_detection(tmp_path: Path):
    """Verify route decorator detection on functions."""
    project = tmp_path / "web_app"
    project.mkdir()
    (project / "app.py").write_text('''
@app.route("/api/users", methods=["GET"])
def get_users():
    """Retrieve users list."""
    return []
''')
    config = ParseConfig(
        root_path=project,
        output_path=project / "map.md",
    )
    project_map, _ = run_project_map(config)
    route_sym = project_map.symbols["app.py::get_users"]
    assert route_sym.kind == SymbolKind.ROUTE
    assert any("route" in m for m in route_sym.modifiers)


def test_private_symbols_filtering(tmp_path: Path):
    """Verify private symbols are excluded by default and included with flag."""
    project = tmp_path / "priv_app"
    project.mkdir()
    (project / "module.py").write_text('''
def public_func():
    pass

def _internal_func():
    pass

def __private_func():
    pass
''')
    # Default: private excluded
    cfg_default = ParseConfig(root_path=project, output_path=project / "default.md", include_private=False)
    map_default, _ = run_project_map(cfg_default)
    assert "module.py::public_func" in map_default.symbols
    assert "module.py::_internal_func" not in map_default.symbols
    assert "module.py::__private_func" not in map_default.symbols

    # Flag: private included
    cfg_priv = ParseConfig(root_path=project, output_path=project / "priv.md", include_private=True)
    map_priv, _ = run_project_map(cfg_priv)
    assert "module.py::public_func" in map_priv.symbols
    assert "module.py::_internal_func" in map_priv.symbols
    assert "module.py::__private_func" in map_priv.symbols

