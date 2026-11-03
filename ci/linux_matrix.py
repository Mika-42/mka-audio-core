#!/usr/bin/env python3
"""All 16 independent Linux backend switch combinations, for CI and local use."""
import argparse
import itertools
import json


def matrix():
    backends = ('alsa', 'jack', 'pipewire', 'pulseaudio')
    entries = []
    for enabled in itertools.product((False, True), repeat=len(backends)):
        entry = dict(zip(backends, ('ON' if value else 'OFF' for value in enabled)))
        entry['name'] = '-'.join(name for name, value in zip(backends, enabled) if value) or 'none'
        entry['native'] = any(enabled)
        entries.append(entry)
    return {'include': entries}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--github-output', action='store_true')
    args = parser.parse_args()
    print(('matrix=' if args.github_output else '') + json.dumps(matrix(), separators=(',', ':')))
