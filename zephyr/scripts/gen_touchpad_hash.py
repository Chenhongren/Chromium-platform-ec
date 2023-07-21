# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import hashlib
import os.path
import pathlib
import sys

def print_hash(out, digest: [int], end: str=',') -> None:
    print("{ ", file=out, end='')
    for x in digest:
        print(f"{x:#04x}, ", file=out, end='')
    print(f'}}{end}', file=out)


def gen_header(fw, out, touchpad_virtual_size, update_pdu_size):
    digest_size = hashlib.sha256().digest_size
    num_blocks = touchpad_virtual_size // update_pdu_size

    print("#include <stdint.h>\n", file=out)

    print(f"const uint8_t touchpad_fw_hashes[{num_blocks}][{digest_size}] = {{", file=out)

    for i in range(num_blocks):
        if fw is not None:
            block = fw.read(update_pdu_size)
            print_hash(out, hashlib.sha256(block).digest())
        else:
            print_hash(out, [0] * digest_size)

    print("};", file=out)
    print(f"const uint8_t touchpad_fw_full_hash[{digest_size}] =\n\t", file=out, end='')
    if fw is not None:
        fw.seek(0, 0)
        print_hash(out, hashlib.sha256(fw.read()).digest(), ';')
    else:
        print_hash(out, [0] * digest_size, ';')


def main() -> int:
    parser = argparse.ArgumentParser()

    parser.add_argument('-t', '--touchpad_virtual_size', type=int,
                        required=True,
                        help='Expected size of the touchpad firmware')
    parser.add_argument('-u', '--update_pdu_size', type=int,
                        required=True,
                        help='PDU size for firmware update')
    parser.add_argument('-f', type=pathlib.Path, dest='fw_path',
                        help='Firmware file. Will output blank hashes if not provided.')
    parser.add_argument('-o', type=pathlib.Path, required=True, dest='output_path',
                        help='Output file')

    args = parser.parse_args()


    args.output_path.parent.mkdir(parents=True, exist_ok=True)

    with args.output_path.open("w") as output:
        if args.fw_path:
            fw_size = os.path.getsize(args.fw_path)
            if fw_size != args.touchpad_virtual_size:
                print(f"Incorrect TP FW size ({fw_size} vs {args.touchpad_virtual_size})", file=sys.stderr)
                return 1
            with args.fw_path.open("rb") as fw:
                gen_header(fw, output, args.touchpad_virtual_size, args.update_pdu_size)
        else:
            gen_header(None, output, args.touchpad_virtual_size, args.update_pdu_size)

    return 0


if __name__ == "__main__":
    sys.exit(main())
