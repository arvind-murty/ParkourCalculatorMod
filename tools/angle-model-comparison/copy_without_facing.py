import argparse
import hashlib
import json
from pathlib import Path


def copy_without_facing(source, output):
    if source.resolve() == output.resolve():
        raise ValueError("The experiment must use a separate copy")
    original = source.read_bytes()
    document = json.loads(original)
    removed = []
    for row in document.get("angleSolver", {}).get("ticks", []):
        constraints = row.get("constraints", [])
        removed.extend({"tick": row["tick"], "constraint": constraint}
                       for constraint in constraints if constraint.get("field") in ("F", "DF"))
        if "constraints" in row:
            row["constraints"] = [constraint for constraint in constraints
                                  if constraint.get("field") not in ("F", "DF")]
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x") as stream:
        stream.write(json.dumps(document, indent=2) + "\n")
    return {"source": str(source), "copy": str(output),
            "sourceSha256": hashlib.sha256(original).hexdigest(),
            "copySha256": hashlib.sha256(output.read_bytes()).hexdigest(),
            "removedFacingConstraints": removed}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    arguments = parser.parse_args()
    manifest = copy_without_facing(arguments.source, arguments.output)
    arguments.manifest.write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))
