#!/bin/bash
# Compiles the plain-logic code of Chronopolis (terrain, roads, zoning,
# buildings, audit) with g++ against stand-in Unreal headers, and runs its
# automation tests. No Unreal Engine needed.
#
# This checks the logic and the zero-clipping audit. It does NOT prove that
# the project builds in Unreal: actors, subsystems, components and everything
# that uses engine classes is left out, and the stand-in types are not the
# engine's (different random numbers, different noise, sorted maps).
#
# Usage: Build/LogicTests/run.sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../../Source"
OUT="${TMPDIR:-/tmp}/chronopolis-logic-tests"
mkdir -p "$OUT/gen"

# Empty stand-ins for the headers that Unreal Header Tool would generate.
for f in $(grep -rhoE '"[A-Za-z0-9]+\.generated\.h"' "$SRC" | tr -d '"' | sort -u); do : > "$OUT/gen/$f"; done

MODULES="ChronoCore ChronoTerrain ChronoRoads ChronoZoning ChronoBuildings ChronoAudit"
INC="-I$HERE/Shim -I$OUT/gen"
DEF=""
for m in $MODULES; do
  INC="$INC -I$SRC/$m/Public"
  DEF="$DEF -D$(echo "$m" | tr '[:lower:]' '[:upper:]')_API="
done

FILES="
$SRC/ChronoCore/Private/ChronoMeshData.cpp
$SRC/ChronoCore/Private/ChronoGeometry2D.cpp
$SRC/ChronoTerrain/Private/ChronoHeightfield.cpp
$SRC/ChronoRoads/Private/ChronoRoadGeometry.cpp
$SRC/ChronoRoads/Private/ChronoRoadGraph.cpp
$SRC/ChronoRoads/Private/ChronoRoadMeshBuilder.cpp
$SRC/ChronoRoads/Private/ChronoRoadBuilder.cpp
$SRC/ChronoZoning/Private/ChronoZoneGrid.cpp
$SRC/ChronoBuildings/Private/ChronoBuildingSet.cpp
$SRC/ChronoBuildings/Private/ChronoBuildingMeshBuilder.cpp
$SRC/ChronoAudit/Private/ChronoClippingAudit.cpp
$SRC/ChronoTerrain/Private/Tests/ChronoHeightfieldTests.cpp
$SRC/ChronoRoads/Private/Tests/ChronoRoadTests.cpp
$SRC/ChronoZoning/Private/Tests/ChronoZoningTests.cpp
$SRC/ChronoBuildings/Private/Tests/ChronoBuildingTests.cpp
$SRC/ChronoAudit/Private/Tests/ChronoAuditTests.cpp
$HERE/Shim/main.cpp"

g++ -std=c++20 -O1 -Wall -Wno-unused-variable -Wno-unused-but-set-variable $INC $DEF $FILES -o "$OUT/chrono_tests"
"$OUT/chrono_tests"
