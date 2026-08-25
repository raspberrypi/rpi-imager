#!/usr/bin/env bash
#
# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2025 Raspberry Pi Ltd
#
# Emit a 128-byte EDID 1.4 block on stdout with a chosen preferred timing and
# physical image size, for the embedded display-scaling matrix.
#
# Blocks are header- and checksum-correct unless a flag says otherwise, so the
# same fixtures can be handed to a kernel via drm.edid_firmware= if we ever
# want to replay a case inside a VM rather than over a bind mount.
#
# Usage: mkedid.sh --px WxH [--mm WxH] [--cm WxH] [--no-dtd-size]
#                  [--monitor-descriptor] [--extensions N] [--bad-header]
#                  [--truncate N]
#
#   --px WxH              preferred timing resolution (required)
#   --mm WxH              image size in mm, written to the first detailed
#                         timing descriptor -- the source our parser prefers
#   --cm WxH              max image size in cm, written to bytes 0x15/0x16 --
#                         the coarse fallback. Defaults to --mm divided by 10.
#   --no-dtd-size         zero the mm fields in the timing descriptor, so only
#                         the centimetre bytes carry a size
#   --monitor-descriptor  make descriptor 1 a monitor descriptor (zero pixel
#                         clock) rather than a timing descriptor
#   --extensions N        append N CTA-861 extension blocks, as every real 4K
#                         TV does, and declare them in byte 126
#   --bad-header          corrupt the fixed 8-byte EDID header
#   --truncate N          emit only the first N bytes
set -euo pipefail

px_w=0; px_h=0
mm_w=0; mm_h=0
cm_w=-1; cm_h=-1
dtd_size=1
descriptor=timing
bad_header=0
extensions=0
length=-1

die() { printf 'mkedid.sh: %s\n' "$1" >&2; exit 2; }
split() { # split WxH into the two named vars
    local value=$1
    [[ $value == *x* ]] || die "expected WxH, got '$value'"
    printf '%s %s' "${value%%x*}" "${value##*x}"
}

while [ $# -gt 0 ]; do
    case $1 in
        --px)   read -r px_w px_h <<<"$(split "$2")"; shift 2 ;;
        --mm)   read -r mm_w mm_h <<<"$(split "$2")"; shift 2 ;;
        --cm)   read -r cm_w cm_h <<<"$(split "$2")"; shift 2 ;;
        --no-dtd-size)        dtd_size=0; shift ;;
        --monitor-descriptor) descriptor=monitor; shift ;;
        --extensions)         extensions=$2; shift 2 ;;
        --bad-header)         bad_header=1; shift ;;
        --truncate)           length=$2; shift 2 ;;
        *) die "unknown argument '$1'" ;;
    esac
done

if [ "$px_w" -le 0 ] || [ "$px_h" -le 0 ]; then die "--px WxH is required"; fi
if [ "$px_w" -gt 4095 ] || [ "$px_h" -gt 4095 ]; then die "--px exceeds the 12 bits EDID gives a timing"; fi
if [ "$mm_w" -gt 4095 ] || [ "$mm_h" -gt 4095 ]; then die "--mm exceeds the 12 bits EDID gives an image size"; fi

# Centimetre bytes default to the millimetre size, which is what a real panel
# reports: the same dimensions at coarser granularity.
[ "$cm_w" -ge 0 ] || cm_w=$((mm_w / 10))
[ "$cm_h" -ge 0 ] || cm_h=$((mm_h / 10))
if [ "$cm_w" -gt 255 ] || [ "$cm_h" -gt 255 ]; then die "--cm exceeds one byte"; fi
if [ "$extensions" -lt 0 ] || [ "$extensions" -gt 254 ]; then die "--extensions out of range"; fi
[ "$length" -ge 0 ] || length=$(( 128 * (1 + extensions) ))

declare -a e
for ((i = 0; i < 128; i++)); do e[i]=0; done

# Fixed header.
for ((i = 1; i < 7; i++)); do e[i]=255; done

# Vendor block: manufacturer "RPI", arbitrary product code and serial, made in
# week 1 of 2025. None of this is read by the code under test; it is here so
# the blocks pass as real EDID to anything else that parses them.
e[8]=$((0x4A)); e[9]=$((0x09))
e[10]=1; e[11]=0
e[16]=1
e[17]=35

e[18]=1                 # EDID version 1
e[19]=4                 # revision 4
e[20]=$((0x80))         # digital input
e[21]=$cm_w             # max horizontal image size, cm
e[22]=$cm_h             # max vertical image size, cm
e[23]=120               # gamma 2.2
e[24]=$((0x0A))         # preferred timing mode in descriptor 1
e[126]=$extensions      # extension block count

# Eight unused standard timing descriptors.
for ((i = 38; i < 54; i += 2)); do e[i]=1; e[i + 1]=1; done

dtd=54
if [ "$descriptor" = timing ]; then
    # Pixel clock in 10 kHz units for a nominal 60 Hz refresh, clamped to the
    # two bytes the field has. Only its being non-zero matters to the parser.
    clock=$(( (px_w + 160) * (px_h + 50) * 60 / 10000 ))
    [ "$clock" -gt 65535 ] && clock=65535
    [ "$clock" -gt 0 ] || clock=1
    e[dtd]=$((clock & 0xFF))
    e[dtd + 1]=$((clock >> 8))

    h_blank=160; v_blank=50
    e[dtd + 2]=$((px_w & 0xFF))
    e[dtd + 3]=$((h_blank & 0xFF))
    e[dtd + 4]=$(( ((px_w >> 8) << 4) | (h_blank >> 8) ))
    e[dtd + 5]=$((px_h & 0xFF))
    e[dtd + 6]=$((v_blank & 0xFF))
    e[dtd + 7]=$(( ((px_h >> 8) << 4) | (v_blank >> 8) ))
    e[dtd + 8]=48       # hsync offset
    e[dtd + 9]=32       # hsync pulse width
    e[dtd + 10]=$((0x30))
    e[dtd + 11]=0

    if [ "$dtd_size" = 1 ]; then
        e[dtd + 12]=$((mm_w & 0xFF))
        e[dtd + 13]=$((mm_h & 0xFF))
        e[dtd + 14]=$(( (((mm_w >> 8) & 0x0F) << 4) | ((mm_h >> 8) & 0x0F) ))
    fi
    e[dtd + 17]=$((0x1E))   # digital separate sync, positive
else
    # Monitor descriptor: zero pixel clock, tag 0xFC (display name).
    e[dtd + 3]=$((0xFC))
    name="TEST PANEL"
    for ((i = 0; i < ${#name}; i++)); do
        printf -v byte '%d' "'${name:i:1}"
        e[dtd + 5 + i]=$byte
    done
    e[dtd + 5 + ${#name}]=$((0x0A))
fi

if [ "$bad_header" = 1 ]; then e[0]=1; fi

# Byte 127 makes the block sum to zero modulo 256.
sum=0
for ((i = 0; i < 127; i++)); do sum=$(( (sum + e[i]) % 256 )); done
e[127]=$(( (256 - sum) % 256 ))

emit=()
for ((i = 0; i < 128; i++)); do emit+=("${e[i]}"); done

# CTA-861 extension: the block a real television carries after the base one.
# Nothing in the code under test reads it; it is here so a 4K TV fixture is
# the length and shape of a 4K TV's actual EDID.
for ((n = 0; n < extensions; n++)); do
    declare -a x
    for ((i = 0; i < 128; i++)); do x[i]=0; done
    x[0]=2              # CTA-861 extension tag
    x[1]=3              # revision 3
    x[2]=4              # detailed timings start at byte 4 (no data blocks)
    sum=0
    for ((i = 0; i < 127; i++)); do sum=$(( (sum + x[i]) % 256 )); done
    x[127]=$(( (256 - sum) % 256 ))
    for ((i = 0; i < 128; i++)); do emit+=("${x[i]}"); done
done

for ((i = 0; i < length && i < ${#emit[@]}; i++)); do
    # shellcheck disable=SC2059 # emitting a byte by its octal value
    printf "\\$(printf '%03o' "${emit[i]}")"
done
