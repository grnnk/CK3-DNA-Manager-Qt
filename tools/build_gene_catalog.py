#!/usr/bin/env python3
"""
Builds resources/genes.json from a local Crusader Kings III installation.

The DNA byte order follows the current game's ruler-designer DNA order
(CK3 1.20.x): three colour genes, the morph genes, the detail/aging genes,
hair type and baldness, then the three accessory genes.  The two legacy
style genes (beards/hairstyles) are kept for Ruler Designer output but are
not part of the Base64 payload.

Usage:
    python tools/build_gene_catalog.py "D:/SteamLibrary/steamapps/common/Crusader Kings III"
"""
from __future__ import annotations

import glob
import json
import os
import re
import sys
from collections import Counter

# Current DNA order (CK3 1.20.x).  Beards/hairstyles are ruler-designer-only.
DNA_ORDER = [
    "hair_color", "skin_color", "eye_color",
    "gene_chin_forward", "gene_chin_height", "gene_chin_width",
    "gene_eye_angle", "gene_eye_depth", "gene_eye_height", "gene_eye_distance",
    "gene_eye_shut", "gene_forehead_angle", "gene_forehead_brow_height",
    "gene_forehead_roundness", "gene_forehead_width", "gene_forehead_height",
    "gene_head_height", "gene_head_width", "gene_head_profile",
    "gene_head_top_height", "gene_head_top_width", "gene_jaw_angle",
    "gene_jaw_forward", "gene_jaw_height", "gene_jaw_width",
    "gene_mouth_corner_depth", "gene_mouth_corner_height", "gene_mouth_forward",
    "gene_mouth_height", "gene_mouth_width", "gene_mouth_upper_lip_size",
    "gene_mouth_lower_lip_size", "gene_mouth_open", "gene_neck_length",
    "gene_neck_width", "gene_bs_cheek_forward", "gene_bs_cheek_height",
    "gene_bs_cheek_width", "gene_bs_ear_angle", "gene_bs_ear_inner_shape",
    "gene_bs_ear_bend", "gene_bs_ear_outward", "gene_bs_ear_size",
    "gene_bs_eye_corner_depth", "gene_bs_eye_fold_shape", "gene_bs_eye_size",
    "gene_bs_eye_upper_lid_size", "gene_bs_forehead_brow_curve",
    "gene_bs_forehead_brow_forward", "gene_bs_forehead_brow_inner_height",
    "gene_bs_forehead_brow_outer_height", "gene_bs_forehead_brow_width",
    "gene_bs_jaw_def", "gene_bs_mouth_lower_lip_def",
    "gene_bs_mouth_lower_lip_full", "gene_bs_mouth_lower_lip_pad",
    "gene_bs_mouth_lower_lip_width", "gene_bs_mouth_philtrum_def",
    "gene_bs_mouth_philtrum_shape", "gene_bs_mouth_philtrum_width",
    "gene_bs_mouth_upper_lip_def", "gene_bs_mouth_upper_lip_full",
    "gene_bs_mouth_upper_lip_profile", "gene_bs_mouth_upper_lip_width",
    "gene_bs_nose_forward", "gene_bs_nose_height", "gene_bs_nose_length",
    "gene_bs_nose_nostril_height", "gene_bs_nose_nostril_width",
    "gene_bs_nose_profile", "gene_bs_nose_ridge_angle", "gene_bs_nose_ridge_width",
    "gene_bs_nose_size", "gene_bs_nose_tip_angle", "gene_bs_nose_tip_forward",
    "gene_bs_nose_tip_width", "face_detail_cheek_def", "face_detail_cheek_fat",
    "face_detail_chin_cleft", "face_detail_chin_def",
    "face_detail_eye_lower_lid_def", "face_detail_eye_socket",
    "face_detail_nasolabial", "face_detail_nose_ridge_def",
    "face_detail_nose_tip_def", "face_detail_temple_def",
    "expression_brow_wrinkles", "expression_eye_wrinkles",
    "expression_forehead_wrinkles", "expression_other", "complexion",
    "gene_height", "gene_bs_body_type", "gene_bs_body_shape", "gene_bs_bust",
    "gene_age", "gene_eyebrows_shape", "gene_eyebrows_fullness",
    "gene_body_hair", "gene_hair_type", "gene_baldness",
    "eye_accessory", "teeth_accessory", "eyelashes_accessory",
    "beards", "hairstyles",
]

CATEGORY_ORDER = ["colors", "face", "eyes", "nose", "mouth", "ears",
                  "head_neck", "body", "hair", "details", "accessories", "ruler"]

# The game's accessory genes (eye_accessory, teeth_accessory, eyelashes_accessory)
# are parsed straight from 02_genes_accessories_misc.txt.  Their "index = N"
# values are the bytes the game stores in the DNA array: 0 is no_eyes, 1 is
# normal_eyes, and so on.  Do not reorder them here.

NAME_OVERRIDES = {
    "gene_age": "Aging",
    "gene_height": "Height",
    "gene_bs_body_type": "Body Type",
    "gene_bs_body_shape": "Body Shape",
    "gene_bs_bust": "Bust",
    "gene_body_hair": "Body Hair",
    "gene_hair_type": "Hair Type",
    "gene_baldness": "Baldness",
    "complexion": "Complexion",
    "eye_accessory": "Eye Accessory",
    "teeth_accessory": "Teeth",
    "eyelashes_accessory": "Eyelashes",
    "beards": "Beard Style",
    "hairstyles": "Hairstyle Group",
    "expression_other": "Other Wrinkles",
    "face_detail_nasolabial": "Nasolabial Detail",
    "face_detail_cheek_fat": "Cheek Fat",
    "face_detail_cheek_def": "Cheek Definition",
    "face_detail_chin_cleft": "Chin Cleft",
    "face_detail_chin_def": "Chin Definition",
    "face_detail_temple_def": "Temple Definition",
    "face_detail_eye_socket": "Eye Socket Detail",
    "face_detail_eye_lower_lid_def": "Lower Eyelid Definition",
    "face_detail_nose_ridge_def": "Nose Ridge Definition",
    "face_detail_nose_tip_def": "Nose Tip Definition",
}

TOKEN_MAP = {
    "def": "Definition", "pos": "Positive", "neg": "Negative",
    "max": "Max", "min": "Min", "bs": "", "lip": "Lip",
    "eyebrows": "Eyebrow", "eyelashes": "Eyelashes",
}


def strip_comments(text: str) -> str:
    out, in_quote = [], False
    i = 0
    while i < len(text):
        ch = text[i]
        if ch == '"':
            in_quote = not in_quote
            out.append(ch)
        elif ch == '#' and not in_quote:
            while i < len(text) and text[i] not in '\r\n':
                i += 1
            continue
        else:
            out.append(ch)
        i += 1
    return ''.join(out)


def tokenize(text: str):
    toks, i = [], 0
    while i < len(text):
        ch = text[i]
        if ch.isspace():
            i += 1
            continue
        if ch == '"':
            j = i + 1
            buf = []
            while j < len(text) and text[j] != '"':
                buf.append(text[j])
                j += 1
            toks.append(('str', ''.join(buf)))
            i = j + 1
            continue
        if ch in '{}=':
            toks.append((ch, ch))
            i += 1
            continue
        j = i
        while j < len(text) and not text[j].isspace() and text[j] not in '{}="':
            j += 1
        toks.append(('word', text[i:j]))
        i = j
    return toks


def direct_index_values(block: str) -> dict[str, int]:
    toks = tokenize(block)
    values = {}
    k = 0
    while k < len(toks):
        typ, val = toks[k]
        if typ == 'word' and k + 2 < len(toks) and toks[k + 1][0] == '=' and toks[k + 2][0] == '{':
            child_key = val
            k += 3
            depth = 1
            sub = []
            while k < len(toks) and depth > 0:
                if toks[k][0] == '{':
                    depth += 1
                elif toks[k][0] == '}':
                    depth -= 1
                if depth > 0:
                    sub.append(toks[k])
                k += 1
            for q in range(len(sub) - 2):
                if sub[q] == ('word', 'index') and sub[q + 1][0] == '=' and sub[q + 2][0] == 'word' and sub[q + 2][1].isdigit():
                    values[child_key] = int(sub[q + 2][1])
                    break
            continue
        k += 1
    return values


def parse_gene_files(gene_dir: str) -> dict:
    known = set(DNA_ORDER)
    text = '\n'.join(strip_comments(open(f, encoding='utf-8-sig', errors='replace').read())
                     for f in sorted(glob.glob(os.path.join(gene_dir, '*.txt'))))
    result = {}
    for match in re.finditer(r'(?m)^\s*([a-zA-Z0-9_]+)\s*=\s*\{', text):
        key = match.group(1)
        if key not in known:
            continue
        start = match.end() - 1
        depth = 0
        for j in range(start, len(text)):
            if text[j] == '{':
                depth += 1
            elif text[j] == '}':
                depth -= 1
                if depth == 0:
                    block = text[start + 1:j]
                    break
        else:
            continue
        group = None
        gm = re.search(r'(?m)^\s*group\s*=\s*([a-zA-Z0-9_]+)', block)
        if gm:
            group = gm.group(1)
        values = direct_index_values(block)
        if values or key in ("eye_accessory", "teeth_accessory", "eyelashes_accessory"):
            result[key] = {"group": group, "values": values}
    return result


def humanize_id(value: str) -> str:
    value = value.replace('__', '_')
    parts = [p for p in value.split('_') if p]
    words = []
    for part in parts:
        low = part.lower()
        if low in TOKEN_MAP:
            mapped = TOKEN_MAP[low]
            if mapped:
                words.append(mapped)
        elif part.isdigit():
            words.append(str(int(part)))
        elif re.fullmatch(r'[0-9]+', part):
            words.append(str(int(part)))
        else:
            words.append(part[:1].upper() + part[1:])
    return ' '.join(words)


def common_prefix(strings: list[str]) -> str:
    if not strings:
        return ''
    prefix = strings[0]
    for s in strings[1:]:
        while not s.startswith(prefix):
            prefix = prefix[:-1]
            if not prefix:
                return ''
    # Never cut a word in half (normal_teeth / no_teeth -> "no").
    if not prefix.endswith('_'):
        prefix = prefix[:prefix.rfind('_') + 1]
    return prefix


def gene_display_name(key: str) -> str:
    if key in NAME_OVERRIDES:
        return NAME_OVERRIDES[key]
    name = key
    if name.startswith('gene_bs_'):
        name = name[8:]
    elif name.startswith('gene_'):
        name = name[5:]
    if name.startswith('face_detail_'):
        name = name[12:]
    elif name.startswith('expression_'):
        name = name[11:]
    return humanize_id(name)


OPTION_LABEL_OVERRIDES = {
    "teeth_accessory": {"normal_teeth": "Normal Teeth", "no_teeth": "No Teeth"},
    "eye_accessory": {
        "normal_eyes": "Normal Eyes",
        "normal_eyes_no_shadow": "Normal Eyes (No Shadow)",
        "normal_eyes_dark_iris": "Normal Eyes (Dark Iris)",
        "normal_eyes_asian": "Normal Eyes (Asian)",
        "bloodshot_eyes": "Bloodshot Eyes",
        "blind_eyes": "Blind Eyes",
        "no_eyes": "No Eyes",
    },
    "eyelashes_accessory": {
        "no_eyelashes": "No Eyelashes",
        "normal_eyelashes": "Normal Eyelashes",
        "asian_eyelashes": "Asian Eyelashes",
    },
    "face_detail_chin_def": {"chin_def_neg": "Negative", "chin_def": "Positive"},
    "face_detail_nose_ridge_def": {
        "nose_ridge_def_pos": "Positive", "nose_ridge_def_neg": "Negative",
    },
    "face_detail_cheek_def": {"cheek_def_01": "Type 1", "cheek_def_02": "Type 2"},
    "face_detail_eye_lower_lid_def": {
        "eye_lower_lid_def": "Default", "eye_lower_lid_def_02": "Type 2",
    },
    "face_detail_eye_socket": {
        "eye_socket_01": "Type 1", "eye_socket_02": "Type 2", "eye_socket_03": "Type 3",
        "eye_socket_color_01": "Color 1", "eye_socket_color_02": "Color 2",
        "eye_socket_color_03": "Color 3",
    },
    "face_detail_nasolabial": {
        "nasolabial_01": "Type 1", "nasolabial_02": "Type 2",
        "nasolabial_03": "Type 3", "nasolabial_04": "Type 4",
    },
    "expression_brow_wrinkles": {
        "brow_wrinkles_01": "Type 1", "brow_wrinkles_02": "Type 2",
        "brow_wrinkles_03": "Type 3", "brow_wrinkles_04": "Type 4",
    },
    "expression_eye_wrinkles": {
        "eye_wrinkles_01": "Type 1", "eye_wrinkles_02": "Type 2",
        "eye_wrinkles_03": "Type 3",
    },
    "expression_forehead_wrinkles": {
        "forehead_wrinkles_01": "Type 1", "forehead_wrinkles_02": "Type 2",
        "forehead_wrinkles_03": "Type 3",
    },
    "gene_bs_eye_fold_shape": {
        "eye_fold_shape_neg": "Negative",
        "eye_fold_shape_pos": "Positive",
        "eye_fold_shape_02_neg": "Type 2 Negative",
        "eye_fold_shape_02_pos": "Type 2 Positive",
    },
    "gene_bs_ear_inner_shape": {"ear_inner_shape_pos": "Default"},
    "gene_bs_mouth_lower_lip_def": {"mouth_lower_lip_def_pos": "Default"},
    "gene_bs_mouth_philtrum_def": {"mouth_philtrum_def_pos": "Default"},
    "gene_bs_mouth_upper_lip_def": {"mouth_upper_lip_def_pos": "Default"},
    "face_detail_nose_tip_def": {"nose_tip_def": "Default"},
    "face_detail_temple_def": {"temple_def": "Default"},
}


def option_labels(key: str, options: list[tuple[str, int]]) -> list[dict]:
    if not options:
        return []
    overrides = OPTION_LABEL_OVERRIDES.get(key, {})
    prefix = common_prefix([o[0] for o in options])
    prefix = prefix.rstrip('_')
    labels = []
    for opt_id, index in options:
        short = opt_id
        if prefix and short.startswith(prefix):
            short = short[len(prefix):].lstrip('_')
        if opt_id in overrides:
            label = overrides[opt_id]
        elif len(options) == 1:
            label = "Default"
        elif not short:
            label = humanize_id(opt_id)
        else:
            label = humanize_id(short)
        labels.append({"id": opt_id, "index": index, "label": label})
    return labels


def category_for(key: str, group: str | None) -> str:
    if key in ("hair_color", "skin_color", "eye_color"):
        return "colors"
    if key in ("beards", "hairstyles"):
        return "ruler"
    if key in ("eye_accessory", "teeth_accessory", "eyelashes_accessory"):
        return "accessories"
    if key in ("complexion", "gene_age") or key.startswith("expression_"):
        return "details"
    if group:
        return group
    return "details"


def game_version(game_dir: str) -> str:
    settings = os.path.join(game_dir, 'launcher', 'launcher-settings.json')
    try:
        with open(settings, encoding='utf-8-sig') as handle:
            version = json.load(handle).get('version')
        if version:
            return version
    except (OSError, ValueError):
        pass
    return 'unknown version'


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    game_dir = sys.argv[1]
    gene_dir = os.path.join(game_dir, 'game', 'common', 'genes')
    parsed = parse_gene_files(gene_dir)

    missing = [k for k in DNA_ORDER if k not in parsed and k not in ("hair_color", "skin_color", "eye_color")]
    if missing:
        print("Missing gene definitions:", ", ".join(missing), file=sys.stderr)
        return 1

    genes = []
    base64_index = 0
    for key in DNA_ORDER:
        in_base64 = base64_index < 104
        kind = "color" if key in ("hair_color", "skin_color", "eye_color") else "enum"
        options = []
        if kind == "enum":
            values = parsed[key]["values"]
            options = sorted(values.items(), key=lambda kv: kv[1])
        entry = {
            "key": key,
            "name": gene_display_name(key),
            "category": category_for(key, parsed.get(key, {}).get("group")),
            "kind": kind,
            "inBase64": in_base64,
        }
        if in_base64:
            entry["base64Index"] = base64_index
            base64_index += 1
        entry["options"] = option_labels(key, options)
        genes.append(entry)

    catalog = {
        "version": 1,
        "source": f"Crusader Kings III {game_version(game_dir)}",
        "base64GeneCount": 104,
        "accessorySuffixCount": 3,
        "categoryOrder": CATEGORY_ORDER,
        "genes": genes,
    }
    out = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                       'resources', 'genes.json')
    with open(out, 'w', encoding='utf-8') as f:
        json.dump(catalog, f, indent=2, ensure_ascii=False)
        f.write('\n')
    print(f"Wrote {out}: {len(genes)} genes, {base64_index} in Base64 payload")
    groups = Counter(g["category"] for g in genes)
    print("Categories:", dict(groups))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
