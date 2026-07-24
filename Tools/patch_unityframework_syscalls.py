"""patch_unityframework_syscalls.py

Va UnityFramework de redirect ~50 diem game tu goi THANG syscall stat/open/access (mov
x16,#N; svc #0 - KHONG qua symbol libSystem, nen fishhook cua Delta.dylib (AssetRedirect.h)
KHONG BAO GIO thay duoc) sang cung 1 co che redirect path dang dung cho cac ham qua thu vien.

BOI CANH: doi chieu byte UnityFramework "und3fined" (ban goc) voi UnityFramework trong
MoniteV2.ipa (Monite.dylib CHAY DUOC, khong bi da) - Monite tu chuan bi rieng 1 ban
UnityFramework co them 2 segment `__HOOK_TEXT`/`__MONITE_TEXT` chua trampoline, roi va
DUNG 50+ diem nay (thay lenh dau cua cap "mov x16,#N; svc#0" bang 1 lenh nhay) de redirect
chung vao he thong hook cua ho. Day la ly do Monite tranh duoc 1 loai kiem tra file ma
Delta chua bao gio cham toi, du da vs MatchClientInfo/mtime/... o phia dylib.

KY THUAT O DAY (an toan hon ban Monite): KHONG them segment/section moi vao Mach-O (da thu
lief add_section/extend_section - ket qua khong chac chan, rui ro that cau truc file that).
Thay vao do, dung dung 2 vung "khoang trong" CO SAN, TOAN SO 0 (padding align trang cuoi
segment, ld64 luon de lai):
  - Cuoi segment __TEXT (sau section that cuoi __eh_frame, truoc bien segment) - CHUA code
    trampoline (thuc thi duoc, segment __TEXT la r-x).
  - Cuoi phan CO THAT NOI DUNG cua segment __DATA (sau __thread_vars, truoc fileoff cua
    __LINKEDIT - phan zerofill __thread_bss/__bss/__common sau do KHONG co du lieu file that)
    - CHUA 1 con tro 8-byte (callback redirect path), duoc chinh Delta.dylib GHI GIA TRI THAT
    vao luc chay (xem DeltaSyscallHook.h ben Source/Includes/).

Ca 2 vung nay van la CUNG 1 binary UnityFramework (khong phai dylib rieng), nen khoang cach
tu cac diem bi va (~0x95000000-0x9c000000) toi vung code trampoline (~0xb64ab44) luon duoi
128MB - lenh nhay "b"/"bl" 1 lenh du tam voi, KHONG can ky thuat ADRP+BR gian tiep nhu khi
tung thu redirect qua Delta.dylib (that bai vi 2 anh rieng biet, ASLR truot doc lap - xem
MatchClientInfoPatch.h's "stub qua xa" fallback).

CHUA KIEM CHUNG TREN THIET BI THAT - cong cu nay tao ra 1 file UnityFramework MOI de thay
the ban trong IPA truoc khi ky, KHONG phai code chay trong Delta.dylib.
"""
import sys
import os
sys.path.insert(0, os.path.dirname(__file__))
import arm64_asm as asm

# ============================================================================
# 50 diem syscall thang da xac nhan: (RVA lenh "mov x16,#N" THAT - da tim lai bang disassemble
# lui tu svc#0, KHONG gia dinh khoang cach co dinh vi thay doi tuy site (12 hoac 16 byte truoc
# svc#0 tuy so luong lenh setup tham so), RVA lenh "svc #0", so hieu syscall). RVA nay LA CUA
# BAN UnityFramework "und3fined" (build 1.126.1 / 2019120772, giong het build tren may - da xac
# nhan qua build_version trong crash log that).
# ============================================================================
SITES = [
    (0x96122d8, 0x96122e4, 338), (0x962a71c, 0x962a72c, 5),   (0x9634b90, 0x9634b9c, 33),
    (0x9634eb0, 0x9634ec0, 5),   (0x9640074, 0x9640084, 5),   (0x9693af0, 0x9693afc, 338),
    (0x96b3fe4, 0x96b3ff0, 338), (0x96b4664, 0x96b4670, 338), (0x96d6678, 0x96d6684, 338),
    (0x96efd8c, 0x96efd98, 33),  (0x96efff8, 0x96f0008, 5),   (0x96f08dc, 0x96f08e8, 338),
    (0x97875c8, 0x97875d4, 338), (0x97b0da0, 0x97b0dac, 338), (0x97b2d74, 0x97b2d80, 33),
    (0x97b490c, 0x97b4918, 338), (0x97c53cc, 0x97c53d8, 33),  (0x98032e0, 0x98032ec, 338),
    (0x9821248, 0x9821254, 338), (0x9823e78, 0x9823e84, 338), (0x984142c, 0x9841438, 338),
    (0x98427e4, 0x98427f0, 338), (0x9a59dcc, 0x9a59dd8, 338), (0x9a5a790, 0x9a5a79c, 338),
    (0x9a72bf8, 0x9a72c04, 338), (0x9a72c28, 0x9a72c34, 33),  (0x9a78474, 0x9a78480, 338),
    (0x9a784a0, 0x9a784ac, 33),  (0x9a78d94, 0x9a78da0, 338), (0x9a78dc0, 0x9a78dcc, 33),
    (0x9a9def0, 0x9a9defc, 338), (0x9a9ee4c, 0x9a9ee58, 33),  (0x9a9ef78, 0x9a9ef84, 33),
    (0x9a9f37c, 0x9a9f388, 338), (0x9aae52c, 0x9aae538, 338), (0x9ad6598, 0x9ad65a4, 338),
    (0x9b8a16c, 0x9b8a178, 33),  (0x9b8a1a0, 0x9b8a1ac, 33),  (0x9b8a1d4, 0x9b8a1e0, 33),
    (0x9b93bec, 0x9b93bf8, 338), (0x9b947d8, 0x9b947e4, 338), (0x9ba0e14, 0x9ba0e20, 338),
    (0x9ba97f0, 0x9ba97fc, 338), (0x9ba9980, 0x9ba998c, 33),  (0x9ba9e70, 0x9ba9e7c, 338),
    (0x9bab02c, 0x9bab038, 33),  (0x9bb05a8, 0x9bb05b4, 33),  (0x9bb1174, 0x9bb1180, 33),
    (0x9bd28f8, 0x9bd2904, 338), (0x9bd4b54, 0x9bd4b60, 33),
]

CODE_SLACK_START = 0xB64AB44   # dau khoang trong cuoi __TEXT (sau __eh_frame that)
CODE_SLACK_END   = 0xB64C000   # bien segment __TEXT (dau __DATA)
DATA_SLACK_START = 0xC419810   # dau khoang trong CO FILE THAT trong __DATA (sau __thread_vars)
DATA_SLACK_END   = 0xC41C000   # fileoff cua __LINKEDIT (het phan file that cua __DATA)

X16, X17, X9, X30 = 16, 17, 9, 30

# Khung sp (160 byte) luu TOAN BO thanh ghi caller-saved (x1-x17, x29, x30 - tru x0: co y KHONG
# luu, vi x0 la path argument/return that cua callback, PHAI duoc phep doi). Ly do mo rong tu ban
# dau chi luu x1/x2/x16/x30: test tren may that (delta_early_diag.log + CrashLogger) cho thay
# crash exc=1 trong libsystem_platform.dylib ~2s sau khi vao tran - dung luc cac diem syscall nay
# duoc goi lien tuc de doc asset. Diem chen trampoline nam GIUA 1 ham khac (khong phai ranh gioi
# goi ham that su do compiler dinh), nen code xung quanh co the dang giu gia tri song trong bat
# ky thanh ghi caller-saved nao (khong chi x1/x2) - callback (goi NSString/ObjC ben trong
# redirectAllTrafficPath) duoc phep clobber tat ca thanh ghi do theo dung AAPCS64, gay hong du
# lieu tre, bieu hien thanh crash o cho khac hoan toan (memmove/memset) sau do. Monite (theo phan
# tich Ghidra truoc do) luu DAY DU thanh ghi trong trampoline cua ho - sua lai cho khop.
_SAVE_PAIRS = [(1, 2, 0), (3, 4, 16), (5, 6, 32), (7, 8, 48), (9, 10, 64),
               (11, 12, 80), (13, 14, 96), (15, 16, 112), (17, 29, 128)]
_SAVE_FRAME_SIZE = 160
_X30_OFF = 144


def encode_shared_routine(sr_addr, data_slot_addr):
    """Tra ve bytes cua ham dung chung: luu toan bo thanh ghi caller-saved, doc con tro callback
    (data_slot_addr), goi (neu co), khoi phuc lai TOAN BO thanh ghi + so hieu syscall, thuc hien
    syscall THAT, roi ret - GIU NGUYEN co carry/x0 that cua chinh syscall that (ret khong dung
    NZCV) de code sau do (b.lo/kiem tra x0 truc tiep) hoat dong dung y het ban goc, bat ke
    redirect co xay ra hay khong."""
    insns = []
    offset = 0
    insns.append(asm.sub_sp_sp_imm(_SAVE_FRAME_SIZE)); offset += 4      # +0
    for r1, r2, off in _SAVE_PAIRS:
        insns.append(asm.stp_x_sp(r1, r2, off)); offset += 4
    insns.append(asm.str_x_sp(X30, _X30_OFF)); offset += 4

    adrp_addr = sr_addr + offset
    cur_page = adrp_addr & ~0xFFF
    tgt_page = data_slot_addr & ~0xFFF
    delta_pages = (tgt_page - cur_page) // 0x1000
    page_off = data_slot_addr & 0xFFF
    insns.append(asm.adrp(X9, delta_pages)); offset += 4
    insns.append(asm.add_reg_imm(X9, X9, page_off)); offset += 4
    insns.append(asm.ldr_x_reg_imm(X9, X9, 0)); offset += 4   # x9 = *callback_ptr (0 neu Delta.dylib chua ghi)

    # cbz x9, do_syscall (blr la lenh duy nhat xen giua -> cach 2 lenh, giong ban cu)
    insns.append(asm.cbz_x(X9, 2)); offset += 4
    insns.append(asm.blr(X9)); offset += 4   # goi callback(path=x0) -> x0 = path moi (hoac cu)

    # do_syscall:
    for r1, r2, off in _SAVE_PAIRS:
        insns.append(asm.ldp_x_sp(r1, r2, off)); offset += 4
    insns.append(asm.ldr_x_sp(X30, _X30_OFF)); offset += 4
    insns.append(asm.add_sp_sp_imm(_SAVE_FRAME_SIZE)); offset += 4
    insns.append(asm.svc0()); offset += 4   # syscall THAT, x0=path (co the da doi)
    insns.append(asm.ret()); offset += 4

    blob = b''.join(insns)
    assert len(blob) == offset
    return blob


def encode_micro_stub(stub_addr, syscall_num, shared_routine_addr, resume_addr):
    insns = []
    insns.append(asm.mov_imm(X16, syscall_num))           # +0
    bl_words = (shared_routine_addr - (stub_addr + 4)) // 4
    insns.append(asm.bl(bl_words))                          # +4
    adrp_addr = stub_addr + 8
    cur_page = adrp_addr & ~0xFFF
    tgt_page = resume_addr & ~0xFFF
    delta_pages = (tgt_page - cur_page) // 0x1000
    page_off = resume_addr & 0xFFF
    insns.append(asm.adrp(X17, delta_pages))                # +8
    insns.append(asm.add_reg_imm(X17, X17, page_off))       # +12
    insns.append(asm.br(X17))                                # +16
    blob = b''.join(insns)
    assert len(blob) == 20, len(blob)
    return blob


def main():
    if len(sys.argv) != 3:
        print('usage: patch_unityframework_syscalls.py <in_UnityFramework> <out_UnityFramework>')
        sys.exit(1)
    in_path, out_path = sys.argv[1], sys.argv[2]

    with open(in_path, 'rb') as f:
        data = bytearray(f.read())

    # 1. Kiem tra khoang trong con dung toan so 0 (an toan truoc khi ghi de)
    code_slack = data[CODE_SLACK_START:CODE_SLACK_END]
    data_slack = data[DATA_SLACK_START:DATA_SLACK_END]
    if any(code_slack) :
        print('LOI: vung trong __TEXT KHONG con toan so 0 - file khac voi ban da phan tich, HUY.')
        sys.exit(2)
    if any(data_slack):
        print('LOI: vung trong __DATA KHONG con toan so 0 - file khac voi ban da phan tich, HUY.')
        sys.exit(2)
    _sr_size_estimate = 4 * (1 + len(_SAVE_PAIRS) + 1 + 3 + 2 + len(_SAVE_PAIRS) + 1 + 1 + 1 + 1)
    print(f'khoang trong __TEXT: {len(code_slack)} byte (can ~{_sr_size_estimate + 20*len(SITES)}), '
          f'__DATA: {len(data_slack)} byte (can 8) - OK')

    data_slot_addr = DATA_SLACK_START

    sr_addr = CODE_SLACK_START
    sr_bytes = encode_shared_routine(sr_addr, data_slot_addr)

    stub_base = sr_addr + len(sr_bytes)
    code_blob = bytearray(sr_bytes)

    patched_sites = 0
    for i, (mov_x16_addr, svc_addr, syscall_num) in enumerate(SITES):
        stub_addr = stub_base + i * 20
        resume_addr = svc_addr + 4          # dia chi ngay sau lenh svc#0 goc

        # kiem tra byte goc dung "mov x16,#N; svc#0" nhu da phan tich, HUY neu khac
        orig_mov = bytes(data[mov_x16_addr:mov_x16_addr+4])
        orig_svc = bytes(data[svc_addr:svc_addr+4])
        expected_mov = asm.mov_imm(16, syscall_num)
        expected_svc = asm.svc0()
        if orig_mov != expected_mov or orig_svc != expected_svc:
            print(f'  BO QUA 0x{svc_addr:x}: byte goc khac du lieu da phan tich '
                  f'(mov thuc={orig_mov.hex()} ky vong={expected_mov.hex()}, '
                  f'svc thuc={orig_svc.hex()})')
            continue

        stub_bytes = encode_micro_stub(stub_addr, syscall_num, sr_addr, resume_addr)
        code_blob += stub_bytes

        # va lenh dau ("mov x16,#N") thanh "b <stub_addr>" - CUNG KICH THUOC 4 byte, KHONG
        # dung lenh thu 2 (svc#0 goc, van con nguyen tai svc_addr nhung khong bao gio chay
        # toi vi lenh b nhay thang di, khong roi qua).
        b_words = (stub_addr - mov_x16_addr) // 4
        data[mov_x16_addr:mov_x16_addr+4] = asm.b(b_words)
        patched_sites += 1

    assert len(code_blob) <= (CODE_SLACK_END - CODE_SLACK_START), 'code vuot qua khoang trong!'
    data[CODE_SLACK_START:CODE_SLACK_START+len(code_blob)] = code_blob
    # data_slot 8 byte giu nguyen = 0 (Delta.dylib se tu ghi luc chay)

    with open(out_path, 'wb') as f:
        f.write(data)

    print(f'DA VA {patched_sites}/{len(SITES)} diem syscall.')
    print(f'shared_routine @ 0x{sr_addr:x} (len={len(sr_bytes)})')
    print(f'micro-stubs bat dau @ 0x{stub_base:x}, tong code = {len(code_blob)} byte')
    print(f'data slot (con tro callback) @ 0x{data_slot_addr:x}')
    print(f'da ghi: {out_path}')


if __name__ == '__main__':
    main()
