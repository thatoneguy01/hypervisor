#!/usr/bin/env python3

import struct
import sys


def write_string(stream, value):
    encoded = value.encode("utf-8")
    stream.write(struct.pack("<Q", len(encoded)))
    stream.write(encoded)


def write_operand(stream, operand_type, value=0):
    stream.write(struct.pack("<I", operand_type))
    if operand_type == 1:
        stream.write(struct.pack("<I", value))
    elif operand_type == 2:
        stream.write(struct.pack("<i", value))
    elif operand_type == 3:
        write_string(stream, value)


def write_instruction(stream, opcode, operands, address, source_line):
    stream.write(struct.pack("<II IQ", opcode, len(operands), address, source_line))
    for operand in operands:
        write_operand(stream, *operand)
    for _ in range(len(operands), 3):
        write_operand(stream, 0)


def main(path):
    with open(path, "wb") as stream:
        stream.write(b"HVSNAP01")
        stream.write(struct.pack("<IIQ", 1, 7, 1))
        write_string(stream, "./test/test_snapshot.asm")
        registers = [0] * 32
        registers[1] = 42
        stream.write(struct.pack("<32I", *registers))
        stream.write(struct.pack("<IIIQIIQ", 0, 0, 0x00400004, 1, 0,
                                 0x00400000, 2))
        write_instruction(stream, 3, [], 0x00400000, 1)
        write_instruction(stream, 4, [], 0x00400004, 2)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: create_snapshot.py <output>")
    main(sys.argv[1])
