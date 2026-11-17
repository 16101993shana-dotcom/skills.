#!/usr/bin/env python3
"""Turns the data of Config/DefaultGame.ini (building definitions and the
civilization layer) into C++ that fills the plain-data structures, so that the
logic tests run on the project's real data without the engine.

It also checks the text: an unknown field name, which Unreal would skip with
only a warning in the log, stops the build here.

Usage: ini_to_cpp.py <DefaultGame.ini> <output .inc>
"""
import re
import sys

BUILDING_FIELDS = {
    "DefId": "name", "Zone": "enum:EChronoZoneType", "WidthCells": "int", "DepthCells": "int",
    "FrontSetbackCm": "float", "SideMarginCm": "float", "RearMarginCm": "float",
    "MinFloors": "int", "MaxFloors": "int", "FloorHeightCm": "float", "Roof": "enum:EChronoRoofType",
    "RoofHeightCm": "float", "MinLandValue": "float", "Weight": "float", "Service": "enum:EChronoServiceType",
    "ServiceRadiusCm": "float", "ConstructionCost": "float", "UpkeepPerDay": "float",
    "Civilization": "name", "Replaces": "name", "MinEra": "int", "MaxEra": "int", "RequiredUnlock": "name",
}
ERA_FIELDS = {"Id": "name", "DisplayName": "text", "MinPopulation": "int", "TechsToAdvance": "int", "CivicsToAdvance": "int"}
TECH_FIELDS = {
    "Id": "name", "DisplayName": "text", "bCivic": "bool", "Era": "int", "Cost": "float",
    "Prerequisites": "names", "Unlocks": "names", "BoostPopulation": "int", "BoostBuilding": "name", "BoostBuildingCount": "int",
}
CIV_FIELDS = {
    "Id": "name", "DisplayName": "text", "Summary": "text", "ScienceMultiplier": "float", "CultureMultiplier": "float",
    "ConstructionCostMultiplier": "float", "WaterRadiusMultiplier": "float", "StartingUnlocks": "names",
}
ARRAYS = {
    ("/Script/ChronoBuildings.ChronoBuildingSettings", "Definitions"): ("FChronoBuildingDefData", BUILDING_FIELDS, "Buildings"),
    ("/Script/ChronoCiv.ChronoCivSettings", "Eras"): ("FChronoEraDef", ERA_FIELDS, "Civ.Eras"),
    ("/Script/ChronoCiv.ChronoCivSettings", "Techs"): ("FChronoTechDef", TECH_FIELDS, "Civ.Techs"),
    ("/Script/ChronoCiv.ChronoCivSettings", "Civilizations"): ("FChronoCivilizationDef", CIV_FIELDS, "Civ.Civilizations"),
}
SCALARS = {
    ("/Script/ChronoCiv.ChronoCivSettings", "BoostFraction"): "Civ.BoostFraction",
    ("/Script/ChronoCiv.ChronoCivSettings", "HeritageCulturePerDay"): "Civ.HeritageCulturePerDay",
}


def split_top(body):
    """Splits a struct body on the commas that are not inside quotes or parentheses."""
    parts, depth, quoted, current = [], 0, False, ""
    for ch in body:
        if ch == '"':
            quoted = not quoted
        elif not quoted and ch == "(":
            depth += 1
        elif not quoted and ch == ")":
            depth -= 1
        if ch == "," and depth == 0 and not quoted:
            parts.append(current)
            current = ""
        else:
            current += ch
    if current:
        parts.append(current)
    return parts


def cstr(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def convert(kind, value, where):
    value = value.strip()
    if kind == "name":
        return "FName(%s)" % cstr(value.strip('"'))
    if kind == "text":
        m = re.fullmatch(r'NSLOCTEXT\("[^"]*",\s*"[^"]*",\s*"(.*)"\)', value) or re.fullmatch(r'"(.*)"', value)
        if not m:
            sys.exit("%s: text must be NSLOCTEXT(...) or a quoted string: %s" % (where, value))
        return "FText{FString(%s)}" % cstr(m.group(1))
    if kind == "int":
        return str(int(value))
    if kind == "float":
        return repr(float(value)) + "f"
    if kind == "bool":
        if value not in ("True", "False"):
            sys.exit("%s: expected True or False, got %s" % (where, value))
        return "true" if value == "True" else "false"
    if kind == "names":
        if not (value.startswith("(") and value.endswith(")")):
            sys.exit("%s: expected a list in parentheses: %s" % (where, value))
        items = [x.strip().strip('"') for x in split_top(value[1:-1]) if x.strip()]
        return "{" + ", ".join("FName(%s)" % cstr(x) for x in items) + "}"
    if kind.startswith("enum:"):
        if not re.fullmatch(r"[A-Za-z]+", value):
            sys.exit("%s: bad enum value %s" % (where, value))
        return "%s::%s" % (kind[5:], value)
    sys.exit("unknown kind " + kind)


def main():
    ini_path, out_path = sys.argv[1], sys.argv[2]
    section = None
    lines = ["// Generated from %s by ini_to_cpp.py. Do not edit." % ini_path.split("/")[-1],
             "inline void ChronoLoadIniData(FChronoCivData& Civ, TArray<FChronoBuildingDefData>& Buildings)", "{"]
    for number, raw in enumerate(open(ini_path, encoding="utf-8"), 1):
        line = raw.strip()
        where = "line %d" % number
        if not line or line.startswith(";"):
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
            continue
        key, _, value = line.partition("=")
        if key.startswith("+") and (section, key[1:]) in ARRAYS:
            struct, fields, target = ARRAYS[(section, key[1:])]
            if not (value.startswith("(") and value.endswith(")")):
                sys.exit("%s: expected (...)" % where)
            lines.append("\t{")
            lines.append("\t\t%s Item;" % struct)
            for part in split_top(value[1:-1]):
                name, _, field_value = part.partition("=")
                name = name.strip()
                if name not in fields:
                    sys.exit("%s: unknown field %s in %s" % (where, name, struct))
                lines.append("\t\tItem.%s = %s;" % (name, convert(fields[name], field_value, where)))
            lines.append("\t\t%s.Add(Item);" % target)
            lines.append("\t}")
        elif (section, key) in SCALARS:
            lines.append("\t%s = %sf;" % (SCALARS[(section, key)], repr(float(value))))
    lines.append("}")
    open(out_path, "w").write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
