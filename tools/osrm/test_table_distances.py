#!/usr/bin/env python3

import argparse
import json
import math
import sys
import urllib.error
import urllib.request


REGRESSION_COORDINATES = (
    "100.305168,13.654952;100.305169,13.654955",
    "100.305169,13.654955;100.305168,13.654952",
    "100.415065,13.825256;100.415065,13.825257",
    "100.415065,13.825257;100.415065,13.825256",
    "100.434668,13.731343;100.434669,13.73134",
    "100.434669,13.73134;100.434668,13.731343",
    "100.439924,13.739731;100.439924,13.739733",
    "100.439924,13.739733;100.439924,13.739731",
    "100.511076,13.778031;100.511076,13.778032",
    "100.511076,13.778032;100.511076,13.778031",
    "100.526781,13.786827;100.52678,13.786827",
    "100.564841,13.763745;100.564841,13.763746",
    "100.564841,13.763746;100.564841,13.763745",
    "100.691657,13.918493;100.691661,13.918492",
    "100.691661,13.918492;100.691657,13.918493",
    "100.693875,13.734274;100.693875,13.73428",
    "100.693875,13.73428;100.693875,13.734274",
)


def positive_float(value: str) -> float:
    parsed = float(value)
    if not math.isfinite(parsed) or parsed <= 0.0:
        raise argparse.ArgumentTypeError(
            "value must be finite and positive"
        )
    return parsed


def fetch_json(url: str, timeout: float) -> dict:
    with urllib.request.urlopen(url, timeout=timeout) as response:
        data = json.load(response)
    if not isinstance(data, dict) or data.get("code") != "Ok":
        raise ValueError("OSRM response must contain code=Ok")
    return data


def require_non_negative(value, label: str) -> float:
    if (
        isinstance(value, bool) or
        not isinstance(value, (int, float)) or
        not math.isfinite(value) or
        value < 0.0
    ):
        raise ValueError(f"{label} must be finite and >= 0; got {value!r}")
    return float(value)


def check_matrix(
    data: dict, field: str, sources: tuple, destinations: tuple
) -> list:
    matrix = data.get(field)
    if (
        not isinstance(matrix, list) or
        len(matrix) != len(sources) or
        any(
            not isinstance(row, list) or len(row) != len(destinations)
            for row in matrix
        )
    ):
        raise ValueError(
            f"table.{field} must be a "
            f"{len(sources)}x{len(destinations)} matrix"
        )
    for i, row in enumerate(matrix):
        for j, value in enumerate(row):
            require_non_negative(value, f"table.{field}[{i}][{j}]")
    for i, source in enumerate(sources):
        for j, destination in enumerate(destinations):
            if source == destination and matrix[i][j] != 0.0:
                raise ValueError(f"table.{field}[{i}][{j}] must be zero")
    return matrix


def check_case(
    base_url: str, coordinates: str, timeout: float,
    query: str, sources: tuple, destinations: tuple,
) -> list[str]:
    table = fetch_json(
        f"{base_url}/table/v1/motorcycle/{coordinates}"
        f"?annotations=duration,distance{query}",
        timeout,
    )
    route = fetch_json(
        f"{base_url}/route/v1/motorcycle/{coordinates}"
        "?overview=false&alternatives=false",
        timeout,
    )
    distances = check_matrix(table, "distances", sources, destinations)
    durations = check_matrix(table, "durations", sources, destinations)
    routes = route.get("routes")
    if (
        not isinstance(routes, list) or
        len(routes) != 1 or
        not isinstance(routes[0], dict)
    ):
        raise ValueError("route.routes must contain exactly one route")
    differences = []
    for field, matrix in (
        ("distance", distances),
        ("duration", durations),
    ):
        route_value = require_non_negative(
            routes[0].get(field),
            f"route.{field}",
        )
        table_value = matrix[sources.index(0)][destinations.index(1)]
        if not math.isclose(
            table_value,
            route_value,
            rel_tol=0.0,
            abs_tol=1e-9,
        ):
            differences.append(
                f"table.{field}={table_value} "
                f"route.{field}={route_value} "
                f"delta={table_value - route_value:+.6g}"
            )
        if coordinates.split(";")[0] == coordinates.split(";")[1]:
            if route_value != 0.0:
                raise ValueError(f"same-point {field} must be zero")

    return differences


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check known OSRM table-distance regressions."
    )
    parser.add_argument(
        "--base-url",
        default="http://127.0.0.1:5000",
        help="OSRM server URL; defaults to http://127.0.0.1:5000",
    )
    parser.add_argument("--timeout", type=positive_float, default=10.0)
    args = parser.parse_args()
    base_url = args.base_url.rstrip("/")
    cases = [
        (
            "same_point",
            "100.305168,13.654952;100.305168,13.654952",
        )
    ] + [
        (f"nearby_{index:02d}", coordinates)
        for index, coordinates in enumerate(REGRESSION_COORDINATES, 1)
    ]
    variants = (
        ("full", "", (0, 1), (0, 1)),
        ("sources", "&sources=0", (0,), (0, 1)),
        ("destinations", "&destinations=1", (0, 1), (1,)),
        ("pair", "&sources=0&destinations=1", (0,), (1,)),
    )
    cases = [
        (f"{name}/{variant}", coordinates, query, sources, destinations)
        for name, coordinates in cases
        for variant, query, sources, destinations in variants
    ]
    differences_count = 0
    failures = 0
    errors = 0
    for name, coordinates, query, sources, destinations in cases:
        try:
            differences = check_case(
                base_url, coordinates, args.timeout,
                query, sources, destinations,
            )
        except ValueError as error:
            failures += 1
            print(f"FAIL {name}: {coordinates}: {error}", flush=True)
        except (urllib.error.URLError, OSError) as error:
            errors += 1
            print(f"ERROR {name}: {error}", flush=True)
        else:
            print(f"PASS {name}", flush=True)
            if differences:
                differences_count += 1
                print(
                    f"DIFF {name}: " + "; ".join(differences),
                    flush=True,
                )
    print(
        f"Cases={len(cases)} "
        f"passed={len(cases) - failures - errors} "
        f"failed={failures} errors={errors} "
        f"route_differences={differences_count}"
    )
    return 2 if errors else 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
