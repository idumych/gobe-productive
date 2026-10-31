#!/usr/bin/env python3
"""Makes the dictionary and thesaurus files Writing Tools' Dictionary… and
Thesaurus… read, from WordNet 3.0 (https://wordnet.princeton.edu, its
"WNdb-3.0" database files: data.noun, data.verb, data.adj, data.adv).

Gobe Productive read BeOS's own files (/boot/optional/goodies/dictionary
and thesaurus), which aren't available; these are in the same formats:

- dictionary (TDictionary, DictIndex): entries "@headword", then the
  entry's text up to the next '@'; sorted by headword ignoring case (the
  index records where each two-letter start begins). Text between two
  backslashes is shown in another font: here the part of speech.
- thesaurus (TThesaurus::Lookup): a line per word, "word:" then the
  words that mean the same, separated by ',', each part of speech
  starting with "-noun-" (and so on); sorted by strcmp (a binary search),
  each line under 1023 bytes (fgets).

usage: tools/wordnet_to_gobe.py <WordNet dict folder> <output folder>
Writes dictionary, thesaurus and LICENSE-WordNet (WordNet's licence,
which must go with the files).
"""
import collections
import os
import re
import sys

PARTS = [("noun", "noun"), ("verb", "verb"), ("adj", "adjective"), ("adv", "adverb")]
MAX_LINE = 1000


def synsets(folder, part):
    """(words, gloss) for each synset of a part of speech."""
    with open(os.path.join(folder, "data." + part), encoding="latin-1") as f:
        for line in f:
            if line.startswith("  "):
                continue		# the licence header
            data, _, gloss = line.partition("|")
            fields = data.split()
            count = int(fields[3], 16)
            words = []
            for i in range(count):
                word = fields[4 + 2 * i]
                # adjectives' markers "(a)", "(p)", "(ip)"
                word = re.sub(r"\((a|p|ip)\)$", "", word)
                words.append(word.replace("_", " "))
            yield words, gloss.strip()


def licence(folder):
    with open(os.path.join(folder, "data.noun"), encoding="latin-1") as f:
        lines = []
        for line in f:
            if not line.startswith("  "):
                break
            # "  <n> " before each line of it
            lines.append(re.sub(r"^\s*\d+ ?", "", line).rstrip())
    return "\n".join(lines).strip() + "\n"


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    folder, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    meanings = collections.defaultdict(lambda: collections.defaultdict(list))	# word -> part -> glosses
    similar = collections.defaultdict(lambda: collections.defaultdict(list))	# word -> part -> words
    for part, name in PARTS:
        for words, gloss in synsets(folder, part):
            for word in words:
                if gloss not in meanings[word.lower()][name]:
                    meanings[word.lower()][name].append(gloss)
                for other in words:
                    if other.lower() != word.lower() and other not in similar[word][name]:
                        similar[word][name].append(other)

    # the dictionary: words starting with a letter, sorted ignoring case
    with open(os.path.join(out, "dictionary"), "w", encoding="latin-1", errors="replace") as f:
        for word in sorted((w for w in meanings if w[:1].isalpha()), key=lambda w: (w.lower(), w)):
            f.write("@" + word + "\n")
            for _, name in PARTS:
                glosses = meanings[word].get(name)
                if not glosses:
                    continue
                f.write("\\" + name + "\\\n")
                for n, gloss in enumerate(glosses, 1):
                    f.write(f"  {n}. {gloss.replace('@', 'at')}\n")
            f.write("\n")

    # the thesaurus: a line per word with others, sorted by strcmp
    with open(os.path.join(out, "thesaurus"), "w", encoding="latin-1", errors="replace") as f:
        for word in sorted(similar, key=lambda w: w.encode("latin-1", "replace")):
            line = word + ":"
            for _, name in PARTS:
                others = [o for o in similar[word].get(name, []) if "," not in o and ":" not in o]
                if not others:
                    continue
                part = "-" + name + "-"
                for other in others:
                    if len(line) + len(part) + len(other) + 2 > MAX_LINE:
                        break
                    line += part + "," + other + "," if part else other + ","
                    part = ""
            if line.endswith(","):
                f.write(line[:-1] + "\n")

    with open(os.path.join(out, "LICENSE-WordNet"), "w") as f:
        f.write("The dictionary and thesaurus files are made from WordNet 3.0.\n\n")
        f.write(licence(folder))
    print("dictionary", os.path.getsize(os.path.join(out, "dictionary")),
        "thesaurus", os.path.getsize(os.path.join(out, "thesaurus")))


main()
