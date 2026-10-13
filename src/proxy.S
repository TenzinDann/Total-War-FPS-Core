    .intel_syntax noprefix
    .text
    .macro FWD name
    .globl proxy_\name
    .def proxy_\name; .scl 2; .type 32; .endef
proxy_\name:
    jmp qword ptr [rip + p_\name]
    .endm
    FWD GetFileVersionInfoA
    FWD GetFileVersionInfoByHandle
    FWD GetFileVersionInfoExA
    FWD GetFileVersionInfoExW
    FWD GetFileVersionInfoSizeA
    FWD GetFileVersionInfoSizeExA
    FWD GetFileVersionInfoSizeExW
    FWD GetFileVersionInfoSizeW
    FWD GetFileVersionInfoW
    FWD VerFindFileA
    FWD VerFindFileW
    FWD VerInstallFileA
    FWD VerInstallFileW
    FWD VerLanguageNameA
    FWD VerLanguageNameW
    FWD VerQueryValueA
    FWD VerQueryValueW
    .globl hfps_cam_update_thunk
    .def hfps_cam_update_thunk; .scl 2; .type 32; .endef
    .seh_proc hfps_cam_update_thunk
hfps_cam_update_thunk:
    push rbx
    .seh_pushreg rbx
    push rsi
    .seh_pushreg rsi
    sub rsp, 40
    .seh_stackalloc 40
    .seh_endprologue
    mov rbx, rcx
    call qword ptr [rip + g_orig_cam_update]
    mov rsi, rax
    mov rcx, rbx
    call hfps_after_cam_update
    mov rax, rsi
    add rsp, 40
    pop rsi
    pop rbx
    ret
    .seh_endproc
