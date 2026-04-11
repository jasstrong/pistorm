#!/usr/bin/env python3
"""Convert MPW 68k assembly to GAS (GNU Assembler) syntax.

Usage: python3 mpw2gas.py input.a > output.S

Handles the common patterns:
- Register names: D0→%d0, A0→%a0, SP→%sp
- Hex: $1234 → 0x1234
- Comments: ; → //
- Directives: DC.W→.word, DC.L→.long, DS.W→.space, etc.
- Address modes: 4(A0)→4(%a0), (A0)+→(%a0)+, -(A0)→-(%a0)
- Labels: name PROC → name:, ENDPROC → (removed)
- IMPORT/EXPORT → .globl
- EQU → .equ
- String literals: 'text' → "text" (in DC.B context)
"""
import re, sys

def convert_register(m):
    """Convert register names to GAS syntax."""
    reg = m.group(0)
    # Don't convert inside comments or strings
    return '%' + reg.lower()

def convert_hex(m):
    """Convert $hex to 0xhex."""
    return '0x' + m.group(1)

current_func = '_global'

def convert_line(line):
    """Convert a single line of MPW assembly to GAS syntax."""
    global current_func
    # Preserve original for reference
    orig = line.rstrip()

    # Split off comment
    comment = ''
    in_string = False
    for i, c in enumerate(line):
        if c in ("'", '"'):
            in_string = not in_string
        elif c == ';' and not in_string:
            comment = ' //' + line[i+1:].rstrip()
            line = line[:i]
            break

    # Strip trailing whitespace
    line = line.rstrip()
    if not line:
        return comment.lstrip() if comment else ''

    # Split into label, opcode, operands
    # MPW format: [label] [opcode [operands]]
    parts = line.split(None, 1)
    if not parts:
        return comment

    # Check if first token is a label (starts in column 1, no whitespace before)
    has_label = line[0] not in (' ', '\t') if line else False

    if has_label:
        label = parts[0]
        # Convert @local labels — make unique with function name
        if label.startswith('@'):
            label = '.L_' + current_func + '_' + label[1:]
        # Track function name for non-local labels (not starting with .)
        elif not label.startswith('.') and len(label) > 1:
            current_func = label
        rest = parts[1] if len(parts) > 1 else ''
    else:
        label = ''
        rest = line.strip()

    # Parse opcode and operands from rest
    rest_parts = rest.split(None, 1)
    opcode = rest_parts[0].upper() if rest_parts else ''
    operands = rest_parts[1] if len(rest_parts) > 1 else ''

    # === Handle directives ===

    # PROC/ENDPROC
    if opcode == 'PROC':
        current_func = label
        return f'    .globl {label}\n{label}:{comment}'
    if opcode == 'ENDPROC' or opcode == 'ENDP':
        return comment or ''

    # IMPORT/EXPORT
    if opcode == 'IMPORT' or opcode == 'EXTERNAL':
        return f'    .globl {operands.split(",")[0].strip()}{comment}'
    if opcode == 'EXPORT':
        return f'    .globl {operands.split(",")[0].strip()}{comment}'

    # EQU
    if opcode == 'EQU':
        val = convert_operand(operands.strip())
        return f'    .equ {label}, {val}{comment}'

    # RECORD/ENDR (structure definitions — convert to .equ offsets)
    if opcode == 'RECORD':
        return f'// RECORD {label} {operands}{comment}'
    if opcode == 'ENDR':
        return f'// ENDR{comment}'

    # DC (define constant)
    if opcode in ('DC.B', 'DCB.B'):
        return f'    .byte {convert_operand(operands)}{comment}'
    if opcode in ('DC.W', 'DCB.W'):
        return f'    .word {convert_operand(operands)}{comment}'
    if opcode in ('DC.L', 'DCB.L'):
        return f'    .long {convert_operand(operands)}{comment}'

    # DS (define space)
    if opcode == 'DS.B':
        return f'    .space {convert_operand(operands)}{comment}'
    if opcode == 'DS.W':
        val = convert_operand(operands)
        return f'    .space ({val})*2{comment}'
    if opcode == 'DS.L':
        val = convert_operand(operands)
        return f'    .space ({val})*4{comment}'

    # ALIGN
    if opcode == 'ALIGN':
        return f'    .even{comment}'

    # STRING/MACRO/ENDM/IF/ELSE/ENDIF
    if opcode == 'STRING':
        return f'// STRING {operands}{comment}'
    if opcode == 'MACRO':
        return f'.macro {label}{comment}'
    if opcode == 'ENDM':
        return f'.endm{comment}'
    if opcode == 'IF' or opcode == 'ELSEIF':
        return f'// {opcode} {operands}{comment}'
    if opcode == 'ELSE':
        return f'// ELSE{comment}'
    if opcode == 'ENDIF':
        return f'// ENDIF{comment}'

    # INCLUDE
    if opcode == 'INCLUDE':
        return f'// INCLUDE {operands}{comment}'

    # PRINT
    if opcode == 'PRINT':
        return f'// PRINT {operands}{comment}'

    # MACHINE
    if opcode == 'MACHINE':
        return f'// MACHINE {operands}{comment}'

    # TITLE/BLANKS/PAGE/EJECT and other formatting directives
    if opcode in ('TITLE', 'BLANKS', 'PAGE', 'LOAD', 'SEG', 'CASE', 'EJECT'):
        return f'// {opcode} {operands}{comment}'

    # BigJSR/BigLEA macros — convert to JSR/LEA with abs.L
    if opcode == 'BIGJSR':
        parts = operands.split(',')
        target = parts[0].strip()
        return f'    jsr {convert_operand(target)}{comment}'
    if opcode == 'BIGLEA':
        parts = operands.split(',')
        target = parts[0].strip()
        reg = convert_operand(parts[1].strip()) if len(parts) > 1 else '%a0'
        return f'    lea {convert_operand(target)},{reg}{comment}'

    # WITH (MPW record scoping)
    if opcode == 'WITH':
        return f'// WITH {operands}{comment}'
    if opcode == 'ENDWITH':
        return f'// ENDWITH{comment}'

    # === Handle instructions ===

    # Convert the operands
    if operands:
        operands = convert_operand(operands)

    # Convert opcode to lowercase and fix MPW aliases
    gas_opcode = opcode.lower()
    # MPW branch aliases
    gas_opcode = gas_opcode.replace('bz.', 'beq.').replace('bnz.', 'bne.')
    gas_opcode = gas_opcode.replace('bz ', 'beq ').replace('bnz ', 'bne ')
    # Convert .s branches to .w to avoid range issues (assembler can optimize)
    if gas_opcode.endswith('.s') and gas_opcode[:1] == 'b':
        gas_opcode = gas_opcode[:-2] + '.w'

    # Build the output line
    result = ''
    if label:
        if has_label:
            result += f'{label}:\n'

    if gas_opcode:
        result += f'    {gas_opcode}'
        if operands:
            result += f' {operands}'

    result += comment
    return result


def convert_operand(op):
    """Convert MPW operand syntax to GAS syntax."""
    # Convert @local labels — prefix with current function name for uniqueness
    op = re.sub(r'@(\w+)', lambda m: '.L_' + current_func + '_' + m.group(1), op)

    # Convert Record.field(An) to numeric offset(An)
    # For now, strip the Record. prefix — the field should be defined as an equate
    op = re.sub(r'\w+\.(\w+)\(', r'\1(', op)

    # Convert hex $xxxx → 0xxxxx
    op = re.sub(r'\$([0-9A-Fa-f]+)', lambda m: '0x' + m.group(1), op)

    # Convert register names (but not inside strings or after .)
    # Match standalone register names
    op = re.sub(r'\b([DdAa][0-7]|[Ss][Pp]|[Ss][Rr]|CCR|ccr|USP|usp|VBR|vbr|CACR|cacr|SFC|sfc|DFC|dfc)\b',
                lambda m: '%' + m.group(0).lower(), op)

    # Fix address modes: offset(An) → offset(%an) — already handled by register convert
    # But need to handle (An)+ → (%an)+ and -(An) → -(%an)

    # Convert 4-char type constants: 'FONT' → 0x464F4E54
    def fourcc_to_hex(m):
        s = m.group(1)
        if len(s) == 4:
            return '0x' + ''.join(f'{ord(c):02X}' for c in s)
        return m.group(0)
    op = re.sub(r"'([^']{4})'", fourcc_to_hex, op)

    return op


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 mpw2gas.py input.a [> output.S]", file=sys.stderr)
        sys.exit(1)

    with open(sys.argv[1], 'r', encoding='mac_roman', errors='replace') as f:
        lines = f.readlines()

    print('/* Converted from MPW assembly by mpw2gas.py */')
    print('    .text')
    print('    .even')
    print()

    for line in lines:
        converted = convert_line(line)
        if converted is not None:
            print(converted)


if __name__ == '__main__':
    main()
