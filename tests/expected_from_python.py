#!/usr/bin/env python3
"""Same canonical dump as tests/core_dump.cpp, from tools/gobersrc.py's JSON."""
import json
import sys

d = json.load(open(sys.argv[1]))


def code(c):
    if isinstance(c, int):
        return str(c)
    return c


def items(lst, depth):
    for it in lst:
        print("%s%s index=%d label=[%s]" % ("  " * depth, it["type"], it.get("index", -1),
                                            it.get("label", "")))
        items(it.get("children", []), depth + 1)


def ids(res):
    return sorted(res, key=int)


for k in ids(d.get("MENU", {})):
    m = d["MENU"][k]
    print("MENU %s title=[%s]" % (k, m["title"]))
    for i in m["items"]:
        if i.get("separator"):
            print("  ---")
        else:
            print("  [%s] cmd=%s sub=%d" % (i["label"], code(i.get("command", 0)), i.get("submenu", 0)))
LOOKS = {"B_TITLED_WINDOW_LOOK": 1, "B_MODAL_WINDOW_LOOK": 3, "B_FLOATING_WINDOW_LOOK": 7,
         "B_DOCUMENT_WINDOW_LOOK": 11, "B_NO_BORDER_WINDOW_LOOK": 19, "B_BORDERED_WINDOW_LOOK": 20}
FEELS = {"B_NORMAL_WINDOW_FEEL": 0, "B_MODAL_APP_WINDOW_FEEL": 1, "B_MODAL_SUBSET_WINDOW_FEEL": 2,
         "B_MODAL_ALL_WINDOW_FEEL": 3, "B_FLOATING_SUBSET_WINDOW_FEEL": 4,
         "B_FLOATING_APP_WINDOW_FEEL": 5, "B_FLOATING_ALL_WINDOW_FEEL": 6}
for k in ids(d.get("DIAL", {})):
    x = d["DIAL"][k]
    print("DIAL %s title=[%s] look=%d feel=%d" % (k, x["title"], LOOKS.get(x["look"], x["look"]),
                                                FEELS.get(x["feel"], x["feel"])))
for k in ids(d.get("Item", {})):
    print("Item %s" % k)
    items(d["Item"][k]["items"], 1)
for k in ids(d.get("STR_", {})):
    print("STR# %s count=%d" % (k, len(d["STR_"][k])))
