#!/usr/bin/env python3
# AP_FLAKE8_CLEAN
'''
Check every AP_Param group table in the tree for invalid group indices.

Why this exists: AP_Param panics during boot ("Bad parameter table") if a group
index is >= 64, or if two entries in the same table use the same index. SITL
cannot catch a duplicate that only exists inside an #if branch which SITL
compiles out - that is how a duplicate ParametersG2 index 54 shipped with
AP_WaterSampler and put a flight controller into a boot loop.

This checker works on the source text, so it sees EVERY #if branch, which makes
it a superset of any single board configuration.

Macro forms handled (the last one is what regex-style checks tend to miss):
  AP_GROUPINFO(name, idx, ...)              -> idx is arg 1
  AP_GROUPINFO_FLAGS / _FRAME / ...         -> idx is arg 1
  AP_SUBGROUPINFO(element, name, idx, ...)  -> idx is arg 2
  AP_SUBGROUPPTR(element, name, idx, ...)   -> idx is arg 2

AP_Param treats index 0 as the last index when looking for duplicates, so that
is applied here as well.

It also rebuilds the full name of every parameter by following the subgroup
macros from the root tables, and reports any name longer than AP_MAX_NAME_SIZE.
AP_Param panics at boot ("Bad parameter table") on an over-long name, and the
limit is measured on the assembled name, so WS_ plus VALVE_ plus ACTIVE_LOW is
three characters over even though each piece looks reasonable on its own.

Usage:
    python3 Tools/scripts/check_param_groupidx.py [dir ...]

Exits non-zero if anything is wrong, so it can be used from CI.
'''

import os
import re
import sys

MACRO_RE = re.compile(r'\b(AP_GROUPINFO[A-Z_]*|AP_SUBGROUPINFO|AP_SUBGROUPPTR)\s*\(')
TABLE_RE = re.compile(
    r'const\s+AP_Param::(?:GroupInfo|Info)\s+'
    r'(\w+)\s*::\s*var_info\s*\[\]\s*=\s*\{')

MAX_INDEX = 64      # AP_Param::_group_level_shift == 6, so index must be < 64
ZERO_AS_LAST = 63   # AP_Param treats idx 0 as the last index for duplicate detection
MAX_NAME = 16       # AP_MAX_NAME_SIZE: a longer assembled name panics at boot

NAME_RE = re.compile(r'^"([^"]*)"$')


def balanced_end(text, open_index):
    '''return the index of the ')' matching the '(' at open_index, or -1'''
    depth = 0
    for i in range(open_index, len(text)):
        if text[i] == '(':
            depth += 1
        elif text[i] == ')':
            depth -= 1
            if depth == 0:
                return i
    return -1


def split_args(text):
    '''split a macro argument list on top-level commas'''
    parts = []
    current = ''
    depth = 0
    for char in text:
        if char in '([{':
            depth += 1
        elif char in ')]}':
            depth -= 1
        if char == ',' and depth == 0:
            parts.append(current.strip())
            current = ''
        else:
            current += char
    parts.append(current.strip())
    return parts


def find_table_body(text, match):
    '''return the text of the braced table body that follows match'''
    start = match.end() - 1
    depth = 0
    index = start
    while index < len(text):
        if text[index] == '{':
            depth += 1
        elif text[index] == '}':
            depth -= 1
            if depth == 0:
                break
        index += 1
    return text[start:index + 1]


def check_file(path):
    '''return (problems, listings, names) for one source file'''
    with open(path, encoding='utf-8', errors='replace') as f:
        src = f.read()

    problems = []
    listings = {}
    names = {}

    for match in TABLE_RE.finditer(src):
        table = match.group(1)
        body = find_table_body(src, match)
        table_line = src[:match.start()].count('\n') + 1

        rows = []
        leaves = []
        children = []
        for token in MACRO_RE.finditer(body):
            open_index = body.index('(', token.end() - 1)
            close_index = balanced_end(body, open_index)
            if close_index < 0:
                continue
            args = split_args(body[open_index + 1:close_index])
            macro = token.group(1)
            # AP_GROUPINFO* put the index second, the subgroup macros put it third
            arg_index = 1 if macro.startswith('AP_GROUPINFO') else 2
            if len(args) <= arg_index:
                continue
            try:
                index = int(args[arg_index], 0)
            except ValueError:
                continue
            label = args[0] if arg_index == 1 else args[1]
            line = table_line + body[:token.start()].count('\n')
            rows.append((index, label, macro, line))

            # collect names so that the full name of every parameter can be
            # reassembled once the whole tree has been read
            if macro.startswith('AP_GROUPINFO'):
                quoted = NAME_RE.match(args[0])
                if quoted:
                    leaves.append((quoted.group(1), line))
            elif len(args) >= 5:
                # AP_SUBGROUPINFO / AP_SUBGROUPPTR:
                #   (element, "PREFIX_", idx, ParentTable, ChildType)
                quoted = NAME_RE.match(args[1])
                if quoted:
                    children.append((quoted.group(1), args[4].strip(), line))

        listings[table] = rows
        entry = names.setdefault(table, {'leaves': [], 'children': []})
        entry['leaves'].extend((name, path, line) for name, line in leaves)
        entry['children'].extend((prefix, child, path, line)
                                 for prefix, child, line in children)

        # duplicate detection, with 0 counting as the last index
        by_effective = {}
        for index, label, macro, line in rows:
            effective = ZERO_AS_LAST if index == 0 else index
            by_effective.setdefault(effective, []).append((index, label, macro, line))

        for effective, entries in sorted(by_effective.items()):
            if len(entries) < 2:
                continue
            detail = ' | '.join(
                'idx=%d %s %s (line %d)' % (i, m, l, ln) for i, l, m, ln in entries)
            problems.append(
                '%s::%s: DUPLICATE effective index %d: %s' % (path, table, effective, detail))

        for index, label, macro, line in rows:
            if index >= MAX_INDEX:
                problems.append(
                    '%s::%s: index %d >= %d (%s %s) at line %d'
                    % (path, table, index, MAX_INDEX, macro, label, line))

    return problems, listings, names


def check_full_names(names):
    '''rebuild every parameter's full name and report the over-long ones'''
    parents = set()
    orphans = set()
    for entry in names.values():
        for _, child, _, _ in entry['children']:
            parents.add(child)
            if child not in names:
                # the table lives outside the scanned tree, which happens on a
                # partial run. Start from it anyway so as much as possible is
                # still checked, even though its prefix will be incomplete
                orphans.add(child)

    problems = []
    seen = set()

    def walk(table, prefix):
        if (table, prefix) in seen:
            return
        seen.add((table, prefix))
        entry = names.get(table)
        if entry is None:
            return
        for name, path, line in entry['leaves']:
            full = prefix + name
            if len(full) > MAX_NAME:
                problems.append(
                    '%s:%d: parameter name "%s" is %d characters, the limit is %d'
                    % (path, line, full, len(full), MAX_NAME))
        for child_prefix, child, _, _ in entry['children']:
            walk(child, prefix + child_prefix)

    for table in sorted((set(names) - parents) | orphans):
        walk(table, '')

    return problems


def main():
    roots = sys.argv[1:] or ['Rover', 'libraries']

    sources = []
    for root in roots:
        for dirpath, _, names in os.walk(root):
            for name in names:
                if name.endswith('.cpp'):
                    sources.append(os.path.join(dirpath, name))
    sources.sort()

    problem_count = 0
    listings = {}
    all_names = {}
    for path in sources:
        problems, found, names = check_file(path)
        for problem in problems:
            print(problem)
            problem_count += 1
        for table, rows in found.items():
            listings['%s::%s' % (path, table)] = rows
        for table, entry in names.items():
            merged = all_names.setdefault(table, {'leaves': [], 'children': []})
            merged['leaves'].extend(entry['leaves'])
            merged['children'].extend(entry['children'])

    name_problems = check_full_names(all_names)
    for problem in name_problems:
        print(problem)
    problem_count += len(name_problems)

    # print the ParametersG2 table in full - it is the one people add to
    for key in sorted(listings):
        if 'ParametersG2' not in key:
            continue
        rows = listings[key]
        used = sorted({ZERO_AS_LAST if r[0] == 0 else r[0] for r in rows})
        free = [i for i in range(1, MAX_INDEX) if i not in used]
        print('')
        print('[%s] %u entries' % (key, len(rows)))
        for index, label, macro, line in sorted(rows):
            print('  %2d  %-24s %s' % (index, macro, label))
        print('  used: %s' % (used,))
        print('  free: %s' % (free,))

    print('---')
    print('scanned %u .cpp files, %u problem(s)' % (len(sources), problem_count))
    return 1 if problem_count else 0


if __name__ == '__main__':
    sys.exit(main())
