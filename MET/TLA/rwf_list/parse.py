from typing import Iterable, List


def read(fp: str, n: int) -> Iterable[List[str]]:
    i = 0
    lines = []  # a buffer to cache lines
    with open(fp, "r") as f:
        for line in f:
            i += 1
            lines.append(line.strip())  # append a line
            if i >= n:
                yield lines
                # reset buffer
                i = 0
                lines.clear()
    # remaining lines
    if i > 0:
        yield lines


def parse_history(h: str) -> str:
    h = h[4:-4].replace(' ', '').replace('\"', '')
    h = h.replace('>>,<<', ' ; ').replace(',', ' ')
    return h


def parse_oracle(o: str) -> str:
    if o == '<< >>':
        return 'n'
    o = o[4:-6].replace(' ', '').replace('[p_ini|->', '')
    o = o.replace('v|->', '').replace('\"', '')
    rtn = ""
    for e in o.split('>>>>,<<'):
        tmp_rh = e.split('],<<')
        tmp_acq = tmp_rh[0].split('lt|->')
        rtn += tmp_acq[0].replace(',', ' ')
        rtn += tmp_acq[1].replace('<<', '').replace('>>', '') + ' '
        rtn += tmp_rh[1]
        rtn += ' ; '
    return rtn[:-3]


N = 2
ops = 2

with open(f"rwf_list_{N}_{ops}.script", "w") as file:
    file.write(str(N) + '\n')
    for lines in read("result.txt", 2):
        file.write(parse_history(lines[0]) + '\n')
        file.write(parse_oracle(lines[1]) + '\n')
