#!/usr/bin/env python3

import argparse
import hashlib
import json
import math
import os
import sys
import tempfile
import urllib.request
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]


def validate_matrix(matrix, size, label, allow_negative=False):
    if (
        not isinstance(matrix, list) or len(matrix) != size or
        any(not isinstance(row, list) or len(row) != size for row in matrix)
    ):
        raise ValueError(f"{label} must be a {size}x{size} matrix")
    for i, row in enumerate(matrix):
        for j, value in enumerate(row):
            if (
                isinstance(value, bool) or
                not isinstance(value, (int, float)) or
                not math.isfinite(value) or
                (not allow_negative and value < 0)
            ):
                raise ValueError(f"{label}[{i}][{j}] is invalid: {value!r}")
            if i == j and value != 0:
                raise ValueError(f"{label}[{i}][{j}] must be zero")


def reference_cost(nodes, routes, matrix):
    indices = {str(node["osm_id"]): i for i, node in enumerate(nodes)}
    if len(indices) != len(nodes):
        raise ValueError("Duplicate osm_id in instance nodes")
    if not isinstance(routes, list):
        raise ValueError("Reference solution routes must be a list")
    cost = 0.0
    for route in routes:
        if not isinstance(route, list):
            raise ValueError("Each reference route must be a list")
        previous = 0
        for visit in route:
            current = indices[str(visit["osm_id"])]
            cost += matrix[previous][current]
            previous = current
        cost += matrix[previous][0]
    return round(cost, 2)


def circuity_factor(nodes, distances):
    depot = nodes[0]
    euclidean_total = 0.0
    for node in nodes[1:]:
        dy = (node["lat"] - depot["lat"]) * 111300
        dx = (
            (node["lng"] - depot["lng"]) * 111300 *
            math.cos(math.radians(depot["lat"]))
        )
        euclidean_total += math.sqrt(dx * dx + dy * dy)
    return round(
        sum(distances[0][1:]) / euclidean_total
        if euclidean_total > 0 else 1.0,
        4,
    )


def sha256(content):
    return hashlib.sha256(content).hexdigest()


def write_atomic(path, content):
    temporary = None
    mode = path.stat().st_mode & 0o777 if path.exists() else 0o644
    try:
        with tempfile.NamedTemporaryFile(
            dir=path.parent, prefix=f".{path.name}.", delete=False
        ) as file:
            temporary = Path(file.name)
            file.write(content)
            file.flush()
            os.fsync(file.fileno())
        os.chmod(temporary, mode)
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def apply_plan(plans, tables, backup_dir, base_url, dataset_id):
    for plan in plans:
        if plan["path"].is_symlink():
            raise ValueError(f"Refusing to replace symlink: {plan['path']}")
        if sha256(plan["path"].read_bytes()) != plan["original_sha256"]:
            raise ValueError(f"Instance changed during planning: {plan['path']}")
        if sha256(plan["solution_path"].read_bytes()) != plan["solution_sha256"]:
            raise ValueError(f"Reference solution changed: {plan['solution_path']}")

    backup_dir.mkdir(parents=True, exist_ok=False)
    originals = backup_dir / "originals"
    staged = backup_dir / "staged"
    originals.mkdir()
    staged.mkdir()
    entries = []
    for index, plan in enumerate(plans, 1):
        original = plan["path"].read_bytes()
        if sha256(original) != plan["original_sha256"]:
            raise ValueError(f"Instance changed during backup: {plan['path']}")
        data = json.loads(original)
        data["distance_matrix"] = tables[plan["coords"]]["distances"]
        data["meta"]["difficulty"]["reference_cost"] = plan["new_cost"]
        data["meta"]["difficulty"]["circuity_factor"] = plan["new_circuity"]
        replacement = json.dumps(data, indent=2, allow_nan=False).encode("utf-8")
        if original.endswith(b"\n"):
            replacement += b"\n"
        name = f"{index:04d}-{plan['path'].name}"
        (originals / name).write_bytes(original)
        (staged / name).write_bytes(replacement)
        entries.append({
            "instance": str(plan["path"].absolute()),
            "backup": f"originals/{name}",
            "staged": f"staged/{name}",
            "original_sha256": plan["original_sha256"],
            "replacement_sha256": sha256(replacement),
            "reference_solution": str(plan["solution_path"].absolute()),
            "reference_solution_sha256": plan["solution_sha256"],
        })

    manifest = {
        "schema_version": 1,
        "base_url": base_url,
        "dataset_id": dataset_id,
        "status": "prepared",
        "applied_count": 0,
        "entries": entries,
    }
    manifest_path = backup_dir / "manifest.json"

    def save_manifest():
        write_atomic(
            manifest_path,
            json.dumps(manifest, indent=2, allow_nan=False).encode("utf-8"),
        )

    save_manifest()
    try:
        for entry in entries:
            path = Path(entry["instance"])
            if sha256(path.read_bytes()) != entry["original_sha256"]:
                raise ValueError(f"Instance changed before replacement: {path}")
            replacement = (backup_dir / entry["staged"]).read_bytes()
            if sha256(replacement) != entry["replacement_sha256"]:
                raise ValueError(f"Staged replacement changed: {path}")
            write_atomic(path, replacement)
            manifest["applied_count"] += 1
            save_manifest()
        manifest["status"] = "complete"
        save_manifest()
    except Exception as error:
        manifest["status"] = "failed"
        manifest["error"] = str(error)
        save_manifest()
        raise


def main():
    parser = argparse.ArgumentParser(
        description="Plan instance matrix updates; write only with --apply."
    )
    parser.add_argument("instances", nargs="*", type=Path)
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--backup-dir", type=Path)
    args = parser.parse_args()
    if args.apply and args.backup_dir is None:
        parser.error("--apply requires --backup-dir")
    if args.backup_dir is not None and not args.apply:
        parser.error("--backup-dir requires --apply")
    if args.apply and args.backup_dir.exists():
        raise ValueError(f"Backup directory already exists: {args.backup_dir}")
    paths = args.instances or sorted(
        (PROJECT_ROOT / "data" / "instances").glob("*_instance.json")
    )
    if not paths:
        raise ValueError("No instances selected")

    base_url = os.environ.get("OSRM_BASE_URL", "").strip().rstrip("/")
    dataset_id = os.environ.get("OSRM_DATASET_ID", "").strip()
    if not base_url or not dataset_id:
        raise ValueError("Set OSRM_BASE_URL and OSRM_DATASET_ID first")

    tables = {}
    plans = []
    changed_instances = 0
    changed_cells = 0
    for path in paths:
        if not path.name.endswith("_instance.json"):
            raise ValueError(f"Expected an instance filename: {path}")
        original = path.read_bytes()
        data = json.loads(original)
        nodes = data["nodes"]
        if not nodes:
            raise ValueError(f"{path}: instance nodes are empty")
        size = len(nodes)
        old_distances = data["distance_matrix"]
        old_durations = data["duration_matrix"]
        validate_matrix(old_distances, size, "old distances", allow_negative=True)
        validate_matrix(old_durations, size, "old durations")

        solution_path = path.with_name(
            path.name.replace("_instance.json", "_solution.json")
        )
        solution_bytes = solution_path.read_bytes()
        solution = json.loads(solution_bytes)
        routes = solution["routes"]
        difficulty = data["meta"]["difficulty"]
        old_cost = reference_cost(nodes, routes, old_distances)
        if not math.isclose(
            old_cost, difficulty["reference_cost"], rel_tol=0, abs_tol=1e-6
        ):
            raise ValueError(f"{path}: stored reference_cost disagrees with solution")
        old_circuity = circuity_factor(nodes, old_distances)
        if not math.isclose(
            old_circuity, difficulty["circuity_factor"], rel_tol=0, abs_tol=1e-6
        ):
            raise ValueError(f"{path}: stored circuity_factor disagrees with matrix")

        coords = ";".join(f"{node['lng']},{node['lat']}" for node in nodes)
        if coords not in tables:
            url = (
                f"{base_url}/table/v1/motorcycle/{coords}"
                "?annotations=duration,distance"
            )
            with urllib.request.urlopen(url, timeout=60) as response:
                table = json.load(response)
            if not isinstance(table, dict) or table.get("code") != "Ok":
                raise ValueError(f"{path}: OSRM response must contain code=Ok")
            validate_matrix(table.get("distances"), size, "new distances")
            validate_matrix(table.get("durations"), size, "new durations")
            tables[coords] = table

        table = tables[coords]
        if table["durations"] != old_durations:
            raise ValueError(f"{path}: durations changed; review time windows first")
        new_distances = table["distances"]
        changes = sum(
            old != new
            for old_row, new_row in zip(old_distances, new_distances)
            for old, new in zip(old_row, new_row)
        )
        new_cost = reference_cost(nodes, routes, new_distances)
        new_circuity = circuity_factor(nodes, new_distances)
        would_update = (
            changes > 0 or new_cost != old_cost or new_circuity != old_circuity
        )
        if would_update:
            plans.append({
                "path": path,
                "original_sha256": sha256(original),
                "solution_path": solution_path,
                "solution_sha256": sha256(solution_bytes),
                "coords": coords,
                "new_cost": new_cost,
                "new_circuity": new_circuity,
            })
        changed_instances += int(would_update)
        changed_cells += changes
        print(json.dumps({
            "instance": str(path.resolve()),
            "distance_cells_changed": changes,
            "duration_cells_changed": 0,
            "reference_cost_old": old_cost,
            "reference_cost_new": new_cost,
            "circuity_factor_old": old_circuity,
            "circuity_factor_new": new_circuity,
            "would_update": would_update,
        }, allow_nan=False), flush=True)

    if args.apply:
        apply_plan(plans, tables, args.backup_dir, base_url, dataset_id)

    print(json.dumps({
        "dry_run": not args.apply,
        "dataset_id": dataset_id,
        "base_url": base_url,
        "instances_checked": len(paths),
        "unique_tables_requested": len(tables),
        "instances_to_update": changed_instances,
        "distance_cells_changed": changed_cells,
        "instances_updated": len(plans) if args.apply else 0,
        "backup_dir": str(args.backup_dir.absolute()) if args.apply else None,
    }, allow_nan=False), flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(2)
