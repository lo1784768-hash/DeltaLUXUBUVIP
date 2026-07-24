"""arm64_asm.py - vai bo ma hoa ARM64 toi thieu, chi du dung cho patch_unityframework_syscalls.py.
KHONG phai assembler day du - chi ho tro dung tap lenh can cho trampoline redirect syscall.
Moi ham tra ve bytes (4 byte, little-endian) cho DUNG 1 lenh.
"""
import struct


def _u32(word):
    return struct.pack('<I', word & 0xFFFFFFFF)


def mov_imm(rd, imm16):
    """MOVZ Xd, #imm16 (shift=0)."""
    assert 0 <= imm16 <= 0xFFFF
    word = 0xD2800000 | (imm16 << 5) | rd
    return _u32(word)


def sub_sp_sp_imm(imm12):
    assert 0 <= imm12 < 4096 and imm12 % 1 == 0
    word = 0xD1000000 | (imm12 << 10) | (31 << 5) | 31
    return _u32(word)


def add_sp_sp_imm(imm12):
    assert 0 <= imm12 < 4096
    word = 0x91000000 | (imm12 << 10) | (31 << 5) | 31
    return _u32(word)


def stp_x_sp(rt1, rt2, imm_off):
    """STP Xt1, Xt2, [SP, #imm_off] (signed offset, imm_off phai chia het 8)."""
    assert imm_off % 8 == 0
    imm7 = (imm_off // 8) & 0x7F
    word = 0xA9000000 | (imm7 << 15) | (rt2 << 10) | (31 << 5) | rt1
    return _u32(word)


def ldp_x_sp(rt1, rt2, imm_off):
    assert imm_off % 8 == 0
    imm7 = (imm_off // 8) & 0x7F
    word = 0xA9400000 | (imm7 << 15) | (rt2 << 10) | (31 << 5) | rt1
    return _u32(word)


def str_x_sp(rt, imm_off):
    """STR Xt, [SP, #imm_off] (unsigned offset, imm_off phai chia het 8, 0..32760)."""
    assert imm_off % 8 == 0 and 0 <= imm_off <= 32760
    imm12 = (imm_off // 8) & 0xFFF
    word = 0xF9000000 | (imm12 << 10) | (31 << 5) | rt
    return _u32(word)


def ldr_x_sp(rt, imm_off):
    assert imm_off % 8 == 0 and 0 <= imm_off <= 32760
    imm12 = (imm_off // 8) & 0xFFF
    word = 0xF9400000 | (imm12 << 10) | (31 << 5) | rt
    return _u32(word)


def ldr_x_reg_imm(rt, rn, imm_off):
    """LDR Xt, [Xn, #imm_off] (unsigned offset)."""
    assert imm_off % 8 == 0 and 0 <= imm_off <= 32760
    imm12 = (imm_off // 8) & 0xFFF
    word = 0xF9400000 | (imm12 << 10) | (rn << 5) | rt
    return _u32(word)


def add_reg_imm(rd, rn, imm12):
    assert 0 <= imm12 < 4096
    word = 0x91000000 | (imm12 << 10) | (rn << 5) | rd
    return _u32(word)


def adrp(rd, delta_pages):
    """ADRP Xd, #(delta_pages * 4096) - delta_pages la so trang (4KB) tinh tu trang chua chinh
    lenh nay toi trang dich, co dau (signed 21-bit: +-2^20 trang = +-4GB)."""
    assert -(1 << 20) <= delta_pages < (1 << 20)
    imm = delta_pages & 0x1FFFFF
    immlo = imm & 0x3
    immhi = (imm >> 2) & 0x7FFFF
    word = (1 << 31) | (immlo << 29) | (0x10 << 24) | (immhi << 5) | rd
    return _u32(word)


def br(rn):
    word = 0xD61F0000 | (rn << 5)
    return _u32(word)


def blr(rn):
    word = 0xD63F0000 | (rn << 5)
    return _u32(word)


def cbz_x(rt, imm19_words):
    """CBZ Xt, label - imm19_words la so LENH (khong phai byte) tu lenh nay toi label, co dau."""
    assert -(1 << 18) <= imm19_words < (1 << 18)
    word = 0xB4000000 | ((imm19_words & 0x7FFFF) << 5) | rt
    return _u32(word)


def bl(imm26_words):
    assert -(1 << 25) <= imm26_words < (1 << 25)
    word = 0x94000000 | (imm26_words & 0x3FFFFFF)
    return _u32(word)


def b(imm26_words):
    assert -(1 << 25) <= imm26_words < (1 << 25)
    word = 0x14000000 | (imm26_words & 0x3FFFFFF)
    return _u32(word)


def ret():
    return _u32(0xD65F03C0)


def svc0():
    return _u32(0xD4000001)


if __name__ == '__main__':
    # Tu kiem tra bang capstone - disasm lai xem co dung khong.
    from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)

    tests = [
        mov_imm(16, 0x152),
        sub_sp_sp_imm(48),
        add_sp_sp_imm(48),
        stp_x_sp(1, 2, 0),
        ldp_x_sp(1, 2, 0),
        str_x_sp(16, 16),
        ldr_x_sp(16, 16),
        ldr_x_reg_imm(9, 9, 0),
        add_reg_imm(9, 9, 0x123),
        adrp(9, 12345),
        br(17),
        blr(9),
        cbz_x(9, 5),
        bl(100),
        b(-50),
        ret(),
        svc0(),
    ]
    base = 0x1000
    blob = b''.join(tests)
    for insn in md.disasm(blob, base):
        print(hex(insn.address), insn.mnemonic, insn.op_str)
